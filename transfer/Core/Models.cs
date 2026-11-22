using System.Text.Json.Serialization;

namespace LYIsland.Transfer;

public sealed record LanNodeOptions(string DataDirectory, string ReceiveDirectory, string Alias,
    int Port = 53317, bool EnableDiscovery = true, bool AllowLoopback = false);

public sealed record LanPeer(string Fingerprint, string Alias, string Address, int Port, DateTimeOffset LastSeen)
{
    internal bool SupportsAppendedSha256 { get; init; }
    public string DisplayLabel => Alias;
    public string AddressLabel => $"{Address}:{Port}";
}

public enum TransferPhase { Idle, WaitingForAcceptance, AwaitingDecision, Transferring, Completed, Rejected, Cancelled, Failed, Paused }

public sealed record TransferStatus(string Direction, TransferPhase Phase, string PeerAlias, string FileName,
    long BytesTransferred, long TotalBytes, int FileCount, int CompletedFiles, string? Message, string? SavedDirectory)
{
    /// <summary>Smoothed payload throughput; zero while waiting, stalled, or finished.</summary>
    public double BytesPerSecond { get; init; }
    public static TransferStatus Idle { get; } = new("", TransferPhase.Idle, "", "", 0, 0, 0, 0, null, null);
}

public sealed record IncomingTransfer(string RequestId, string SenderAlias, string SenderAddress,
    long TotalBytes, IReadOnlyList<TransferFileOffer> Files);
public sealed record TransferFileOffer(string Name, long Size);

internal sealed record DeviceInfo
{
    public string Alias { get; init; } = "";
    public string Version { get; init; } = "2.2";
    public string DeviceModel { get; init; } = "LYwintabs / Windows";
    public string DeviceType { get; init; } = "desktop";
    public string Fingerprint { get; init; } = "";
    public int Port { get; init; }
    public string Protocol { get; init; } = "https";
    public bool Download { get; init; }
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public bool SupportsAppendedSha256 { get; init; }
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public bool? Announce { get; init; }
}

internal sealed record FileInfoDto
{
    public string Id { get; init; } = "";
    public string FileName { get; init; } = "";
    public long Size { get; init; }
    public string FileType { get; init; } = "application/octet-stream";
    public string? Sha256 { get; init; }
}

internal sealed record PrepareRequest(DeviceInfo Info, Dictionary<string, FileInfoDto> Files,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)] bool AppendSha256 = false);
internal sealed record PrepareResponse(string SessionId, Dictionary<string, string> Files);
