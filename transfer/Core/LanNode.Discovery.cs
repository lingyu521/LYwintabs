using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text.Json;

namespace LYIsland.Transfer;

public sealed partial class LanNode
{
    private const int DiscoveryPort = 53317;
    private static readonly IPAddress DiscoveryGroup = IPAddress.Parse("224.0.0.167");
    private UdpClient? _discoverySocket;
    private Task? _discoveryTask;
    private readonly SemaphoreSlim _probeSlots = new(4, 4);
    private readonly Dictionary<string, DateTimeOffset> _probeCooldown = new();

    public IReadOnlyList<string> GetLocalIPv4Addresses() => LocalInterfaces().Select(ip => ip.ToString()).ToArray();

    private static IPAddress[] LocalInterfaces()
    {
        try
        {
            return NetworkInterface.GetAllNetworkInterfaces()
                .Where(n => n.OperationalStatus == OperationalStatus.Up && n.NetworkInterfaceType != NetworkInterfaceType.Loopback)
                .SelectMany(n => n.GetIPProperties().UnicastAddresses)
                .Select(a => a.Address)
                .Where(a => a.AddressFamily == AddressFamily.InterNetwork && TransferSafety.IsLocalAddress(a, false))
                .Distinct().ToArray();
        }
        catch (NetworkInformationException) { return []; }
    }

    private void StartDiscovery()
    {
        UdpClient? socket = null;
        try
        {
            socket = new UdpClient(AddressFamily.InterNetwork);
            socket.ExclusiveAddressUse = false;
            socket.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
            socket.Client.Bind(new IPEndPoint(IPAddress.Any, DiscoveryPort));
            var interfaces = LocalInterfaces();
            int joined = 0;
            foreach (var address in interfaces)
            {
                try { socket.JoinMulticastGroup(DiscoveryGroup, address); joined++; }
                catch (SocketException) { }
            }
            if (joined == 0) socket.JoinMulticastGroup(DiscoveryGroup);
            _discoverySocket = socket;
            _discoveryTask = ReadAnnouncementsAsync(socket);
        }
        catch (Exception ex) when (ex is SocketException or NetworkInformationException)
        {
            socket?.Dispose();
            _discoverySocket = null;
            _lastError = "自动发现暂不可用，可通过局域网 IP 地址连接。";
        }
    }

    public async Task DiscoverAsync()
    {
        EnsureStarted();
        if (_options.EnableDiscovery) await AnnounceAsync(true);
        await Task.WhenAll(Peers.Select(peer => ProbePeerAsync(peer.Address, peer.Port, peer.Fingerprint, _lifetime.Token)));
        RaiseChanged();
    }

    private async Task AnnounceAsync(bool announce)
    {
        if (!IsListening || _lifetime.IsCancellationRequested) return;
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(Info(announce), Json);
        var interfaces = LocalInterfaces();
        if (interfaces.Length == 0) interfaces = [IPAddress.Any];
        foreach (var address in interfaces)
        {
            try
            {
                using var client = new UdpClient(new IPEndPoint(address, 0));
                client.MulticastLoopback = false;
                client.Ttl = 1;
                if (!address.Equals(IPAddress.Any))
                    client.Client.SetSocketOption(SocketOptionLevel.IP, SocketOptionName.MulticastInterface, address.GetAddressBytes());
                await client.SendAsync(bytes, new IPEndPoint(DiscoveryGroup, DiscoveryPort), _lifetime.Token);
            }
            catch (Exception ex) when (ex is SocketException or OperationCanceledException or ObjectDisposedException) { }
        }
    }

    private async Task ReadAnnouncementsAsync(UdpClient socket)
    {
        try
        {
            while (!_lifetime.IsCancellationRequested)
            {
                var packet = await socket.ReceiveAsync(_lifetime.Token);
                if (packet.Buffer.Length > 8192 || !TransferSafety.IsLocalAddress(packet.RemoteEndPoint.Address, _options.AllowLoopback)) continue;
                DeviceInfo? info;
                try { info = JsonSerializer.Deserialize<DeviceInfo>(packet.Buffer, Json); }
                catch (JsonException) { continue; }
                if (info is null || !ValidInfo(info) || info.Fingerprint.Equals(Fingerprint, StringComparison.OrdinalIgnoreCase)) continue;
                string source = TransferSafety.NormalizeAddress(packet.RemoteEndPoint.Address).ToString();
                string key = source + "|" + info.Fingerprint + "|" + info.Alias;
                bool schedule;
                lock (_probeCooldown)
                {
                    var now = DateTimeOffset.UtcNow;
                    schedule = !_probeCooldown.TryGetValue(key, out var last) || now - last >= TimeSpan.FromSeconds(3);
                    if (_probeCooldown.Count > 1024) _probeCooldown.Clear();
                    if (schedule) _probeCooldown[key] = now;
                }
                if (schedule && await _probeSlots.WaitAsync(0))
                {
                    // UDP identity is untrusted until the HTTPS handshake pins the advertised certificate.
                    _ = ProbeAnnouncementAsync(source, info);
                }
            }
        }
        catch (Exception ex) when (ex is OperationCanceledException or ObjectDisposedException) { }
        catch (SocketException)
        { if (!_disposed) { _lastError = "自动发现已中断，可手动刷新或通过 IP 地址连接。"; RaiseChanged(); } }
    }

    private async Task ProbeAnnouncementAsync(string address, DeviceInfo info)
    {
        try
        {
            var peer = await ProbePeerAsync(address, info.Port, info.Fingerprint, _lifetime.Token);
            if (peer is not null && info.Announce == true) await AnnounceAsync(false);
        }
        catch (Exception ex) when (ex is ObjectDisposedException or InvalidOperationException or System.Security.Cryptography.CryptographicException) { }
        finally { _probeSlots.Release(); }
    }
}
