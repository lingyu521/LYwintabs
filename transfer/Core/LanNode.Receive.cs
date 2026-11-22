using System.Buffers;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Server.Kestrel.Core.Features;

namespace LYIsland.Transfer;

public sealed partial class LanNode
{
    private sealed record IncomingDecision(bool Accepted, string? ReceiveDirectory = null);

    private sealed class IncomingSession
    {
        public required string Id { get; init; }
        public required string SenderAlias { get; init; }
        public required string Address { get; init; }
        public required string Fingerprint { get; init; }
        public required int SenderPort { get; init; }
        public required Dictionary<string, FileInfoDto> Files { get; init; }
        public required Dictionary<string, string> Tokens { get; init; }
        public required long TotalBytes { get; init; }
        public required CancellationTokenSource Cancellation { get; init; }
        public required CancellationToken Token { get; init; }
        public TaskCompletionSource<IncomingDecision> Decision { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public HashSet<string> Active { get; } = [];
        public HashSet<string> Completed { get; } = [];
        public string? Directory { get; set; }
        public string DestinationRoot { get; init; } = "";
        public bool AppendSha256 { get; init; }
        public TaskCompletionSource? Resume;
        public long ReceivedBytes;
        public long NextProgressTick;
        public DateTimeOffset LastActivity { get; set; } = DateTimeOffset.UtcNow;
    }

    /// <summary>Accepts this request only. A custom root gets its own unique batch subdirectory.</summary>
    public void AcceptIncoming(string requestId, string? receiveDirectory = null)
    {
        lock (_gate)
        {
            if (_incoming?.Id == requestId && _pendingIncoming?.RequestId == requestId)
                _incoming.Decision.TrySetResult(new(true, receiveDirectory));
        }
    }

    public void RejectIncoming(string requestId)
    {
        lock (_gate)
        {
            if (_incoming?.Id == requestId && _pendingIncoming?.RequestId == requestId)
                _incoming.Decision.TrySetResult(new(false));
        }
    }

    internal void SetReceivingPaused(bool paused, string? requestId = null)
    {
        lock (_gate)
        {
            if (_incoming is not { Directory: not null } session ||
                (requestId is not null && session.Id != requestId) || (session.Resume is not null) == paused) return;
            var previous = session.Resume;
            Volatile.Write(ref session.Resume, paused ? new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously) : null);
            session.LastActivity = DateTimeOffset.UtcNow;
            var status = Status;
            SetStatus(status with
            {
                Phase = paused ? TransferPhase.Paused : TransferPhase.Transferring,
                BytesTransferred = Interlocked.Read(ref session.ReceivedBytes),
                Message = paused ? "接收已暂停。" : "正在接收…"
            });
            previous?.TrySetResult();
        }
    }

    private static async ValueTask WaitForReceiveResumeAsync(IncomingSession session, CancellationToken token)
    {
        while (Volatile.Read(ref session.Resume) is { } resume) await resume.Task.WaitAsync(token);
        token.ThrowIfCancellationRequested();
    }

    private async Task PrepareUploadAsync(HttpContext context)
    {
        if (context.Request.ContentLength > TransferSafety.MaximumMetadataBytes)
        { context.Response.StatusCode = 413; return; }
        string address = TransferSafety.NormalizeAddress(context.Connection.RemoteIpAddress!).ToString();
        var now = DateTimeOffset.UtcNow;
        if (_prepareAttempts.TryGetValue(address, out var last) && now - last < TimeSpan.FromMilliseconds(250))
        { context.Response.StatusCode = 429; return; }
        if (_prepareAttempts.Count > 1024) { context.Response.StatusCode = 429; return; }
        _prepareAttempts[address] = now;
        var body = await ReadJsonAsync<PrepareRequest>(context.Request.Body, TransferSafety.MaximumMetadataBytes, context.RequestAborted);
        string? fingerprint = await ClientFingerprintAsync(context);
        if (body?.Info is null || !ValidInfo(body.Info) || body.Files is null || fingerprint is null || body.Files.Count is < 1 or > TransferSafety.MaximumFiles)
        { context.Response.StatusCode = 400; return; }
        long total = 0;
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var pair in body.Files)
        {
            var file = pair.Value;
            if (file is null || string.IsNullOrWhiteSpace(pair.Key) || pair.Key.Length > 200 || file.Id != pair.Key
                || file.Size < 0 || file.Size > TransferSafety.MaximumFileBytes
                || file.Sha256 is not null && !TransferSafety.ValidFingerprint(file.Sha256))
            { context.Response.StatusCode = 400; return; }
            string name = TransferSafety.SafeRelativePath(file.FileName);
            if (!names.Add(name)) { context.Response.StatusCode = 400; return; }
            total = checked(total + file.Size);
            if (total > TransferSafety.MaximumFileBytes) { context.Response.StatusCode = 400; return; }
        }
        // A file may not be the ancestor directory of another file.
        foreach (string name in names)
        {
            string? ancestor = Path.GetDirectoryName(name);
            while (!string.IsNullOrEmpty(ancestor))
            {
                if (names.Contains(ancestor)) { context.Response.StatusCode = 400; return; }
                ancestor = Path.GetDirectoryName(ancestor);
            }
        }
        string root = ReceiveDirectory;
        var cancellation = CancellationTokenSource.CreateLinkedTokenSource(_lifetime.Token);
        var session = new IncomingSession
        {
            Id = Guid.NewGuid().ToString("N"), SenderAlias = TransferSafety.NormalizeAlias(body.Info.Alias),
            Address = address, Fingerprint = fingerprint, SenderPort = body.Info.Port, Files = body.Files, TotalBytes = total,
            Tokens = body.Files.Keys.ToDictionary(key => key, _ => Convert.ToHexString(RandomNumberGenerator.GetBytes(32))),
            DestinationRoot = root, AppendSha256 = body.AppendSha256, Cancellation = cancellation, Token = cancellation.Token
        };
        lock (_gate)
        {
            if (_incoming is not null || _outgoing is not null)
            { cancellation.Dispose(); context.Response.StatusCode = 409; return; }
            _incoming = session;
            _pendingIncoming = new IncomingTransfer(session.Id, session.SenderAlias, address, total,
                body.Files.Values.Select(f => new TransferFileOffer(f.FileName, f.Size)).ToArray());
            SetStatus(new("receive", TransferPhase.AwaitingDecision, session.SenderAlias, "", 0, total, body.Files.Count, 0, "收到文件请求，等待你确认。", null));
        }
        try
        {
            using var approvalTimeout = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted, session.Token);
            approvalTimeout.CancelAfter(TimeSpan.FromMinutes(2));
            IncomingDecision decision = await session.Decision.Task.WaitAsync(approvalTimeout.Token);
            if (!decision.Accepted)
            {
                FinishIncoming(session, TransferPhase.Rejected, "已拒绝接收。");
                context.Response.StatusCode = 403;
                return;
            }
            session.Token.ThrowIfCancellationRequested();
            // Resolve and validate only the accepted destination. A bad default can be
            // bypassed with Save As, and no receive directories exist before consent.
            root = decision.ReceiveDirectory is null ? session.DestinationRoot : Path.GetFullPath(decision.ReceiveDirectory);
            TransferSafety.EnsureNoReparsePoints(root);
            CheckDiskSpace(root, total);
            string sessionDirectory = Path.Combine(root, $"LYwintabs_{DateTime.Now:yyyyMMdd_HHmmss}_{session.Id[..8]}");
            lock (_gate)
            {
                if (!ReferenceEquals(_incoming, session) || session.Token.IsCancellationRequested) throw new OperationCanceledException();
                TransferSafety.EnsureNoReparsePoints(root);
                System.IO.Directory.CreateDirectory(root);
                TransferSafety.EnsureNoReparsePoints(root);
                if (System.IO.Directory.Exists(sessionDirectory) || File.Exists(sessionDirectory)) throw new IOException("接收目录已存在，请重试。");
                System.IO.Directory.CreateDirectory(sessionDirectory);
                session.Directory = sessionDirectory;
                TransferSafety.EnsureNoReparsePoints(sessionDirectory);
                _pendingIncoming = null;
                session.LastActivity = DateTimeOffset.UtcNow;
                SetStatus(new("receive", TransferPhase.Transferring, session.SenderAlias, "", 0, total, body.Files.Count, 0, "准备接收文件…", sessionDirectory));
            }
            await context.Response.WriteAsJsonAsync(new PrepareResponse(session.Id, session.Tokens), Json, context.RequestAborted);
        }
        catch (OperationCanceledException)
        {
            FinishIncoming(session, TransferPhase.Cancelled, "接收请求已取消或超时。");
            if (!context.Response.HasStarted) context.Response.StatusCode = 403;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            FinishIncoming(session, TransferPhase.Failed, "无法创建接收目录：" + ex.Message);
            if (!context.Response.HasStarted) context.Response.StatusCode = 500;
        }
        finally { RemoveEmptySessionDirectory(session); }
    }

    private static void CheckDiskSpace(string root, long required)
    {
        string? driveRoot = Path.GetPathRoot(Path.GetFullPath(root));
        if (driveRoot is null) throw new IOException("接收路径无效。");
        var drive = new DriveInfo(driveRoot);
        if (drive.IsReady && drive.AvailableFreeSpace < checked(required + 16 * 1024 * 1024L))
            throw new IOException("接收磁盘剩余空间不足。");
    }

    private async Task UploadAsync(HttpContext context)
    {
        string sessionId = context.Request.Query["sessionId"].ToString();
        string fileId = context.Request.Query["fileId"].ToString();
        string token = context.Request.Query["token"].ToString();
        string address = TransferSafety.NormalizeAddress(context.Connection.RemoteIpAddress!).ToString();
        string? fingerprint = await ClientFingerprintAsync(context);
        IncomingSession? session;
        FileInfoDto? file;
        lock (_gate)
        {
            session = _incoming;
            if (session is null || session.Id != sessionId || session.Directory is null
                || session.Address != address || !string.Equals(session.Fingerprint, fingerprint, StringComparison.OrdinalIgnoreCase)
                || !session.Tokens.TryGetValue(fileId, out var expected)
                || !TokensEqual(expected, token) || !session.Files.TryGetValue(fileId, out file))
            { context.Response.StatusCode = 403; return; }
            if (session.Active.Contains(fileId) || session.Completed.Contains(fileId))
            { context.Response.StatusCode = 409; return; }
            session.Active.Add(fileId);
            session.LastActivity = DateTimeOffset.UtcNow;
        }
        string? temporaryPath = null;
        try
        {
            long expectedLength = file.Size + (session.AppendSha256 ? SHA256.HashSizeInBytes : 0);
            if (context.Request.ContentLength is long length ? length != expectedLength : session.AppendSha256)
                throw new InvalidDataException("文件大小与声明不符。");
            // Only an authenticated upload accepted by the user may be paused.
            if (context.Features.Get<IHttpMinRequestBodyDataRateFeature>() is { } rate) rate.MinDataRate = null;
            string destination = TransferSafety.ContainedPath(session.Directory, file.FileName);
            TransferSafety.EnsureNoReparsePoints(destination);
            System.IO.Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            TransferSafety.EnsureNoReparsePoints(destination);
            temporaryPath = Path.Combine(Path.GetDirectoryName(destination)!, ".lyisland-" + Guid.NewGuid().ToString("N") + ".partial");
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted, session.Token);
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            long fileBytes = 0;
            byte[]? expectedHash = null;
            await using (var output = new FileStream(temporaryPath, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                TransferBufferSize, FileOptions.Asynchronous | FileOptions.SequentialScan))
            {
                byte[] buffer = ArrayPool<byte>.Shared.Rent(TransferBufferSize);
                try
                {
                    while (true)
                    {
                        await WaitForReceiveResumeAsync(session, linked.Token);
                        int desired = session.AppendSha256 ? (int)Math.Min(file.Size - fileBytes, TransferBufferSize) : TransferBufferSize;
                        if (desired == 0) break;
                        int read = await context.Request.Body.ReadAsync(buffer.AsMemory(0, desired), linked.Token);
                        if (read == 0) break;
                        if (fileBytes > file.Size - read) throw new InvalidDataException("收到的数据超过声明大小。");
                        await WaitForReceiveResumeAsync(session, linked.Token);
                        await output.WriteAsync(buffer.AsMemory(0, read), linked.Token);
                        hash.AppendData(buffer, 0, read);
                        fileBytes += read;
                        Interlocked.Add(ref session.ReceivedBytes, read);
                        long now = Environment.TickCount64;
                        long next = Interlocked.Read(ref session.NextProgressTick);
                        if (now < next ||
                            Interlocked.CompareExchange(ref session.NextProgressTick, now + ProgressIntervalMilliseconds, next) != next) continue;
                        lock (_gate)
                        {
                            if (!ReferenceEquals(_incoming, session) || session.Token.IsCancellationRequested) throw new OperationCanceledException();
                            if (session.Resume is not null) continue;
                            session.LastActivity = DateTimeOffset.UtcNow;
                            SetStatus(new("receive", TransferPhase.Transferring, session.SenderAlias, file.FileName,
                                Interlocked.Read(ref session.ReceivedBytes), session.TotalBytes, session.Files.Count,
                                session.Completed.Count, "正在接收…", session.Directory));
                        }
                    }
                    if (fileBytes != file.Size) throw new InvalidDataException("文件数据不完整。");
                    if (session.AppendSha256)
                    {
                        expectedHash = new byte[SHA256.HashSizeInBytes];
                        await context.Request.Body.ReadExactlyAsync(expectedHash, linked.Token);
                    }
                }
                finally { ArrayPool<byte>.Shared.Return(buffer, clearArray: true); }
                await WaitForReceiveResumeAsync(session, linked.Token);
                await output.FlushAsync(linked.Token);
            }
            byte[] digest = hash.GetHashAndReset();
            if ((expectedHash is not null && !CryptographicOperations.FixedTimeEquals(digest, expectedHash)) ||
                (file.Sha256 is not null && !Convert.ToHexString(digest).Equals(file.Sha256, StringComparison.OrdinalIgnoreCase)))
            {
                FinishIncoming(session, TransferPhase.Failed, "文件校验失败，未保存损坏文件。");
                context.Response.StatusCode = 422;
                return;
            }
            lock (_gate)
            {
                linked.Token.ThrowIfCancellationRequested();
                if (!ReferenceEquals(_incoming, session)) throw new OperationCanceledException();
                TransferSafety.EnsureNoReparsePoints(destination);
                File.Move(temporaryPath, destination, overwrite: false);
                temporaryPath = null;
                session.Completed.Add(fileId);
                int count = session.Completed.Count;
                if (count == session.Files.Count) FinishIncoming(session, TransferPhase.Completed, "文件已接收。", file.FileName);
                else SetStatus(new("receive", session.Resume is null ? TransferPhase.Transferring : TransferPhase.Paused,
                    session.SenderAlias, file.FileName, Interlocked.Read(ref session.ReceivedBytes), session.TotalBytes,
                    session.Files.Count, count, session.Resume is null ? "继续接收…" : "接收已暂停。", session.Directory));
            }
            context.Response.StatusCode = 200;
        }
        catch (OperationCanceledException)
        {
            FinishIncoming(session, TransferPhase.Cancelled, "接收已取消，未完成文件已移除。");
            if (!context.Response.HasStarted) context.Response.StatusCode = 409;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            FinishIncoming(session, TransferPhase.Failed, "接收失败：" + ex.Message);
            if (!context.Response.HasStarted) context.Response.StatusCode = ex is InvalidDataException ? 400 : 500;
        }
        finally
        {
            lock (_gate) session.Active.Remove(fileId);
            if (temporaryPath is not null)
            {
                try
                {
                    // This randomly named path was created above; never delete a sender-provided path.
                    TransferSafety.EnsureNoReparsePoints(temporaryPath);
                    File.Delete(temporaryPath);
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
            }
            RemoveEmptySessionDirectory(session);
        }
    }

    private async Task CancelIncomingAsync(HttpContext context)
    {
        string sessionId = context.Request.Query["sessionId"].ToString();
        string address = TransferSafety.NormalizeAddress(context.Connection.RemoteIpAddress!).ToString();
        string? fingerprint = await ClientFingerprintAsync(context);
        IncomingSession? session;
        lock (_gate)
        {
            session = _incoming;
            if (session is null && _outgoing is not null && _outgoingPeer is not null)
            {
                if (_outgoingPeer.Address != address || !string.Equals(_outgoingPeer.Fingerprint, fingerprint, StringComparison.OrdinalIgnoreCase)
                    || (_outgoingSessionId is not null && sessionId != _outgoingSessionId))
                { context.Response.StatusCode = 403; return; }
                _outgoing.Cancel();
                context.Response.StatusCode = 200;
                return;
            }
        }
        if (session is null) { context.Response.StatusCode = 200; return; }
        if (session.Address != address || !string.Equals(session.Fingerprint, fingerprint, StringComparison.OrdinalIgnoreCase)
            || (sessionId != session.Id && !(sessionId.Length == 0 && session.Directory is null)))
        { context.Response.StatusCode = 403; return; }
        FinishIncoming(session, TransferPhase.Cancelled, "发送方取消了传输。");
        context.Response.StatusCode = 200;
    }

    private static bool TokensEqual(string expected, string actual)
    {
        if (actual.Length != expected.Length) return false;
        return CryptographicOperations.FixedTimeEquals(Encoding.UTF8.GetBytes(expected), Encoding.UTF8.GetBytes(actual));
    }

    private void FinishIncoming(IncomingSession session, TransferPhase phase, string message, string fileName = "")
    {
        lock (_gate)
        {
            if (!ReferenceEquals(_incoming, session)) return;
            _incoming = null;
            _pendingIncoming = null;
            session.Decision.TrySetResult(new(false));
            session.Cancellation.Cancel();
            session.Cancellation.Dispose();
            SetStatus(new("receive", phase, session.SenderAlias, fileName, Interlocked.Read(ref session.ReceivedBytes),
                session.TotalBytes, session.Files.Count, session.Completed.Count, message, session.Directory));
        }
        RemoveEmptySessionDirectory(session);
    }

    private void RemoveEmptySessionDirectory(IncomingSession session)
    {
        lock (_gate)
        {
            if (ReferenceEquals(_incoming, session) || session.Active.Count != 0 || session.Directory is null) return;
            try
            {
                // Delete only the empty generated batch folder; never the chosen root
                // or completed files, and never follow a user-created reparse point.
                TransferSafety.EnsureNoReparsePoints(session.Directory);
                System.IO.Directory.Delete(session.Directory, recursive: false);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        }
    }
}
