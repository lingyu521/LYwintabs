using LYIsland.Transfer;
using static LYwintabs.Transfer.TransferLanguage;

namespace LYwintabs.Transfer;

internal sealed class IncomingPrompt : Form
{
    internal event Action<bool>? Answer;
    internal event Action? SaveAsRequested;
    internal event Action<bool>? PauseRequested;
    internal event Action? StopRequested;
    private readonly Label destination;
    private readonly Label fileDetail;
    private readonly Label sender;
    private readonly Label summary;
    private readonly IncomingTransfer request;
    private readonly string total;
    private readonly Action translateControls;
    private readonly Button accept, saveAs, reject, pause, stop;
    private readonly ProgressBar progress = new() { Dock = DockStyle.Top, Maximum = 1000, Height = 7, Visible = false };
    internal bool IsReceiving { get; private set; }
    private bool paused;
    private string receiveDirectory;

    public IncomingPrompt(IncomingTransfer request, string receiveDirectory, string total, TransferColors colors, Font font)
    {
        this.request = request;
        this.total = total;
        this.receiveDirectory = receiveDirectory;
        Text = "LYwintabs · 接收文件";
        Font = font;
        BackColor = colors.Surface;
        ForeColor = colors.Text;
        ClientSize = new Size(438, 181);
        AutoScaleMode = AutoScaleMode.Dpi;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        TopMost = true;
        StartPosition = FormStartPosition.Manual;
        var body = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 6, Padding = new Padding(18, 12, 18, 10) };
        body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        body.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        body.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        body.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        body.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        body.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        body.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        sender = Line("", colors.Text);
        summary = Line("", colors.Text);
        body.Controls.Add(sender, 0, 0);
        body.Controls.Add(summary, 0, 1);
        var file = request.Files.FirstOrDefault();
        fileDetail = Line("", colors.Muted);
        body.Controls.Add(fileDetail, 0, 2);
        destination = Line("", colors.Muted);
        body.Controls.Add(destination, 0, 3);
        body.Controls.Add(progress, 0, 4);

        reject = CreateAction("拒绝", "RejectOffer", TransferGlyph.None, colors.Text);
        saveAs = CreateAction("另存为…", "SaveAsOffer", TransferGlyph.Folder, colors.Text);
        accept = CreateAction("接收", "AcceptOffer", TransferGlyph.Receive, colors.Accent);
        pause = CreateAction("暂停", "ReceivePause", TransferGlyph.None, colors.Accent);
        stop = CreateAction("终止", "ReceiveStop", TransferGlyph.None, colors.Text);
        pause.Visible = stop.Visible = false;
        reject.Click += (_, _) => Answer?.Invoke(false);
        saveAs.Click += (_, _) => SaveAsRequested?.Invoke();
        accept.Click += (_, _) => Answer?.Invoke(true);
        pause.Click += (_, _) => PauseRequested?.Invoke(!paused);
        stop.Click += (_, _) => { stop.Enabled = false; PauseRequested = null; StopRequested?.Invoke(); };
        var actions = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill, AutoSize = true, FlowDirection = FlowDirection.RightToLeft,
            WrapContents = false, Margin = Padding.Empty
        };
        actions.Controls.AddRange([accept, saveAs, reject, pause, stop]);
        body.Controls.Add(actions, 0, 5);
        Controls.Add(body);
        CancelButton = reject;
        translateControls = Bind(this);
        fileDetail.Text = file?.Name ?? "";
        RefreshLanguage();

        TransferAction CreateAction(string label, string name, TransferGlyph glyph, Color ink) => new(label, glyph)
        {
            Name = name, BackColor = colors.Surface, ForeColor = ink, Accent = colors.Accent, DisabledInk = colors.Disabled
        };
    }

    // The notification never redirects keystrokes from the user's current application.
    protected override bool ShowWithoutActivation => true;

    protected override void OnLoad(EventArgs e)
    {
        base.OnLoad(e);
        var area = Screen.FromPoint(Cursor.Position).WorkingArea;
        Location = new Point(Math.Max(area.Left, area.Right - Width - 16),
            Math.Max(area.Top, area.Bottom - Height - 16));
    }

    protected override void WndProc(ref Message message)
    {
        const int mouseActivate = 0x0021, activateAndDeliverClick = 1;
        if (message.Msg == mouseActivate)
        {
            message.Result = new IntPtr(activateAndDeliverClick);
            return;
        }
        base.WndProc(ref message);
    }

    internal void BeginReceiving(string directory)
    {
        IsReceiving = true;
        SetReceiveDirectory(directory);
        CancelButton = null;
        accept.Visible = saveAs.Visible = reject.Visible = false;
        progress.Visible = pause.Visible = stop.Visible = true;
        pause.Enabled = false;
        fileDetail.Text = Translate("正在准备接收…");
    }

    internal void UpdateReceiving(string detail, long bytes, long total, bool isPaused)
    {
        paused = isPaused;
        fileDetail.Text = detail;
        progress.Value = total == 0 ? 0 : (int)Math.Clamp(bytes * 1000d / total, 0, 1000);
        pause.Text = Translate(paused ? "继续" : "暂停");
        pause.Enabled = stop.Enabled;
    }

    internal void SetReceiveDirectory(string directory)
    {
        receiveDirectory = directory;
        destination.Text = $"{Translate("保存到")} {directory}";
    }

    internal void RefreshLanguage()
    {
        translateControls();
        sender.Text = $"{Translate("来自")} {request.SenderAlias} · {request.SenderAddress}";
        summary.Text = English
            ? $"Incoming files: {request.Files.Count} ({total})"
            : $"请求发送 {request.Files.Count} 个文件（{total}）";
        SetReceiveDirectory(receiveDirectory);
        pause.Text = Translate(paused ? "继续" : "暂停");
    }

    private static Label Line(string text, Color ink) => new()
    {
        Text = text, ForeColor = ink, Dock = DockStyle.Top, AutoEllipsis = true,
        Height = 22, Margin = new Padding(0, 0, 0, 2)
    };
}
