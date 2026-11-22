using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace LYwintabs.Transfer;

internal static class TransferLanguage
{
    private const string SettingsKey = @"Software\LYwintabs\Explorer";
    private const string LanguageValue = "UiLanguage";

    internal static bool English { get; private set; } = ReadEnglish();

    internal static bool Refresh()
    {
        bool english = ReadEnglish();
        if (english == English) return false;
        English = english;
        return true;
    }

    private static bool ReadEnglish()
    {
        try
        {
            using var settings = Registry.CurrentUser.OpenSubKey(SettingsKey);
            if (settings?.GetValue(LanguageValue) is int value and (0 or 1)) return value == 1;
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or System.Security.SecurityException or IOException)
        {
            // Use the Windows UI language if the preference cannot be read.
        }
        const int chinese = 0x04, primaryLanguageMask = 0x03ff;
        return (GetUserDefaultUILanguage() & primaryLanguageMask) != chinese;
    }

    [DllImport("kernel32.dll")]
    private static extern ushort GetUserDefaultUILanguage();

    internal static string Translate(string text)
    {
        if (!English) return text;
        if (Translations.TryGetValue(text, out string? translation)) return translation;
        foreach (var (prefix, english) in DetailPrefixes)
            if (text.StartsWith(prefix, StringComparison.Ordinal)) return english + Translate(text[prefix.Length..]);
        return text;
    }

    internal static Action Bind(Control root)
    {
        var updates = new List<Action>();
        AddControl(root);
        return () =>
        {
            root.SuspendLayout();
            try
            {
                foreach (var update in updates) update();
            }
            finally { root.ResumeLayout(true); }
        };

        void AddControl(Control control)
        {
            if (control is Form or Label or Button)
                AddText(control.Text, translated => control.Text = translated);
            AddText(control.AccessibleName, translated => control.AccessibleName = translated);
            if (control is TextBox box)
                AddText(box.PlaceholderText, translated => box.PlaceholderText = translated);
            foreach (Control child in control.Controls) AddControl(child);
        }

        void AddText(string? chinese, Action<string> update)
        {
            if (chinese is not null && Translations.TryGetValue(chinese, out string? english))
                updates.Add(() => update(English ? english : chinese));
        }
    }

    private static readonly (string Chinese, string English)[] DetailPrefixes =
    [
        ("无法读取已保存的昵称。 ", "Could not read the saved nickname. "),
        ("无法创建接收目录：", "Could not create the receive folder: "),
        ("接收失败：", "Receive failed: "),
        ("发送失败：", "Send failed: "),
        ("局域网接收未启动：", "The receive service could not start: "),
    ];

    private static readonly Dictionary<string, string> Translations = new(StringComparer.Ordinal)
    {
        ["局域网互传 · LYwintabs"] = "LAN File Transfer · LYwintabs",
        ["LYwintabs 局域网互传"] = "LYwintabs LAN File Transfer",
        ["局域网互传"] = "LAN File Transfer",
        ["打开互传"] = "Open File Transfer",
        ["退出互传"] = "Exit File Transfer",
        ["设置"] = "Settings",
        ["对方网络地址"] = "Device address",
        ["对方 IP 或 IP:端口"] = "Device IP or IP:port",
        ["搜索附近设备"] = "Search nearby devices",
        ["搜索设备名称或 IP"] = "Search by name or IP address",
        ["设备昵称"] = "Device nickname",
        ["留空时自动命名"] = "Leave blank to name automatically",
        ["保存昵称"] = "Save nickname",
        ["接收"] = "Receive",
        ["发送"] = "Send",
        ["附近设备"] = "Nearby devices",
        ["待发送文件"] = "Files to send",
        ["待接收文件"] = "Incoming files",
        ["连接信息"] = "Connection details",
        ["收起连接信息"] = "Hide connection details",
        ["连接"] = "Connect",
        ["刷新设备"] = "Refresh",
        ["添加文件"] = "Add files",
        ["添加文件夹"] = "Add folder",
        ["移除"] = "Remove",
        ["拒绝"] = "Decline",
        ["取消传输"] = "Cancel transfer",
        ["暂停"] = "Pause",
        ["继续"] = "Resume",
        ["终止"] = "Stop",
        ["更改…"] = "Change…",
        ["另存为…"] = "Save as…",
        ["打开接收目录"] = "Open received files",
        ["将文件或文件夹拖到这里"] = "Drop files or folders here",
        ["在另一台设备上打开局域网互传"] = "Open File Transfer on another device",
        ["按地址连接"] = "Connect by IP",
        ["接收文件"] = "Receive files",
        ["收到传送请求"] = "Incoming transfer",
        ["发送文件"] = "Send files",
        ["留空时使用域账户显示名称或计算机名。"] = "Leave blank to use your domain name or computer name.",
        ["接收位置"] = "Save received files to",
        ["互传设置"] = "Transfer settings",
        ["选择要发送的文件"] = "Choose files to send",
        ["选择要发送的文件夹"] = "Choose a folder to send",
        ["正在启动互传，请稍候。"] = "File Transfer is starting. Please wait.",
        ["请先完成、拒绝或取消当前传输，再退出互传。"] = "Finish, decline, or cancel the current transfer before exiting.",
        ["当前页面"] = "Current page",
        ["请输入对方的局域网 IP，例如 192.168.1.25 或 192.168.1.25:53318。"] = "Enter the device's LAN IP, such as 192.168.1.25 or 192.168.1.25:53318.",
        ["无法连接。请确认对方启用了局域网互传，并允许专用网络访问。"] = "Could not connect. Make sure File Transfer is running and private network access is allowed on the other device.",
        ["无法使用此接收位置"] = "Cannot use this save location",
        ["选择接收文件的保存位置"] = "Choose where to save received files",
        ["来自"] = "From",
        ["没有符合搜索条件的设备"] = "No devices match your search",
        ["请在另一台设备上启用局域网互传"] = "Enable File Transfer on the other device",
        ["等待接收"] = "Ready to receive",
        ["暂无"] = "None",
        ["端口"] = "Port",
        ["正在启用局域网互传…"] = "Starting File Transfer…",
        ["局域网互传未启动"] = "File Transfer is offline",
        ["终止接收"] = "Stop receiving",
        ["正在停止局域网接收…"] = "Stopping the receive service…",
        ["LYwintabs · 接收文件"] = "LYwintabs · Receive Files",
        ["正在准备接收…"] = "Preparing to receive…",
        ["保存到"] = "Save to",
        ["无法启动设置窗口。"] = "Could not open the settings window.",
        ["无法打开局域网互传。"] = "Could not open LAN File Transfer.",
        ["接收已暂停。"] = "Receiving paused.",
        ["正在接收…"] = "Receiving…",
        ["收到文件请求，等待你确认。"] = "Incoming transfer awaiting your confirmation.",
        ["已拒绝接收。"] = "Transfer declined.",
        ["接收目录已存在，请重试。"] = "The receive folder already exists. Try again.",
        ["准备接收文件…"] = "Preparing to receive files…",
        ["接收请求已取消或超时。"] = "The incoming request was cancelled or timed out.",
        ["接收路径无效。"] = "The save path is invalid.",
        ["接收磁盘剩余空间不足。"] = "There is not enough disk space to receive these files.",
        ["文件大小与声明不符。"] = "The file size does not match the transfer offer.",
        ["收到的数据超过声明大小。"] = "Received more data than the transfer offer specified.",
        ["文件数据不完整。"] = "The file is incomplete.",
        ["文件校验失败，未保存损坏文件。"] = "File verification failed. The damaged file was discarded.",
        ["文件已接收。"] = "Files received.",
        ["继续接收…"] = "Resuming receive…",
        ["接收已取消，未完成文件已移除。"] = "Receive cancelled. Incomplete files were removed.",
        ["发送方取消了传输。"] = "The sender cancelled the transfer.",
        ["文件名无效。"] = "The file name is invalid.",
        ["文件路径包含不安全字符。"] = "The file path contains unsupported characters.",
        ["不支持系统保留文件名。"] = "System-reserved file names are not supported.",
        ["文件路径超出接收目录。"] = "The file path points outside the receive folder.",
        ["接收目录或文件不能是符号链接/重解析点。"] = "The receive folder and files cannot be symbolic links or reparse points.",
        ["自动发现暂不可用，可通过局域网 IP 地址连接。"] = "Automatic discovery is unavailable. Connect by LAN IP address.",
        ["自动发现已中断，可手动刷新或通过 IP 地址连接。"] = "Automatic discovery stopped. Refresh the list or connect by IP address.",
        ["无法启动局域网接收服务，请检查端口占用。"] = "Could not start the receive service. Check whether its port is in use.",
        ["请输入局域网 IP 地址和有效端口。"] = "Enter a LAN IP address and a valid port.",
        ["暂时无法连接该电脑，请确认对方已打开互传面板、端口正确且网络可互访。"] = "Could not reach this device. Check its File Transfer service, port, and network connection.",
        ["局域网接收服务尚未启动。"] = "The receive service is not running.",
        ["请求内容过大。"] = "The request is too large.",
        ["传输长时间无响应，已取消。"] = "The transfer was cancelled because it stopped responding.",
        ["已终止接收。"] = "Receive stopped.",
        ["已有传输正在进行，请先完成或取消。"] = "Finish or cancel the transfer already in progress.",
        ["正在准备文件…"] = "Preparing files…",
        ["接收方地址或身份无效。"] = "The recipient address or identity is invalid.",
        ["文件信息过多，请分批发送。"] = "There are too many file details. Send in smaller batches.",
        ["等待对方确认接收…"] = "Waiting for the recipient to accept…",
        ["对方拒绝接收或确认已超时。"] = "The recipient declined or did not answer in time.",
        ["对方正在传输其他文件，请稍后重试。"] = "The recipient is busy with another transfer. Try again later.",
        ["对方启用了 PIN 验证，请关闭对方 PIN 后重试。"] = "The recipient requires a PIN. Turn off PIN verification on the receiving device and try again.",
        ["对方未选择接收文件。"] = "The recipient did not select any files to receive.",
        ["接收方返回了无效的传输信息。"] = "The recipient returned invalid transfer information.",
        ["发送前文件已发生变化，请重新选择。"] = "The file changed before sending. Select it again.",
        ["正在发送…"] = "Sending…",
        ["文件校验不一致，接收方已丢弃未完成文件。"] = "File verification failed. The recipient discarded the incomplete file.",
        ["发送完成。"] = "Transfer complete.",
        ["已取消发送。"] = "Send cancelled.",
        ["等待接收方响应超时。"] = "The recipient did not respond in time.",
        ["请选择 1–1000 个文件。"] = "Select 1 to 1,000 files.",
        ["存在同名文件，请分批发送。"] = "Files have duplicate names. Split them into separate batches.",
        ["每次最多发送 1000 个文件，请分批发送。"] = "Send up to 1,000 files per batch.",
        ["所选文件已不存在。"] = "The selected file no longer exists.",
        ["所选目录中没有可发送的文件。"] = "The selected folder contains no files to send.",
        ["文件总量超过单次传输限制。"] = "The total size exceeds the limit for one transfer.",
        ["准备发送时文件已发生变化。"] = "The file changed while preparing the transfer.",
        ["文件已发生变化。"] = "The file changed during the transfer.",
    };
}
