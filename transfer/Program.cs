using System.Security.Cryptography;
using System.Text;
using System.Diagnostics;
using System.Runtime.InteropServices;
using LYIsland.Transfer;

namespace LYwintabs.Transfer;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        string installation = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(
            Path.GetFullPath(AppContext.BaseDirectory).ToUpperInvariant())));
        string name = $@"Local\LYwintabs.Transfer.{installation}";
        using var owner = new Mutex(false, name + ".Owner");
        using var show = new EventWaitHandle(false, EventResetMode.AutoReset, name + ".Show");
        using var close = new EventWaitHandle(false, EventResetMode.AutoReset, name + ".Close");
        bool stopping = args is ["--close-if-idle"];
        bool background = args is ["--background"];
        if (args.Length != 0 && !stopping && !background) return 87;
        if (!stopping && !Environment.UserInteractive) return 1459;
        if (!TakeOwnership(owner, 0))
        {
            if (!stopping)
            {
                if (!background) show.Set();
                return 0;
            }
            close.Set();
            if (!TakeOwnership(owner, 7000)) return 170;
        }
        try
        {
            if (stopping) return 0;
            using (var process = Process.GetCurrentProcess())
            {
                try { process.PriorityClass = ProcessPriorityClass.BelowNormal; }
                catch (System.ComponentModel.Win32Exception) { }
            }
            ApplicationConfiguration.Initialize();
            string data = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "LYwintabs", "Transfer");
            var node = new LanNode(new LanNodeOptions(data, DefaultReceiveDirectory(), Environment.MachineName));
            using var panel = new TransferPanel(node, new DeviceNickname(data), show, close);
            using var context = new ApplicationContext();
            using var trayMenu = new ContextMenuStrip();
            using var trayIcon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            using var tray = new NotifyIcon { Icon = trayIcon ?? SystemIcons.Application, Text = "LYwintabs 局域网互传", ContextMenuStrip = trayMenu };
            trayMenu.Items.Add("打开互传", null, (_, _) => panel.ShowPanel());
            var settings = trayMenu.Items.Add("设置");
            settings.Click += async (_, _) =>
            {
                settings.Enabled = false;
                try
                {
                    string launcher = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "LYwintabs.Launcher.exe"));
                    using var dialog = Process.Start(new ProcessStartInfo(launcher, "--settings") { UseShellExecute = false });
                    if (dialog is null) throw new InvalidOperationException("无法启动设置窗口。");
                    _ = AllowSetForegroundWindow(dialog.Id);
                    await dialog.WaitForExitAsync();
                    if (dialog.ExitCode != 0) throw new InvalidOperationException($"无法打开设置（{dialog.ExitCode}）。");
                }
                catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
                {
                    MessageBox.Show(ex.Message, "LYwintabs", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
                finally { settings.Enabled = true; }
            };
            trayMenu.Items.Add("退出互传", null, (_, _) => panel.RequestExit());
            tray.DoubleClick += (_, _) => panel.ShowPanel();
            panel.FormClosed += (_, _) => context.ExitThread();
            tray.Visible = true;
            _ = panel.Handle;
            panel.BeginInvoke(async () =>
            {
                if (!background) panel.ShowPanel();
                await panel.StartListeningAsync();
            });
            Application.Run(context);
            return 0;
        }
        catch (Exception ex)
        {
            if (background) Debug.WriteLine(ex);
            else MessageBox.Show("无法打开局域网互传。\n" + ex.Message, "LYwintabs", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
        finally
        {
            owner.ReleaseMutex();
        }
    }

    [DllImport("user32.dll", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool AllowSetForegroundWindow(int processId);

    private static bool TakeOwnership(Mutex owner, int milliseconds)
    {
        try { return owner.WaitOne(milliseconds); }
        catch (AbandonedMutexException) { return true; }
    }

    internal static string DefaultReceiveDirectory()
    {
        string desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory, Environment.SpecialFolderOption.DoNotVerify);
        return string.IsNullOrEmpty(desktop)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Desktop")
            : desktop;
    }
}
