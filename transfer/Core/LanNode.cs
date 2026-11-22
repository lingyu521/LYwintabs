using System.Collections.Concurrent;
using System.Net;
using System.Net.Http.Json;
using System.Net.Security;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Server.Kestrel.Core;
using Microsoft.AspNetCore.Server.Kestrel.Https;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace LYIsland.Transfer;

/// <summary>In-process, consent-based LocalSend v2 HTTPS transport. No external GUI or shell process.</summary>
public sealed partial class LanNode : IAsyncDisposable
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    private const int TransferBufferSize = 128 * 1024;
    private const int ProgressIntervalMilliseconds = 125;
    private readonly LanNodeOptions _options;
    private readonly object _gate = new();
    private readonly ConcurrentDictionary<string, LanPeer> _peers = new(StringComparer.OrdinalIgnoreCase);
    private readonly CancellationTokenSource _lifetime = new();
    private readonly SemaphoreSlim _startLock = new(1, 1);
    private X509Certificate2? _identity;
    private WebApplication? _server;
    private string _alias;
    private string _receiveDirectory;
    private string? _lastError;
    private TransferStatus _status = TransferStatus.Idle;
    private readonly object _statusGate = new();
    private readonly TransferRateEstimator _rate = new();
    private IncomingTransfer? _pendingIncoming;
    private IncomingSession? _incoming;
    private CancellationTokenSource? _outgoing;
    private LanPeer? _outgoingPeer;
    private string? _outgoingSessionId;
    private Task? _maintenance;
    private Task? _rateMaintenance;
    private bool _disposed;
    private readonly ConcurrentDictionary<string, DateTimeOffset> _prepareAttempts = new();

    public LanNode(LanNodeOptions options)
    {
        _options = options;
        _alias = TransferSafety.NormalizeAlias(options.Alias);
        _receiveDirectory = Path.GetFullPath(options.ReceiveDirectory);
        if (options.Port is < 0 or > 65535) throw new ArgumentOutOfRangeException(nameof(options.Port));
    }

    public event EventHandler? Changed;
    public bool IsListening { get; private set; }
    public int Port { get; private set; }
    public string Alias => Volatile.Read(ref _alias);
    public string Fingerprint { get; private set; } = "";
    public string ReceiveDirectory => Volatile.Read(ref _receiveDirectory);
    public string? LastError => Volatile.Read(ref _lastError);
    public TransferStatus Status
    {
        get
        {
            lock (_statusGate)
                return _status.Phase == TransferPhase.Transferring
                    ? _status with { BytesPerSecond = _rate.GetBytesPerSecond() } : _status;
        }
    }
    public IncomingTransfer? PendingIncoming => Volatile.Read(ref _pendingIncoming);
    public bool IsBusy
    {
        get { lock (_gate) return _incoming is not null || _outgoing is not null; }
    }
    public IReadOnlyList<LanPeer> Peers => _peers.Values.OrderBy(p => p.Alias, StringComparer.CurrentCultureIgnoreCase).ToArray();

    private DeviceInfo Info(bool? announce = null) => new()
    {
        Alias = Alias, Fingerprint = Fingerprint, Port = Port, Announce = announce, SupportsAppendedSha256 = true
    };

    public async Task StartAsync(CancellationToken cancellationToken = default)
    {
        await _startLock.WaitAsync(cancellationToken);
        try
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (IsListening) return;
            _identity ??= TransferSafety.LoadIdentity(Path.GetFullPath(_options.DataDirectory));
            Fingerprint = TransferSafety.CertificateFingerprint(_identity);
            int attempts = _options.Port == 53317 ? 11 : 1;
            Exception? last = null;
            for (int attempt = 0; attempt < attempts; attempt++)
            {
                int candidate = _options.Port == 0 ? 0 : _options.Port + attempt;
                WebApplication? app = null;
                try
                {
                    var builder = WebApplication.CreateSlimBuilder(new WebApplicationOptions
                    { Args = [], ContentRootPath = Path.GetFullPath(_options.DataDirectory), ApplicationName = typeof(LanNode).Assembly.FullName });
                    builder.Logging.ClearProviders();
                    builder.WebHost.ConfigureKestrel(server =>
                    {
                        server.AddServerHeader = false;
                        server.Limits.MaxConcurrentConnections = 48;
                        server.Limits.MaxRequestBodySize = TransferSafety.MaximumFileBytes + SHA256.HashSizeInBytes;
                        server.Limits.RequestHeadersTimeout = TimeSpan.FromSeconds(10);
                        server.Limits.KeepAliveTimeout = TimeSpan.FromSeconds(30);
                        server.ListenAnyIP(candidate, listen =>
                        {
                            listen.Protocols = HttpProtocols.Http1;
                            listen.UseHttps(https =>
                            {
                                https.ServerCertificate = _identity;
                                https.ClientCertificateMode = ClientCertificateMode.RequireCertificate;
                                https.ClientCertificateValidation = (certificate, _, _) => certificate is not null
                                    && DateTime.UtcNow >= certificate.NotBefore.ToUniversalTime()
                                    && DateTime.UtcNow <= certificate.NotAfter.ToUniversalTime();
                                https.SslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13;
                            });
                        });
                    });
                    app = builder.Build();
                    app.Use(async (context, next) =>
                    {
                        var address = context.Connection.RemoteIpAddress;
                        if (address is null || !TransferSafety.IsLocalAddress(address, _options.AllowLoopback))
                        { context.Response.StatusCode = 403; return; }
                        try { await next(context); }
                        catch (Microsoft.AspNetCore.Http.BadHttpRequestException ex) { if (!context.Response.HasStarted) context.Response.StatusCode = ex.StatusCode; }
                        catch (OperationCanceledException) { if (!context.Response.HasStarted) context.Response.StatusCode = 408; }
                        catch (Exception ex) when (ex is IOException or JsonException or InvalidDataException or UnauthorizedAccessException or OverflowException)
                        { if (!context.Response.HasStarted) context.Response.StatusCode = 400; }
                    });
                    app.MapGet("/api/localsend/v2/info", () => Results.Json(Info(), Json));
                    app.MapPost("/api/localsend/v2/register", RegisterAsync);
                    app.MapPost("/api/localsend/v2/prepare-upload", PrepareUploadAsync);
                    app.MapPost("/api/localsend/v2/upload", UploadAsync);
                    app.MapPost("/api/localsend/v2/cancel", CancelIncomingAsync);
                    await app.StartAsync(cancellationToken);
                    var addresses = app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!;
                    Port = new Uri(addresses.Addresses.First()).Port;
                    _server = app;
                    IsListening = true;
                    _lastError = null;
                    break;
                }
                catch (Exception ex) when (ex is IOException or InvalidOperationException or System.Net.Sockets.SocketException)
                {
                    last = ex;
                    if (app is not null) await app.DisposeAsync();
                }
                catch
                {
                    if (app is not null) await app.DisposeAsync();
                    throw;
                }
            }
            if (!IsListening) throw new IOException("无法启动局域网接收服务，请检查端口占用。", last);
            if (_options.EnableDiscovery) StartDiscovery();
            _maintenance = MaintenanceAsync();
            _rateMaintenance = RefreshRateAsync();
            RaiseChanged();
            if (_options.EnableDiscovery) await DiscoverAsync();
        }
        catch (Exception ex)
        {
            _lastError = "局域网接收未启动：" + ex.Message;
            RaiseChanged();
            throw;
        }
        finally { _startLock.Release(); }
    }

    public async Task UpdateAliasAsync(string alias)
    {
        Volatile.Write(ref _alias, TransferSafety.NormalizeAlias(alias));
        RaiseChanged();
        if (!IsListening) return;
        if (_options.EnableDiscovery) await AnnounceAsync(true);
        await Task.WhenAll(Peers.Select(peer => ProbePeerAsync(peer.Address, peer.Port, peer.Fingerprint, _lifetime.Token)));
    }

    public void UpdateReceiveDirectory(string path)
    {
        string fullPath = Path.GetFullPath(path);
        TransferSafety.EnsureNoReparsePoints(fullPath);
        Volatile.Write(ref _receiveDirectory, fullPath);
        RaiseChanged();
    }

    public async Task<LanPeer?> AddPeerAsync(string address, int port = 53317)
    {
        EnsureStarted();
        if (!IPAddress.TryParse(address.Trim(), out var ip) || !TransferSafety.IsLocalAddress(ip, _options.AllowLoopback)
            || port is < 1 or > 65535)
        {
            _lastError = "请输入局域网 IP 地址和有效端口。";
            RaiseChanged();
            return null;
        }
        return await ProbePeerAsync(TransferSafety.NormalizeAddress(ip).ToString(), port, null, _lifetime.Token);
    }

    private async Task RegisterAsync(HttpContext context)
    {
        var info = await ReadJsonAsync<DeviceInfo>(context.Request.Body, TransferSafety.MaximumMetadataBytes, context.RequestAborted);
        string? fingerprint = await ClientFingerprintAsync(context);
        if (info is null || fingerprint is null || !ValidInfo(info) || !string.Equals(info.Fingerprint, fingerprint, StringComparison.OrdinalIgnoreCase))
        { context.Response.StatusCode = 400; return; }
        RememberPeer(info, context.Connection.RemoteIpAddress!, fingerprint, info.Port);
        await context.Response.WriteAsJsonAsync(Info(), Json, context.RequestAborted);
    }

    private async Task<LanPeer?> ProbePeerAsync(string address, int port, string? expectedFingerprint, CancellationToken cancellationToken)
    {
        if (_identity is null) return null;
        if (!IPAddress.TryParse(address, out var ip) || !TransferSafety.IsLocalAddress(ip, _options.AllowLoopback)) return null;
        string? capturedFingerprint = null;
        using var client = CreateClient(expectedFingerprint, captured => capturedFingerprint = captured);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(4));
        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Post, Endpoint(address, port, "register"))
            { Content = JsonContent.Create(Info(), options: Json) };
            using var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, timeout.Token);
            if (!response.IsSuccessStatusCode) return null;
            var info = await ReadResponseJsonAsync<DeviceInfo>(response, timeout.Token);
            if (info is null || !TransferSafety.ValidFingerprint(capturedFingerprint)
                || !string.Equals(info.Fingerprint, capturedFingerprint, StringComparison.OrdinalIgnoreCase)) return null;
            if (info.Fingerprint.Equals(Fingerprint, StringComparison.OrdinalIgnoreCase)) return null;
            RememberPeer(info, ip, capturedFingerprint!, port);
            _lastError = null;
            return _peers.GetValueOrDefault(capturedFingerprint!);
        }
        catch (Exception ex) when (ex is HttpRequestException or OperationCanceledException or IOException or JsonException or AuthenticationException)
        {
            if (expectedFingerprint is null)
            { _lastError = "暂时无法连接该电脑，请确认对方已打开互传面板、端口正确且网络可互访。"; RaiseChanged(); }
            return null;
        }
    }

    private HttpClient CreateClient(string? expectedFingerprint, Action<string>? observed = null)
    {
        var handler = new HttpClientHandler { UseProxy = false, AllowAutoRedirect = false, ClientCertificateOptions = ClientCertificateOption.Manual };
        handler.ClientCertificates.Add(_identity!);
        handler.SslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13;
        handler.ServerCertificateCustomValidationCallback = (_, certificate, _, _) =>
        {
            if (certificate is null || DateTime.UtcNow < certificate.NotBefore.ToUniversalTime()
                || DateTime.UtcNow > certificate.NotAfter.ToUniversalTime()) return false;
            string actual = TransferSafety.CertificateFingerprint(certificate);
            // Only explicit discovery may learn an identity. Transfers always pin a selected peer.
            if (expectedFingerprint is not null && !actual.Equals(expectedFingerprint, StringComparison.OrdinalIgnoreCase)) return false;
            observed?.Invoke(actual);
            return true;
        };
        return new HttpClient(handler) { Timeout = Timeout.InfiniteTimeSpan, DefaultRequestVersion = HttpVersion.Version11, DefaultVersionPolicy = HttpVersionPolicy.RequestVersionExact };
    }

    private static Uri Endpoint(string address, int port, string path) => new UriBuilder("https", address, port, "/api/localsend/v2/" + path).Uri;
    private void EnsureStarted() { if (!IsListening || _disposed) throw new InvalidOperationException("局域网接收服务尚未启动。"); }
    private static bool ValidInfo(DeviceInfo info) => info.Protocol == "https" && info.Port is > 0 and <= 65535 && TransferSafety.ValidFingerprint(info.Fingerprint) && info.Alias?.Length is > 0 and <= 200;
    private static async Task<string?> ClientFingerprintAsync(HttpContext context)
    {
        var certificate = await context.Connection.GetClientCertificateAsync(context.RequestAborted);
        return certificate is null ? null : TransferSafety.CertificateFingerprint(certificate);
    }
    private void RememberPeer(DeviceInfo info, IPAddress address, string fingerprint, int port)
    {
        if (fingerprint.Equals(Fingerprint, StringComparison.OrdinalIgnoreCase) || !TransferSafety.IsLocalAddress(address, _options.AllowLoopback)
            || port is < 1 or > 65535 || !TransferSafety.ValidFingerprint(fingerprint)) return;
        if (_peers.Count >= 256 && !_peers.ContainsKey(fingerprint)) return;
        _peers[fingerprint] = new LanPeer(fingerprint, TransferSafety.NormalizeAlias(info.Alias), TransferSafety.NormalizeAddress(address).ToString(), port, DateTimeOffset.UtcNow)
        { SupportsAppendedSha256 = info.SupportsAppendedSha256 };
        RaiseChanged();
    }

    private static async Task<T?> ReadJsonAsync<T>(Stream stream, int maximumBytes, CancellationToken token)
    {
        using var buffer = new MemoryStream();
        byte[] chunk = new byte[8192];
        while (true)
        {
            int read = await stream.ReadAsync(chunk, token);
            if (read == 0) break;
            if (buffer.Length + read > maximumBytes) throw new InvalidDataException("请求内容过大。");
            buffer.Write(chunk, 0, read);
        }
        return JsonSerializer.Deserialize<T>(buffer.GetBuffer().AsSpan(0, checked((int)buffer.Length)), Json);
    }
    private static async Task<T?> ReadResponseJsonAsync<T>(HttpResponseMessage response, CancellationToken token)
    {
        await using var stream = await response.Content.ReadAsStreamAsync(token);
        return await ReadJsonAsync<T>(stream, TransferSafety.MaximumMetadataBytes, token);
    }

    private void SetStatus(TransferStatus value)
    {
        lock (_statusGate)
        {
            if (value.Phase != TransferPhase.Transferring || _status.Phase != TransferPhase.Transferring
                || value.Direction != _status.Direction || value.BytesTransferred < _status.BytesTransferred)
                _rate.Reset(value.BytesTransferred);
            else _rate.Record(value.BytesTransferred);
            _status = value with { BytesPerSecond = value.Phase == TransferPhase.Transferring ? _rate.GetBytesPerSecond() : 0 };
        }
        RaiseChanged();
    }

    private async Task RefreshRateAsync()
    {
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(250));
        try
        {
            while (await timer.WaitForNextTickAsync(_lifetime.Token))
            {
                bool changed = false;
                lock (_statusGate)
                {
                    if (_status.Phase == TransferPhase.Transferring)
                    {
                        double rate = _rate.GetBytesPerSecond();
                        if (rate != _status.BytesPerSecond)
                        {
                            _status = _status with { BytesPerSecond = rate };
                            changed = true;
                        }
                    }
                }
                // An idle stream still needs an event so the UI clears its last speed.
                if (changed) RaiseChanged();
            }
        }
        catch (OperationCanceledException) { }
    }
    private void RaiseChanged()
    {
        foreach (var handler in Changed?.GetInvocationList() ?? [])
        { try { ((EventHandler)handler)(this, EventArgs.Empty); } catch { /* UI subscribers cannot break network processing. */ } }
    }

    private async Task MaintenanceAsync()
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(30));
        try
        {
            while (await timer.WaitForNextTickAsync(_lifetime.Token))
            {
                bool changed = false;
                foreach (var pair in _peers)
                    if (DateTimeOffset.UtcNow - pair.Value.LastSeen > TimeSpan.FromMinutes(2)) changed |= _peers.TryRemove(pair.Key, out _);
                foreach (var pair in _prepareAttempts)
                    if (DateTimeOffset.UtcNow - pair.Value > TimeSpan.FromMinutes(2)) _prepareAttempts.TryRemove(pair.Key, out _);
                lock (_gate)
                {
                    if (_incoming is { Resume: null } session && DateTimeOffset.UtcNow - session.LastActivity > TimeSpan.FromMinutes(5))
                        FinishIncoming(session, TransferPhase.Cancelled, "传输长时间无响应，已取消。");
                }
                if (_options.EnableDiscovery) await AnnounceAsync(true);
                if (changed) RaiseChanged();
            }
        }
        catch (OperationCanceledException) { }
    }

    public async Task CancelAsync()
    {
        IncomingSession? incoming;
        CancellationTokenSource? outgoing;
        lock (_gate) { incoming = _incoming; outgoing = _outgoing; }
        if (incoming is not null) await StopIncomingAsync(incoming);
        if (outgoing is not null) { try { await outgoing.CancelAsync(); } catch (ObjectDisposedException) { } }
    }

    internal Task StopReceivingAsync(string requestId)
    {
        IncomingSession? incoming;
        lock (_gate) incoming = _incoming?.Id == requestId ? _incoming : null;
        return incoming is null ? Task.CompletedTask : StopIncomingAsync(incoming);
    }

    private async Task StopIncomingAsync(IncomingSession incoming)
    {
        FinishIncoming(incoming, TransferPhase.Cancelled, "已终止接收。");
        await TryCancelRemoteAsync(new(incoming.Fingerprint, incoming.SenderAlias, incoming.Address, incoming.SenderPort, DateTimeOffset.UtcNow), incoming.Id);
    }

    public async ValueTask DisposeAsync()
    {
        await _startLock.WaitAsync();
        try
        {
            if (_disposed) return;
            _disposed = true;
            IsListening = false;
            await CancelAsync();
            await _lifetime.CancelAsync();
            _discoverySocket?.Dispose();
            if (_server is not null)
            {
                using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(4));
                try { await _server.StopAsync(timeout.Token); } catch (OperationCanceledException) { }
                await _server.DisposeAsync();
            }
            if (_maintenance is not null) { try { await _maintenance; } catch (OperationCanceledException) { } }
            if (_rateMaintenance is not null) { try { await _rateMaintenance; } catch (OperationCanceledException) { } }
            if (_discoveryTask is not null) { try { await _discoveryTask; } catch (OperationCanceledException) { } catch (ObjectDisposedException) { } }
            _identity?.Dispose();
            _lifetime.Dispose();
        }
        finally { _startLock.Release(); }
    }
}
