using System.Runtime.InteropServices;
using System.Text;
using LYIsland.Transfer;

namespace LYwintabs.Transfer;

internal sealed class DeviceNickname(string directory)
{
    private readonly Lazy<string> automatic = new(() => Resolve(null, GetDomainDisplayName(), Environment.MachineName));

    public string AutomaticName => automatic.Value;

    public string Read()
    {
        string path = Path.Combine(directory, "nickname.txt");
        TransferSafety.EnsureNoReparsePoints(path);
        if (!File.Exists(path)) return "";
        using var reader = new StreamReader(path);
        // Nicknames are at most 40 UTF-16 code units. Bound reads of externally edited files.
        var buffer = new char[TransferSafety.MaximumAliasLength + 1];
        int length = reader.ReadBlock(buffer, 0, buffer.Length);
        return TransferSafety.CleanAlias(new string(buffer, 0, length));
    }

    public string Save(string value)
    {
        string nickname = TransferSafety.CleanAlias(value);
        string path = Path.Combine(directory, "nickname.txt");
        TransferSafety.EnsureNoReparsePoints(path);
        Directory.CreateDirectory(directory);
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporary, nickname);
            File.Move(temporary, path, overwrite: true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
        return nickname;
    }

    internal static string Resolve(string? custom, string? domainDisplayName, string computerName)
    {
        string nickname = TransferSafety.CleanAlias(custom);
        if (nickname.Length != 0) return nickname;
        string display = TransferSafety.CleanAlias(domainDisplayName);
        return display.Length != 0 ? display : computerName;
    }

    private static string? GetDomainDisplayName()
    {
        const int nameDisplay = 3;
        var text = new StringBuilder(256);
        uint size = (uint)text.Capacity;
        if (GetUserNameExW(nameDisplay, text, ref size)) return text.ToString();
        if (size <= (uint)text.Capacity || size > 32_768) return null;
        text.EnsureCapacity((int)size);
        return GetUserNameExW(nameDisplay, text, ref size) ? text.ToString() : null;
    }

    [DllImport("secur32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.U1)]
    private static extern bool GetUserNameExW(int format, StringBuilder text, ref uint size);
}
