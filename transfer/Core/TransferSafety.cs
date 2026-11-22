using System.Net;
using System.Security.AccessControl;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Security.Principal;

namespace LYIsland.Transfer;

internal static class TransferSafety
{
    internal const int MaximumFiles = 1000;
    internal const int MaximumMetadataBytes = 1024 * 1024;
    internal const long MaximumFileBytes = 4L * 1024 * 1024 * 1024 * 1024;
    internal const int MaximumAliasLength = 40;

    internal static string CleanAlias(string? alias)
    {
        string value = new((alias ?? "").Where(c => !char.IsControl(c)).ToArray());
        value = value.Trim();
        if (value.Length > MaximumAliasLength)
        {
            var end = MaximumAliasLength;
            if (char.IsHighSurrogate(value[end - 1])) end--;
            value = value[..end].TrimEnd();
        }
        return value;
    }

    internal static string NormalizeAlias(string? alias)
    {
        string value = CleanAlias(alias);
        return value.Length == 0 ? Environment.MachineName : value;
    }

    internal static IPAddress NormalizeAddress(IPAddress address) => address.IsIPv4MappedToIPv6 ? address.MapToIPv4() : address;

    internal static bool IsLocalAddress(IPAddress address, bool allowLoopback)
    {
        address = NormalizeAddress(address);
        if (IPAddress.IsLoopback(address)) return allowLoopback;
        byte[] b = address.GetAddressBytes();
        if (b.Length == 4)
            return b[0] == 10 || b[0] == 192 && b[1] == 168 || b[0] == 172 && b[1] is >= 16 and <= 31
                || b[0] == 169 && b[1] == 254;
        return address.IsIPv6LinkLocal || (b[0] & 0xFE) == 0xFC;
    }

    internal static bool ValidFingerprint(string? value) => value?.Length == 64 && value.All(Uri.IsHexDigit);
    internal static string CertificateFingerprint(X509Certificate certificate) => Convert.ToHexString(SHA256.HashData(certificate.GetRawCertData()));

    internal static X509Certificate2 LoadIdentity(string dataDirectory)
    {
        EnsureNoReparsePoints(dataDirectory);
        Directory.CreateDirectory(dataDirectory);
        string path = Path.Combine(dataDirectory, "identity.pfx");
        if (File.Exists(path)) EnsureNoReparsePoints(path);
        if (!File.Exists(path))
        {
            using RSA rsa = RSA.Create(2048);
            var request = new CertificateRequest("CN=LYwintabs", rsa, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
            request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, true));
            request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment, true));
            request.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(new OidCollection
            { new("1.3.6.1.5.5.7.3.1"), new("1.3.6.1.5.5.7.3.2") }, false));
            var san = new SubjectAlternativeNameBuilder(); san.AddDnsName("localhost"); san.AddIpAddress(IPAddress.Loopback);
            request.CertificateExtensions.Add(san.Build());
            using var generated = request.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddYears(5));
            string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                File.WriteAllBytes(temporary, generated.Export(X509ContentType.Pfx));
                ProtectPrivateFile(temporary);
                File.Move(temporary, path, overwrite: false);
            }
            finally { if (File.Exists(temporary)) File.Delete(temporary); }
        }
        // Windows Schannel needs a persisted user key. No certificate is added to a certificate store.
        return X509CertificateLoader.LoadPkcs12FromFile(path, null,
            X509KeyStorageFlags.UserKeySet | X509KeyStorageFlags.PersistKeySet | X509KeyStorageFlags.Exportable);
    }

    private static void ProtectPrivateFile(string path)
    {
        if (OperatingSystem.IsWindows())
        {
            var sid = WindowsIdentity.GetCurrent().User ?? throw new IOException("Cannot resolve current user.");
            var security = new FileSecurity();
            security.SetAccessRuleProtection(true, false);
            security.AddAccessRule(new FileSystemAccessRule(sid, FileSystemRights.FullControl, AccessControlType.Allow));
            new FileInfo(path).SetAccessControl(security);
        }
        else File.SetUnixFileMode(path, UnixFileMode.UserRead | UnixFileMode.UserWrite);
    }

    internal static string SafeRelativePath(string value)
    {
        if (string.IsNullOrWhiteSpace(value) || value.Length > 500 || Path.IsPathRooted(value)
            || value.StartsWith('/') || value.StartsWith('\\')) throw new InvalidDataException("文件名无效。");
        string[] parts = value.Replace('\\', '/').Split('/');
        foreach (string part in parts)
        {
            if (part is "" or "." or ".." || part.Length > 200 || part.EndsWith(' ') || part.EndsWith('.')
                || part.Any(c => c < 32 || "<>:\"|?*".Contains(c))) throw new InvalidDataException("文件路径包含不安全字符。");
            string stem = part.Split('.')[0].TrimEnd(' ').ToUpperInvariant();
            if (stem is "CON" or "PRN" or "AUX" or "NUL" or "CONIN$" or "CONOUT$"
                || stem.Length == 4 && (stem.StartsWith("COM") || stem.StartsWith("LPT")) && (stem[3] is >= '0' and <= '9' or '¹' or '²' or '³'))
                throw new InvalidDataException("不支持系统保留文件名。");
        }
        return Path.Combine(parts);
    }

    internal static string ContainedPath(string root, string relative)
    {
        string fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        string result = Path.GetFullPath(Path.Combine(fullRoot, SafeRelativePath(relative)));
        if (!result.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("文件路径超出接收目录。");
        return result;
    }

    internal static void EnsureNoReparsePoints(string path)
    {
        string? current = Path.GetFullPath(path);
        while (!string.IsNullOrEmpty(current))
        {
            if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("接收目录或文件不能是符号链接/重解析点。");
            current = Path.GetDirectoryName(current);
        }
    }
}
