using System.Buffers;
using System.Net;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text.Json;

namespace LYIsland.Transfer;

public sealed partial class LanNode
{
    private sealed record OutgoingFile(string Path, FileInfoDto Offer);

    public async Task SendAsync(LanPeer peer, IReadOnlyList<string> paths, CancellationToken cancellationToken = default)
    {
        EnsureStarted();
        using var operation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, _lifetime.Token);
        lock (_gate)
        {
            if (_incoming is not null || _outgoing is not null) throw new InvalidOperationException("已有传输正在进行，请先完成或取消。");
            _outgoing = operation;
            _outgoingPeer = peer;
            _outgoingSessionId = null;
        }
        string? sessionId = null;
        bool prepareStarted = false, completed = false;
        long sent = 0, total = 0;
        int count = 0, completedFiles = 0;
        string currentName = "";
        try
        {
            SetStatus(new("send", TransferPhase.WaitingForAcceptance, peer.Alias, "", 0, 0, 0, 0, "正在准备文件…", null));
            if (!IPAddress.TryParse(peer.Address, out var address) || !TransferSafety.IsLocalAddress(address, _options.AllowLoopback)
                || peer.Port is < 1 or > 65535 || !TransferSafety.ValidFingerprint(peer.Fingerprint)
                || peer.Fingerprint.Equals(Fingerprint, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("接收方地址或身份无效。");
            bool appendSha256 = peer.SupportsAppendedSha256;
            var files = await PrepareOutgoingFilesAsync(paths, appendSha256, operation.Token);
            total = files.Sum(f => f.Offer.Size);
            count = files.Count;
            var offers = files.ToDictionary(f => f.Offer.Id, f => f.Offer);
            byte[] manifest = JsonSerializer.SerializeToUtf8Bytes(new PrepareRequest(Info(), offers, appendSha256), Json);
            if (manifest.Length > TransferSafety.MaximumMetadataBytes)
                throw new InvalidDataException("文件信息过多，请分批发送。");
            using var client = CreateClient(peer.Fingerprint);
            SetStatus(new("send", TransferPhase.WaitingForAcceptance, peer.Alias, "", 0, total, count, 0, "等待对方确认接收…", null));
            using var prepareTimeout = CancellationTokenSource.CreateLinkedTokenSource(operation.Token);
            prepareTimeout.CancelAfter(TimeSpan.FromSeconds(150));
            using var prepare = new HttpRequestMessage(HttpMethod.Post, Endpoint(peer.Address, peer.Port, "prepare-upload"))
            { Content = new ByteArrayContent(manifest) };
            prepare.Content.Headers.ContentType = new("application/json");
            prepareStarted = true;
            using var response = await client.SendAsync(prepare, HttpCompletionOption.ResponseHeadersRead, prepareTimeout.Token);
            if (response.StatusCode == HttpStatusCode.Forbidden)
            {
                completed = true;
                SetStatus(new("send", TransferPhase.Rejected, peer.Alias, "", 0, total, count, 0, "对方拒绝接收或确认已超时。", null));
                return;
            }
            if (response.StatusCode == HttpStatusCode.Conflict) throw new IOException("对方正在传输其他文件，请稍后重试。");
            if (response.StatusCode == HttpStatusCode.Unauthorized) throw new IOException("对方启用了 PIN 验证，请关闭对方 PIN 后重试。");
            if (response.StatusCode == HttpStatusCode.NoContent)
            {
                completed = true;
                SetStatus(new("send", TransferPhase.Rejected, peer.Alias, "", 0, total, count, 0, "对方未选择接收文件。", null));
                return;
            }
            response.EnsureSuccessStatusCode();
            var accepted = await ReadResponseJsonAsync<PrepareResponse>(response, prepareTimeout.Token);
            if (accepted is null || string.IsNullOrWhiteSpace(accepted.SessionId) || accepted.SessionId.Length > 200
                || accepted.Files is null || accepted.Files.Count is < 1 or > TransferSafety.MaximumFiles
                || accepted.Files.Any(p => !offers.ContainsKey(p.Key) || string.IsNullOrWhiteSpace(p.Value) || p.Value.Length > 512))
                throw new InvalidDataException("接收方返回了无效的传输信息。");
            sessionId = accepted.SessionId;
            lock (_gate) _outgoingSessionId = sessionId;
            var selected = files.Where(f => accepted.Files.ContainsKey(f.Offer.Id)).ToArray();
            total = selected.Sum(f => f.Offer.Size);
            count = selected.Length;
            foreach (var file in selected)
            {
                operation.Token.ThrowIfCancellationRequested();
                currentName = file.Offer.FileName;
                TransferSafety.EnsureNoReparsePoints(file.Path);
                if (new FileInfo(file.Path).Length != file.Offer.Size) throw new IOException("发送前文件已发生变化，请重新选择。");
                SetStatus(new("send", TransferPhase.Transferring, peer.Alias, currentName, sent, total, count, completedFiles, "正在发送…", null));
                string query = "upload?sessionId=" + Uri.EscapeDataString(sessionId) + "&fileId=" + Uri.EscapeDataString(file.Offer.Id)
                    + "&token=" + Uri.EscapeDataString(accepted.Files[file.Offer.Id]);
                using var upload = new HttpRequestMessage(HttpMethod.Post, EndpointWithQuery(peer.Address, peer.Port, query));
                long nextProgressTick = Environment.TickCount64 + ProgressIntervalMilliseconds;
                upload.Content = new ProgressFileContent(file.Path, file.Offer.Size, appendSha256, bytes =>
                {
                    sent += bytes;
                    long now = Environment.TickCount64;
                    if (now < nextProgressTick) return;
                    nextProgressTick = now + ProgressIntervalMilliseconds;
                    SetStatus(new("send", TransferPhase.Transferring, peer.Alias, currentName, sent, total, count, completedFiles, "正在发送…", null));
                }, operation.Token);
                using var uploaded = await client.SendAsync(upload, HttpCompletionOption.ResponseHeadersRead, operation.Token);
                // The pinned receiver may end the upload before its separate authenticated cancel request arrives.
                if (uploaded.StatusCode == HttpStatusCode.Conflict)
                {
                    operation.Cancel();
                    throw new OperationCanceledException(operation.Token);
                }
                if ((int)uploaded.StatusCode == 422) throw new IOException("文件校验不一致，接收方已丢弃未完成文件。");
                uploaded.EnsureSuccessStatusCode();
                completedFiles++;
            }
            completed = true;
            _lastError = null;
            SetStatus(new("send", TransferPhase.Completed, peer.Alias, currentName, sent, total, count, completedFiles, "发送完成。", null));
        }
        catch (OperationCanceledException)
        {
            SetStatus(new("send", operation.IsCancellationRequested ? TransferPhase.Cancelled : TransferPhase.Failed,
                peer.Alias, currentName, sent, total, count, completedFiles,
                operation.IsCancellationRequested ? "已取消发送。" : "等待接收方响应超时。", null));
        }
        catch (Exception ex) when (ex is IOException or HttpRequestException or UnauthorizedAccessException or JsonException
            or ArgumentException or OverflowException or System.Security.Authentication.AuthenticationException)
        {
            if (operation.IsCancellationRequested)
                SetStatus(new("send", TransferPhase.Cancelled, peer.Alias, currentName, sent, total, count, completedFiles, "已取消发送。", null));
            else
            {
                _lastError = "发送失败：" + ex.Message;
                SetStatus(new("send", TransferPhase.Failed, peer.Alias, currentName, sent, total, count, completedFiles, _lastError, null));
            }
        }
        finally
        {
            if (prepareStarted && !completed) await TryCancelRemoteAsync(peer, sessionId);
            lock (_gate)
            {
                if (ReferenceEquals(_outgoing, operation))
                { _outgoing = null; _outgoingPeer = null; _outgoingSessionId = null; }
            }
            RaiseChanged();
        }
    }

    private static Uri EndpointWithQuery(string address, int port, string pathAndQuery)
    {
        int separator = pathAndQuery.IndexOf('?');
        var uri = new UriBuilder(Endpoint(address, port, separator < 0 ? pathAndQuery : pathAndQuery[..separator]));
        if (separator >= 0) uri.Query = pathAndQuery[(separator + 1)..];
        return uri.Uri;
    }

    private async Task TryCancelRemoteAsync(LanPeer peer, string? sessionId)
    {
        try
        {
            if (_identity is null || !TransferSafety.ValidFingerprint(peer.Fingerprint)) return;
            using var client = CreateClient(peer.Fingerprint);
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
            string path = "cancel" + (sessionId is null ? "" : "?sessionId=" + Uri.EscapeDataString(sessionId));
            using var request = new HttpRequestMessage(HttpMethod.Post, EndpointWithQuery(peer.Address, peer.Port, path));
            using var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, timeout.Token);
        }
        catch (Exception ex) when (ex is HttpRequestException or OperationCanceledException or ObjectDisposedException or ArgumentException) { }
    }

    private static async Task<List<OutgoingFile>> PrepareOutgoingFilesAsync(IReadOnlyList<string> paths, bool appendSha256, CancellationToken token)
    {
        if (paths.Count is < 1 or > TransferSafety.MaximumFiles) throw new InvalidDataException("请选择 1–1000 个文件。");
        var candidates = new List<(string Path, string Name)>();
        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        void AddFile(string full, string name)
        {
            token.ThrowIfCancellationRequested();
            TransferSafety.EnsureNoReparsePoints(full);
            if (!visited.Add(full)) return;
            string safe = TransferSafety.SafeRelativePath(name);
            if (!names.Add(safe)) throw new InvalidDataException("存在同名文件，请分批发送。");
            candidates.Add((full, safe.Replace('\\', '/')));
            if (candidates.Count > TransferSafety.MaximumFiles) throw new InvalidDataException("每次最多发送 1000 个文件，请分批发送。");
        }
        foreach (string path in paths)
        {
            token.ThrowIfCancellationRequested();
            string full = Path.GetFullPath(path);
            TransferSafety.EnsureNoReparsePoints(full);
            if (File.Exists(full)) AddFile(full, Path.GetFileName(full));
            else if (Directory.Exists(full))
            {
                string rootName = new DirectoryInfo(full).Name;
                var pending = new Stack<string>(); pending.Push(full);
                while (pending.TryPop(out string? folder))
                {
                    token.ThrowIfCancellationRequested();
                    TransferSafety.EnsureNoReparsePoints(folder);
                    foreach (string entry in Directory.EnumerateFileSystemEntries(folder))
                    {
                        TransferSafety.EnsureNoReparsePoints(entry);
                        if (Directory.Exists(entry)) pending.Push(entry);
                        else AddFile(entry, Path.Combine(rootName, Path.GetRelativePath(full, entry)));
                    }
                }
            }
            else throw new FileNotFoundException("所选文件已不存在。", full);
        }
        if (candidates.Count == 0) throw new InvalidDataException("所选目录中没有可发送的文件。");
        var result = new List<OutgoingFile>(candidates.Count);
        long total = 0;
        foreach (var candidate in candidates)
        {
            token.ThrowIfCancellationRequested();
            long size = new FileInfo(candidate.Path).Length;
            total = checked(total + size);
            if (size > TransferSafety.MaximumFileBytes || total > TransferSafety.MaximumFileBytes) throw new InvalidDataException("文件总量超过单次传输限制。");
            string? hash = null;
            if (!appendSha256)
            {
                await using var stream = new FileStream(candidate.Path, FileMode.Open, FileAccess.Read, FileShare.Read,
                    TransferBufferSize, FileOptions.Asynchronous | FileOptions.SequentialScan);
                if (stream.Length != size) throw new IOException("准备发送时文件已发生变化。");
                hash = Convert.ToHexString(await SHA256.HashDataAsync(stream, token));
            }
            result.Add(new(candidate.Path, new FileInfoDto { Id = Guid.NewGuid().ToString("N"), FileName = candidate.Name, Size = size, Sha256 = hash }));
        }
        return result;
    }

    private sealed class ProgressFileContent(string path, long length, bool appendSha256, Action<int> progress, CancellationToken operationToken) : HttpContent
    {
        protected override bool TryComputeLength(out long size)
        {
            size = appendSha256 ? length + SHA256.HashSizeInBytes : length;
            return true;
        }
        protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context) => CopyAsync(stream, operationToken);
        protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context, CancellationToken cancellationToken) => CopyAsync(stream, cancellationToken);
        private async Task CopyAsync(Stream output, CancellationToken token)
        {
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(token, operationToken);
            TransferSafety.EnsureNoReparsePoints(path);
            await using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read,
                TransferBufferSize, FileOptions.Asynchronous | FileOptions.SequentialScan);
            if (input.Length != length) throw new IOException("文件已发生变化。");
            using var checksum = appendSha256 ? IncrementalHash.CreateHash(HashAlgorithmName.SHA256) : null;
            byte[] buffer = ArrayPool<byte>.Shared.Rent(TransferBufferSize);
            try
            {
                long copied = 0;
                while (true)
                {
                    int read = await input.ReadAsync(buffer.AsMemory(0, TransferBufferSize), linked.Token);
                    if (read == 0) break;
                    copied += read;
                    if (copied > length) throw new IOException("文件已发生变化。");
                    checksum?.AppendData(buffer, 0, read);
                    await output.WriteAsync(buffer.AsMemory(0, read), linked.Token);
                    progress(read);
                }
                if (copied != length) throw new IOException("文件已发生变化。");
                if (checksum is not null) await output.WriteAsync(checksum.GetHashAndReset(), linked.Token);
            }
            finally { ArrayPool<byte>.Shared.Return(buffer, clearArray: true); }
        }
    }
}
