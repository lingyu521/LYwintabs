using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Net;
using System.Runtime.InteropServices;
using LYIsland.Transfer;

namespace LYwintabs.Transfer;

internal sealed class TransferPanel : Form
{
    private const string PanelProperty = "LYwintabs.Transfer.Panel";
    private static readonly uint activateMessage = RegisterWindowMessage("LYwintabs.Transfer.Activate");
    private readonly LanNode node;
    private readonly DeviceNickname nicknameStore;
    private readonly EventWaitHandle showRequest;
    private readonly EventWaitHandle closeRequest;
    private readonly System.Windows.Forms.Timer refresh = new() { Interval = 250 };
    private readonly TextBox address = new() { AccessibleName = "对方网络地址", PlaceholderText = "对方 IP 或 IP:端口", Width = 198 };
    private readonly TextBox deviceSearch = new() { AccessibleName = "搜索附近设备", PlaceholderText = "搜索设备名称或 IP", Dock = DockStyle.Top, Margin = new Padding(0, 1, 0, 4) };
    private readonly TextBox nickname = new() { AccessibleName = "设备昵称", PlaceholderText = "留空时自动命名", Width = 211, MaxLength = TransferSafety.MaximumAliasLength };
    private readonly Button saveNickname = Action("保存昵称");
    private readonly Panel receivePage = Page("ReceivePage");
    private readonly Panel sendPage = Page("SendPage");
    private readonly Panel settingsPage = Page("SettingsPage");
    private readonly Panel pageHost = new() { Dock = DockStyle.Fill };
    private readonly TransferAction receiveNav = Navigation("接收", "ReceiveNav", TransferGlyph.Receive);
    private readonly TransferAction sendNav = Navigation("发送", "SendNav", TransferGlyph.Send);
    private readonly TransferAction settingsNav = Navigation("设置", "SettingsNav", TransferGlyph.Settings);
    private readonly Label advertisedName = new() { AutoSize = true, Margin = new Padding(0, 12, 0, 8) };
    private readonly TransferList devices = new("附近设备");
    private readonly TransferList files = new("待发送文件");
    private readonly TransferList offered = new("待接收文件", twoLines: true);
    private readonly Label incomingTitle = new() { AutoSize = true };
    private readonly Label listening = new() { AutoSize = true };
    private readonly Label network = new() { AutoSize = true, Visible = false };
    private readonly Button networkToggle = Action("连接信息", TransferGlyph.Link);
    private readonly Label destination = new() { AutoSize = true, MaximumSize = new Size(470, 0) };
    private readonly Label status = new() { Dock = DockStyle.Fill, AutoEllipsis = true, TextAlign = ContentAlignment.MiddleLeft };
    private readonly ProgressBar progress = new() { Dock = DockStyle.Fill, Maximum = 1000, Height = 8 };
    private readonly Button connect = Action("连接");
    private readonly Button discover = Action("刷新设备", TransferGlyph.Refresh);
    private readonly Button send = Action("发送", TransferGlyph.Send);
    private readonly Button addFiles = Action("添加文件", TransferGlyph.Plus);
    private readonly Button addFolder = Action("添加文件夹", TransferGlyph.Folder);
    private readonly Button remove = Action("移除");
    private readonly Button accept = Action("接收", TransferGlyph.Receive);
    private readonly Button reject = Action("拒绝");
    private readonly Button cancel = Action("取消传输");
    private readonly Button pauseReceive = Action("暂停");
    private readonly Button chooseDestination = Action("更改…");
    private readonly Button openSaved = Action("打开接收目录", TransferGlyph.Folder);
    private readonly Label fileCount = new() { AutoSize = true, Margin = new Padding(0, 6, 12, 4) };
    private readonly Label deviceCount = new() { AutoSize = true, Margin = new Padding(0, 6, 12, 4) };
    private readonly Label dropHint = new() { Text = "将文件或文件夹拖到这里", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter };
    private readonly Label deviceHint = new() { Text = "在另一台设备上打开局域网互传", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter };
    private readonly Button manualAddress = Action("按地址连接");
    private readonly FlowLayoutPanel addressRow;
    private readonly TableLayoutPanel waitingView;
    private readonly TableLayoutPanel incomingView;
    private readonly Font bodyFont = new("Segoe UI", 10.5f);
    private readonly Font brandFont = new("Segoe UI Semibold", 13);
    private readonly Font titleFont = new("Segoe UI Semibold", 19);
    private readonly Font deviceFont = new("Segoe UI Semibold", 25);
    private readonly TransferColors colors;
    private LanPeer[] currentPeers = [];
    private string? offerId;
    private string? lastSavedDirectory;
    private string? decision;
    private string? notice;
    private IncomingPrompt? incomingPrompt;
    private bool sending;
    private bool connecting;
    private bool refreshing;
    private bool savingNickname;
    private bool starting = true;
    private bool shuttingDown;
    private bool stopped;
    private bool exitRequested;
    private int incomingUpdateQueued;

    public TransferPanel(LanNode node, DeviceNickname nicknameStore, EventWaitHandle showRequest, EventWaitHandle closeRequest,
        TransferColors? appearance = null)
    {
        this.node = node;
        this.nicknameStore = nicknameStore;
        this.showRequest = showRequest;
        this.closeRequest = closeRequest;
        colors = appearance ?? TransferColors.FromWindows();
        Text = "局域网互传 · LYwintabs";
        Font = bodyFont;
        ClientSize = new Size(750, 524);
        MinimumSize = new Size(700, 495);
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleMode = AutoScaleMode.Dpi;

        BackColor = colors.Surface;
        ForeColor = colors.Text;
        var shell = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = Padding.Empty };
        shell.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 168));
        shell.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        shell.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        var brand = new Label
        {
            Text = "LYwintabs", Font = brandFont, AutoSize = false, Dock = DockStyle.Top,
            Height = 31, Margin = new Padding(12, 0, 0, 0)
        };
        var panelTitle = new Label { Text = "局域网互传", ForeColor = colors.Muted, AutoSize = true, Margin = new Padding(12, 0, 0, 22) };
        var rail = Stack(0, brand, panelTitle, receiveNav, sendNav, settingsNav, new Panel());
        rail.Padding = new Padding(15, 29, 14, 10);
        rail.BackColor = colors.Sidebar;
        rail.RowStyles[5] = new RowStyle(SizeType.Percent, 100);
        var sidebar = new Panel { Dock = DockStyle.Fill, BackColor = colors.Edge, Padding = new Padding(0, 0, 1, 0), Margin = Padding.Empty };
        sidebar.Controls.Add(rail);
        shell.Controls.Add(sidebar, 0, 0);

        pageHost.Controls.AddRange([receivePage, sendPage, settingsPage]);
        var layout = Stack(28, pageHost, progress, Row(pauseReceive, cancel), status);
        layout.RowStyles[0] = new RowStyle(SizeType.Percent, 100);
        layout.RowStyles[3] = new RowStyle(SizeType.AutoSize);
        status.AutoSize = true;
        status.MinimumSize = new Size(0, 23);
        status.Margin = new Padding(0, 7, 0, 0);
        shell.Controls.Add(layout, 1, 0);
        Controls.Add(shell);

        advertisedName.Font = deviceFont;
        advertisedName.MaximumSize = new Size(398, 0);
        advertisedName.TextAlign = ContentAlignment.MiddleCenter;
        advertisedName.Anchor = listening.Anchor = network.Anchor = networkToggle.Anchor = AnchorStyles.None;
        network.MaximumSize = new Size(398, 0);
        network.TextAlign = ContentAlignment.MiddleCenter;
        listening.ForeColor = network.ForeColor = colors.Muted;
        listening.Margin = new Padding(0, 1, 0, 0);
        networkToggle.ForeColor = colors.Accent;
        openSaved.Visible = false;
        var hero = Stack(8, new Panel(), new ReceiveSignal(colors), advertisedName, listening, new Panel());
        hero.BackColor = colors.Panel;
        hero.RowStyles[0] = new RowStyle(SizeType.Percent, 45);
        hero.RowStyles[4] = new RowStyle(SizeType.Percent, 55);
        var heroCard = Card(hero, 18, new Padding(9));
        waitingView = Stack(0, Heading("接收文件"), heroCard, Row(networkToggle, openSaved), network);
        waitingView.RowStyles[1] = new RowStyle(SizeType.Percent, 100);
        network.Anchor = AnchorStyles.Left;
        network.Margin = new Padding(11, 0, 0, 0);
        network.TextAlign = ContentAlignment.MiddleLeft;
        incomingTitle.ForeColor = colors.Muted;
        incomingTitle.MaximumSize = new Size(440, 0);
        incomingTitle.Margin = new Padding(0, 0, 0, 12);
        incomingView = Stack(0, Heading("收到传送请求"), incomingTitle,
            Card(offered), AlignActions(null, reject, accept));
        incomingView.RowStyles[2] = new RowStyle(SizeType.Percent, 100);
        incomingView.Visible = false;
        receivePage.Controls.AddRange([waitingView, incomingView]);

        var queueCard = Card(files, hint: dropHint);
        queueCard.Height = 80;
        remove.Visible = false;
        addressRow = Row(address, connect);
        addressRow.Visible = false;
        var deviceCard = Card(devices, hint: deviceHint);
        var sendLayout = Stack(0, Heading("发送文件"), AlignActions(fileCount, addFiles, addFolder, remove), queueCard,
            AlignActions(deviceCount, discover, manualAddress), deviceSearch, addressRow, deviceCard, AlignActions(null, send));
        sendLayout.RowStyles[6] = new RowStyle(SizeType.Percent, 100);
        sendPage.Controls.Add(sendLayout);

        var nicknameLabel = new Label { Text = "设备昵称", AutoSize = true, Font = brandFont };
        var nicknameHint = new Label { Text = "留空时使用域账户显示名称或计算机名。", AutoSize = true, Margin = new Padding(0, 7, 0, 0), ForeColor = colors.Muted };
        var destinationLabel = new Label { Text = "接收位置", AutoSize = true, Font = brandFont };
        var nicknameGroup = SettingsCard(nicknameLabel, Row(nickname, saveNickname), nicknameHint);
        var folderGroup = SettingsCard(destinationLabel, destination, Row(chooseDestination));
        var settings = Stack(0, Heading("互传设置"), nicknameGroup, folderGroup, new Panel());
        settings.RowStyles[3] = new RowStyle(SizeType.Percent, 100);
        settingsPage.Controls.Add(settings);
        foreach (var list in new[] { devices, files, offered })
        {
            list.Palette = colors;
            list.BackColor = colors.Panel;
            list.ForeColor = colors.Text;
        }
        devices.Present = item => item is PeerItem peer
            ? new(peer.Peer.Alias, peer.Peer.AddressLabel, TransferGlyph.Computer) : default;
        files.Present = item => item is SendItem file
            ? new(file.ToString(), null, file.Directory ? TransferGlyph.Folder : TransferGlyph.File) : default;
        offered.Present = item => item is TransferFileOffer file ? new(file.Name, FormatBytes(file.Size), TransferGlyph.File) : default;
        offered.DisplayMember = nameof(TransferFileOffer.Name);
        dropHint.BackColor = deviceHint.BackColor = colors.Panel;
        dropHint.ForeColor = deviceHint.ForeColor = colors.Muted;
        foreach (var box in new[] { address, nickname, deviceSearch })
        {
            box.BorderStyle = BorderStyle.FixedSingle;
            box.BackColor = colors.Panel;
            box.ForeColor = colors.Text;
        }
        foreach (var button in new[] { send, accept, saveNickname, addFiles, addFolder, chooseDestination, openSaved })
            button.ForeColor = colors.Accent;
        foreach (var label in new[] { status, fileCount, destination })
            label.ForeColor = colors.Muted;
        foreach (var button in AllActions(this))
        {
            button.BackColor = button.Parent?.BackColor ?? colors.Surface;
            button.Accent = colors.Accent;
            button.DisabledInk = colors.Disabled;
        }
        receiveNav.Click += (_, _) => ShowPage(receivePage);
        sendNav.Click += (_, _) => ShowPage(sendPage);
        settingsNav.Click += (_, _) => ShowPage(settingsPage);
        networkToggle.Click += (_, _) =>
        {
            network.Visible = !network.Visible;
            networkToggle.Text = network.Visible ? "收起连接信息" : "连接信息";
        };
        ShowPage(receivePage);

        Shown += async (_, _) => await StartListeningAsync();
        FormClosing += ClosePanel;
        node.Changed += OnNodeChanged;
        refresh.Tick += (_, _) =>
        {
            UpdateControls();
            if (showRequest.WaitOne(0) && !shuttingDown) ShowPanel();
            if (closeRequest.WaitOne(0) && !IsBusy) RequestExit();
        };
        deviceSearch.TextChanged += (_, _) => { RefreshDeviceList(); UpdateActions(); };
        connect.Click += async (_, _) => await ConnectAsync();
        manualAddress.Click += (_, _) =>
        {
            addressRow.Visible = !addressRow.Visible;
            if (addressRow.Visible) address.Focus();
        };
        saveNickname.Click += async (_, _) => await SaveNicknameAsync();
        nickname.KeyDown += async (_, e) =>
        {
            if (e.KeyCode != Keys.Enter || !saveNickname.Enabled) return;
            e.SuppressKeyPress = true;
            await SaveNicknameAsync();
        };
        address.KeyDown += async (_, e) =>
        {
            if (e.KeyCode != Keys.Enter || !connect.Enabled) return;
            e.SuppressKeyPress = true;
            await ConnectAsync();
        };
        discover.Click += async (_, _) =>
        {
            refreshing = true;
            await RunAsync(node.DiscoverAsync);
            refreshing = false;
            UpdateControls();
        };
        addFiles.Click += (_, _) =>
        {
            using var dialog = new OpenFileDialog { Multiselect = true, Title = "选择要发送的文件" };
            if (dialog.ShowDialog(this) == DialogResult.OK) AddPaths(dialog.FileNames);
        };
        addFolder.Click += (_, _) =>
        {
            using var dialog = new FolderBrowserDialog { Description = "选择要发送的文件夹" };
            if (dialog.ShowDialog(this) == DialogResult.OK) AddPaths([dialog.SelectedPath]);
        };
        remove.Click += (_, _) =>
        {
            if (files.SelectedIndex >= 0) files.Items.RemoveAt(files.SelectedIndex);
            UpdateControls();
        };
        foreach (Control target in new Control[] { this, files, sendPage, devices, sendLayout, waitingView, receivePage, dropHint, deviceHint, hero })
        {
            target.AllowDrop = true;
            target.DragEnter += (_, e) =>
            {
                if (!IsBusy && e.Data?.GetDataPresent(DataFormats.FileDrop) == true) e.Effect = DragDropEffects.Copy;
            };
            target.DragDrop += (_, e) =>
            {
                if (!IsBusy && e.Data?.GetData(DataFormats.FileDrop) is string[] paths)
                {
                    ShowPage(sendPage);
                    AddPaths(paths);
                }
            };
        }
        devices.SelectedIndexChanged += (_, _) => UpdateActions();
        files.SelectedIndexChanged += (_, _) => UpdateActions();
        send.Click += async (_, _) =>
        {
            if (devices.SelectedItem is not PeerItem item || IsBusy || files.Items.Count == 0) return;
            var paths = files.Items.Cast<SendItem>().Select(file => file.Path).ToArray();
            sending = true;
            await RunAsync(() => node.SendAsync(item.Peer, paths));
            sending = false;
            UpdateControls();
        };
        accept.Click += (_, _) => Decide(true);
        reject.Click += (_, _) => Decide(false);
        cancel.Click += async (_, _) => await RunAsync(node.CancelAsync);
        pauseReceive.Click += (_, _) =>
        {
            node.SetReceivingPaused(node.Status.Phase != TransferPhase.Paused);
            UpdateControls();
        };
        chooseDestination.Click += (_, _) =>
        {
            var selected = PickReceiveDirectory(this);
            if (selected is not null)
            {
                try
                {
                    node.UpdateReceiveDirectory(selected);
                    incomingPrompt?.SetReceiveDirectory(node.ReceiveDirectory);
                }
                catch (Exception ex) { notice = ex.Message; }
                UpdateControls();
            }
        };
        openSaved.Click += (_, _) =>
        {
            if (lastSavedDirectory is null) return;
            try { Process.Start(new ProcessStartInfo(lastSavedDirectory) { UseShellExecute = true }); }
            catch (Exception ex) { notice = ex.Message; UpdateControls(); }
        };
        UpdateControls();
    }

    private bool IsBusy => starting || sending || node.IsBusy;

    internal async Task StartListeningAsync()
    {
        if (refresh.Enabled || shuttingDown) return;
        refresh.Start();
        try
        {
            try { nickname.Text = await Task.Run(nicknameStore.Read); }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            { notice = "无法读取已保存的昵称。 " + ex.Message; }
            string alias = nickname.Text;
            if (alias.Length == 0)
            {
                alias = await Task.Run(() => nicknameStore.AutomaticName);
                nickname.PlaceholderText = alias;
            }
            await Task.Run(async () =>
            {
                await node.UpdateAliasAsync(alias);
                await node.StartAsync();
            });
        }
        catch (Exception ex) { notice = ex.Message; }
        finally { starting = false; UpdateControls(); }
    }

    internal void ShowPanel()
    {
        if (!Visible) Show();
        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
        if (!SetForegroundWindow(Handle)) BringToFront();
        UpdateControls();
    }

    internal void RequestExit()
    {
        if (shuttingDown) return;
        if (IsBusy)
        {
            notice = starting ? "正在启动互传，请稍候。" : "请先完成、拒绝或取消当前传输，再退出互传。";
            ShowPanel();
            UpdateControls();
            return;
        }
        exitRequested = true;
        Close();
    }

    private static Panel Page(string name) => new() { Name = name, Dock = DockStyle.Fill };

    private static TransferAction Navigation(string text, string name, TransferGlyph glyph)
    {
        var button = new TransferAction(text, glyph, navigation: true);
        button.Name = name;
        button.Margin = new Padding(0, 1, 0, 2);
        button.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        button.TextAlign = ContentAlignment.MiddleLeft;
        return button;
    }

    private void ShowPage(Panel selected)
    {
        receivePage.Visible = selected == receivePage;
        sendPage.Visible = selected == sendPage;
        settingsPage.Visible = selected == settingsPage;
        receiveNav.Active = selected == receivePage;
        sendNav.Active = selected == sendPage;
        settingsNav.Active = selected == settingsPage;
        receiveNav.AccessibleDescription = selected == receivePage ? "当前页面" : "";
        sendNav.AccessibleDescription = selected == sendPage ? "当前页面" : "";
        settingsNav.AccessibleDescription = selected == settingsPage ? "当前页面" : "";
    }

    private Label Heading(string text) => new()
    {
        Text = text, AutoSize = true, Font = titleFont, Margin = new Padding(0, 0, 0, 15)
    };

    private static TransferAction Action(string text, TransferGlyph glyph = TransferGlyph.None) => new(text, glyph);

    private static IEnumerable<TransferAction> AllActions(Control control)
    {
        foreach (Control child in control.Controls)
        {
            if (child is TransferAction action) yield return action;
            foreach (TransferAction descendant in AllActions(child)) yield return descendant;
        }
    }

    private TransferCard Card(Control content, int radius = 12, Padding? inset = null, Label? hint = null)
    {
        var panel = new TransferCard(colors.Panel, colors.Edge, radius)
        {
            Dock = DockStyle.Fill, Margin = new Padding(0, 2, 0, 8),
            Padding = inset ?? new Padding(8, 6, 8, 6)
        };
        panel.Controls.Add(content);
        if (hint is not null) panel.Controls.Add(hint);
        return panel;
    }

    private TransferCard SettingsCard(params Control[] sections)
    {
        var content = Stack(0, sections);
        content.AutoSize = true;
        content.AutoSizeMode = AutoSizeMode.GrowAndShrink;
        content.BackColor = colors.Panel;
        var panel = Card(content, inset: new Padding(17, 14, 17, 12));
        panel.AutoSize = true;
        panel.AutoSizeMode = AutoSizeMode.GrowAndShrink;
        panel.Margin = new Padding(0, 0, 0, 12);
        return panel;
    }

    private static TableLayoutPanel AlignActions(Label? label, params Control[] actions)
    {
        var bar = new TableLayoutPanel
        {
            ColumnCount = 2, RowCount = 1, Dock = DockStyle.Fill,
            Margin = Padding.Empty, AutoSize = true
        };
        bar.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        bar.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        bar.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        if (label is not null)
        {
            label.Anchor = AnchorStyles.Left;
            label.Margin = new Padding(1, 0, 4, 0);
            bar.Controls.Add(label, 0, 0);
        }
        var buttons = Row(actions.Reverse().ToArray());
        buttons.FlowDirection = FlowDirection.RightToLeft;
        bar.Controls.Add(buttons, 1, 0);
        return bar;
    }

    private static FlowLayoutPanel Row(params Control[] controls)
    {
        var row = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, Margin = Padding.Empty, WrapContents = false };
        row.Controls.AddRange(controls);
        return row;
    }

    private static TableLayoutPanel Stack(int padding, params Control[] controls)
    {
        var layout = new TableLayoutPanel
        {
            ColumnCount = 1, RowCount = controls.Length, Dock = DockStyle.Fill,
            Padding = new Padding(padding), Margin = Padding.Empty
        };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        for (int row = 0; row < controls.Length; row++)
        {
            layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.Controls.Add(controls[row], 0, row);
        }
        return layout;
    }

    private void AddPaths(IEnumerable<string> paths)
    {
        var previous = files.Items.Cast<SendItem>().Select(file => file.Path).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (string path in paths)
            if (previous.Add(path)) files.Items.Add(new SendItem(path, Directory.Exists(path)));
        UpdateControls();
    }

    private async Task ConnectAsync()
    {
        string input = address.Text.Trim();
        if (!IPAddress.TryParse(input, out var ip) && !IPEndPoint.TryParse(input, out _))
        {
            notice = "请输入对方的局域网 IP，例如 192.168.1.25 或 192.168.1.25:53318。";
            UpdateControls();
            return;
        }
        int port = 53317;
        if (ip is null && IPEndPoint.TryParse(input, out var endpoint))
        {
            ip = endpoint.Address;
            port = endpoint.Port;
        }
        LanPeer? peer = null;
        connecting = true;
        await RunAsync(async () =>
        {
            peer = await node.AddPeerAsync(ip!.ToString(), port);
            if (peer is null) throw new IOException("无法连接。请确认对方启用了局域网互传，并允许专用网络访问。");
        });
        connecting = false;
        if (peer is not null) deviceSearch.Clear();
        UpdateControls();
        if (peer is not null)
            devices.SelectedIndex = Array.FindIndex(currentPeers, p => p.Fingerprint == peer.Fingerprint);
    }

    private async Task SaveNicknameAsync()
    {
        string value = nickname.Text;
        string? saved = null;
        savingNickname = true;
        await RunAsync(async () =>
        {
            saved = nicknameStore.Save(value);
            await node.UpdateAliasAsync(saved.Length == 0 ? nicknameStore.AutomaticName : saved);
        });
        if (saved is not null)
        {
            nickname.Text = saved;
            if (saved.Length == 0) nickname.PlaceholderText = node.Alias;
        }
        savingNickname = false;
        UpdateControls();
    }

    private async Task RunAsync(Func<Task> operation)
    {
        notice = null;
        UpdateControls();
        try { await Task.Run(operation); }
        catch (Exception ex) { notice = ex.Message; }
        if (!IsDisposed && !shuttingDown) UpdateControls();
    }

    private void Decide(bool approved) => Decide(approved, offerId);

    private void Decide(bool approved, string? requestId, string? receiveDirectory = null)
    {
        if (requestId is null || requestId != offerId || requestId == decision || node.PendingIncoming?.RequestId != requestId) return;
        decision = offerId;
        notice = null;
        if (approved)
        {
            string directory = receiveDirectory ?? node.ReceiveDirectory;
            incomingPrompt?.BeginReceiving(directory);
            node.AcceptIncoming(requestId, directory);
        }
        else
        {
            node.RejectIncoming(requestId);
            DismissIncomingPrompt();
        }
        UpdateActions();
    }

    private void DismissIncomingPrompt()
    {
        var prompt = incomingPrompt;
        incomingPrompt = null;
        prompt?.Close();
    }

    private void ShowIncomingPrompt(IncomingTransfer request)
    {
        var prompt = new IncomingPrompt(request, node.ReceiveDirectory, FormatBytes(request.TotalBytes), colors, bodyFont);
        incomingPrompt = prompt;
        prompt.Answer += approved => Decide(approved, request.RequestId);
        prompt.PauseRequested += paused =>
        {
            node.SetReceivingPaused(paused, request.RequestId);
            UpdateControls();
        };
        prompt.StopRequested += async () => await RunAsync(() => node.StopReceivingAsync(request.RequestId));
        prompt.SaveAsRequested += () =>
        {
            var selected = PickReceiveDirectory(prompt);
            if (selected is null || prompt.IsDisposed) return;
            try
            {
                TransferSafety.EnsureNoReparsePoints(selected);
                Decide(true, request.RequestId, selected);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
            {
                MessageBox.Show(prompt, ex.Message, "无法使用此接收位置", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        };
        prompt.FormClosed += (_, _) =>
        {
            if (incomingPrompt != prompt) return;
            incomingPrompt = null;
            if (!prompt.IsReceiving) Decide(false, request.RequestId);
        };
        prompt.Show();
    }

    private string? PickReceiveDirectory(IWin32Window owner)
    {
        using var dialog = new FolderBrowserDialog
        {
            Description = "选择接收文件的保存位置", UseDescriptionForTitle = true,
            InitialDirectory = node.ReceiveDirectory, ShowNewFolderButton = true
        };
        return dialog.ShowDialog(owner) == DialogResult.OK ? dialog.SelectedPath : null;
    }

    private void OnNodeChanged(object? sender, EventArgs e)
    {
        if (node.PendingIncoming?.RequestId == Volatile.Read(ref offerId) || !IsHandleCreated ||
            Interlocked.CompareExchange(ref incomingUpdateQueued, 1, 0) != 0) return;
        try
        {
            BeginInvoke(() =>
            {
                Interlocked.Exchange(ref incomingUpdateQueued, 0);
                UpdateIncoming();
                UpdateReceivePopup();
            });
        }
        catch (InvalidOperationException) { Interlocked.Exchange(ref incomingUpdateQueued, 0); }
    }

    private void UpdateIncoming()
    {
        if (IsDisposed || shuttingDown) return;
        var pending = node.PendingIncoming;
        if (pending?.RequestId == offerId) return;
        if (pending is not null || incomingPrompt is not { IsReceiving: true }) DismissIncomingPrompt();
        Volatile.Write(ref offerId, pending?.RequestId);
        decision = null;
        offered.Items.Clear();
        if (pending is not null)
        {
            ShowIncomingPrompt(pending);
            offered.Items.AddRange(pending.Files.Cast<object>().ToArray());
            incomingTitle.Text = $"来自 {pending.SenderAlias}  ·  {pending.SenderAddress}  ·  {FormatBytes(pending.TotalBytes)}";
            ShowPage(receivePage);
        }
        waitingView.Visible = pending is null;
        incomingView.Visible = pending is not null;
        UpdateActions();
    }

    private void UpdateReceivePopup()
    {
        if (incomingPrompt is not { IsReceiving: true } prompt) return;
        var current = node.Status;
        if (current.Direction == "receive" && current.Phase is TransferPhase.Transferring or TransferPhase.Paused)
        {
            string message = $"{current.Message}  {FormatBytes(current.BytesTransferred)} / {FormatBytes(current.TotalBytes)}";
            prompt.UpdateReceiving(message, current.BytesTransferred, current.TotalBytes, current.Phase == TransferPhase.Paused);
        }
        else if (!node.IsBusy || current.Phase != TransferPhase.AwaitingDecision) DismissIncomingPrompt();
    }

    private void RefreshDeviceList()
    {
        string query = deviceSearch.Text.Trim();
        var available = node.Peers;
        var peers = available.Where(peer => peer.Alias.Contains(query, StringComparison.CurrentCultureIgnoreCase) ||
                peer.AddressLabel.Contains(query, StringComparison.OrdinalIgnoreCase))
            .OrderBy(peer => peer.Alias, StringComparer.CurrentCultureIgnoreCase)
            .ThenBy(peer => peer.Fingerprint, StringComparer.Ordinal).ToArray();
        if (!currentPeers.Select(p => (p.Fingerprint, p.Alias, p.Address, p.Port))
            .SequenceEqual(peers.Select(p => (p.Fingerprint, p.Alias, p.Address, p.Port))))
        {
            var selected = (devices.SelectedItem as PeerItem)?.Peer.Fingerprint;
            int previousTop = devices.TopIndex;
            string? top = previousTop >= 0 && previousTop < devices.Items.Count
                ? (devices.Items[previousTop] as PeerItem)?.Peer.Fingerprint : null;
            currentPeers = peers;
            devices.BeginUpdate();
            try
            {
                devices.Items.Clear();
                devices.Items.AddRange(peers.Select(p => new PeerItem(p)).ToArray());
                if (selected is not null) devices.SelectedIndex = Array.FindIndex(peers, p => p.Fingerprint == selected);
                int topIndex = Array.FindIndex(peers, p => p.Fingerprint == top);
                if (topIndex >= 0) devices.TopIndex = topIndex;
            }
            finally { devices.EndUpdate(); }
        }
        deviceCount.Text = query.Length != 0 ? $"附近设备 · {peers.Length}/{available.Count}" :
            peers.Length > 0 ? $"附近设备 · {peers.Length}" : "附近设备";
        devices.Visible = peers.Length != 0;
        deviceHint.Visible = peers.Length == 0;
        deviceHint.Text = query.Length != 0 ? "没有符合搜索条件的设备" : "请在另一台设备上启用局域网互传";
    }

    private void UpdateControls()
    {
        UpdateIncoming();
        UpdateReceivePopup();
        if (IsDisposed || shuttingDown || !Visible) return;
        if (node.IsListening && listening.Tag is null)
        {
            listening.Text = "等待接收";
            network.Text = $"{string.Join(" / ", node.GetLocalIPv4Addresses().DefaultIfEmpty("暂无"))}  ·  端口 {node.Port}";
            listening.Tag = true;
        }
        else if (!node.IsListening)
            listening.Text = starting ? "正在启用局域网互传…" : "局域网互传未启动";

        if (sendPage.Visible) RefreshDeviceList();
        var info = node.Status;
        advertisedName.Text = node.Alias;
        lastSavedDirectory = info.SavedDirectory ?? lastSavedDirectory;
        destination.Text = node.ReceiveDirectory;
        fileCount.Text = files.Items.Count == 0 ? "待发送文件" : $"待发送 · {files.Items.Count} 项";
        files.Visible = remove.Visible = files.Items.Count != 0;
        dropHint.Visible = files.Items.Count == 0;
        progress.Visible = info.Phase is TransferPhase.Transferring or TransferPhase.Paused;
        progress.Value = info.TotalBytes > 0
            ? (int)Math.Clamp(1000.0 * info.BytesTransferred / info.TotalBytes, 0, 1000) : 0;
        bool unrelatedReceiveStatus = sendPage.Visible && info.Direction == "receive" && !node.IsBusy;
        status.Text = notice ?? (unrelatedReceiveStatus ? "" : info.Phase switch
        {
            TransferPhase.Transferring => $"{info.Message}  {info.CompletedFiles}/{info.FileCount}  " +
                $"{FormatBytes(info.BytesTransferred)} / {FormatBytes(info.TotalBytes)}  {FormatBytes((long)info.BytesPerSecond)}/s",
            TransferPhase.Paused => $"{info.Message}  {FormatBytes(info.BytesTransferred)} / {FormatBytes(info.TotalBytes)}",
            TransferPhase.Idle => node.LastError ?? "",
            _ => info.Message ?? ""
        });
        status.Visible = status.Text.Length != 0;
        UpdateActions();
    }

    private void UpdateActions()
    {
        bool busy = IsBusy || shuttingDown;
        nickname.Enabled = saveNickname.Enabled = !busy && !savingNickname;
        networkToggle.Enabled = node.IsListening;
        discover.Enabled = node.IsListening && !refreshing;
        connect.Enabled = node.IsListening && !connecting;
        addFiles.Enabled = addFolder.Enabled = !busy && node.IsListening;
        remove.Enabled = !busy && files.SelectedIndex >= 0;
        send.Enabled = !busy && node.IsListening && devices.SelectedIndex >= 0 && files.Items.Count != 0;
        accept.Enabled = reject.Enabled = offerId is not null && decision != offerId && !shuttingDown;
        chooseDestination.Enabled = !busy || accept.Enabled;
        cancel.Visible = cancel.Enabled = node.IsBusy && offerId is null && !shuttingDown;
        var current = node.Status;
        bool receiving = current.Direction == "receive" && current.Phase is TransferPhase.Transferring or TransferPhase.Paused;
        pauseReceive.Visible = pauseReceive.Enabled = receiving && !shuttingDown;
        pauseReceive.Text = current.Phase == TransferPhase.Paused ? "继续" : "暂停";
        cancel.Text = receiving ? "终止接收" : "取消传输";
        openSaved.Visible = lastSavedDirectory is not null;
        openSaved.Enabled = lastSavedDirectory is not null && !shuttingDown;
    }

    private async void ClosePanel(object? sender, FormClosingEventArgs e)
    {
        if (stopped) return;
        if (e.CloseReason is CloseReason.WindowsShutDown or CloseReason.TaskManagerClosing) return;
        e.Cancel = true;
        if (!exitRequested && e.CloseReason == CloseReason.UserClosing)
        {
            Hide();
            return;
        }
        if (shuttingDown || IsBusy)
        {
            if (!shuttingDown)
            {
                exitRequested = false;
                notice = starting ? "正在启动互传，请稍候。" : "请先完成、拒绝或取消当前传输，再退出互传。";
                UpdateControls();
            }
            return;
        }
        shuttingDown = true;
        DismissIncomingPrompt();
        refresh.Stop();
        UpdateActions();
        status.Text = "正在停止局域网接收…";
        status.Visible = true;
        try { await Task.Run(async () => await node.DisposeAsync()); }
        catch (Exception ex) { Debug.WriteLine(ex); }
        finally { stopped = true; Close(); }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            node.Changed -= OnNodeChanged;
            DismissIncomingPrompt();
            refresh.Dispose();
        }
        base.Dispose(disposing);
        if (disposing)
        {
            bodyFont.Dispose();
            brandFont.Dispose();
            titleFont.Dispose();
            deviceFont.Dispose();
        }
    }

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        _ = SetProp(Handle, PanelProperty, new IntPtr(1));
        if (colors.IsDark)
        {
            const int useImmersiveDarkMode = 20;
            int enabled = 1;
            _ = DwmSetWindowAttribute(Handle, useImmersiveDarkMode, ref enabled, sizeof(int));
        }
    }

    protected override void OnHandleDestroyed(EventArgs e)
    {
        _ = RemoveProp(Handle, PanelProperty);
        base.OnHandleDestroyed(e);
    }

    protected override void WndProc(ref Message message)
    {
        if (activateMessage != 0 && message.Msg == activateMessage && message.WParam == IntPtr.Zero &&
            message.LParam == IntPtr.Zero && !shuttingDown)
        {
            ShowPanel();
            return;
        }
        base.WndProc(ref message);
    }

    [DllImport("user32.dll", EntryPoint = "RegisterWindowMessageW", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern uint RegisterWindowMessage(string name);

    [DllImport("user32.dll", EntryPoint = "SetPropW", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetProp(IntPtr window, string name, IntPtr value);

    [DllImport("user32.dll", EntryPoint = "RemovePropW", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern IntPtr RemoveProp(IntPtr window, string name);

    [DllImport("user32.dll", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("dwmapi.dll", ExactSpelling = true)]
    private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);

    private sealed class ReceiveSignal : Control
    {
        private readonly TransferColors palette;
        public ReceiveSignal(TransferColors palette)
        {
            this.palette = palette;
            Size = new Size(110, 110);
            Anchor = AnchorStyles.None;
            TabStop = false;
            SetStyle(ControlStyles.Opaque | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer, true);
            BackColor = palette.Panel;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            using (var surface = new SolidBrush(BackColor))
                e.Graphics.FillRectangle(surface, ClientRectangle);
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            float scale = Math.Min(ClientSize.Width, ClientSize.Height) / 110f;
            e.Graphics.TranslateTransform(ClientSize.Width / 2f, ClientSize.Height / 2f);
            e.Graphics.ScaleTransform(scale, scale);
            using var tint = new SolidBrush(palette.Selection);
            using var line = new Pen(palette.Edge);
            e.Graphics.FillEllipse(tint, -44, -44, 88, 88);
            e.Graphics.DrawEllipse(line, -44, -44, 88, 88);
            e.Graphics.TranslateTransform(0, 13);
            using var pen = new Pen(palette.Accent, 2.2f) { StartCap = LineCap.Round, EndCap = LineCap.Round };
            for (int radius = 10; radius <= 28; radius += 9)
                e.Graphics.DrawArc(pen, -radius, -radius, radius * 2, radius * 2, 226, 88);
            using var brush = new SolidBrush(palette.Accent);
            e.Graphics.FillEllipse(brush, -3.4f, -3.4f, 6.8f, 6.8f);
        }
    }

    private static string FormatBytes(long bytes)
    {
        if (bytes >= 1L << 30) return $"{bytes / (double)(1L << 30):0.##} GB";
        if (bytes >= 1L << 20) return $"{bytes / (double)(1L << 20):0.##} MB";
        if (bytes >= 1L << 10) return $"{bytes / 1024.0:0.##} KB";
        return $"{bytes} B";
    }

    private sealed record PeerItem(LanPeer Peer)
    {
        public override string ToString() => $"{Peer.Alias}  ({Peer.AddressLabel})";
    }

    private sealed record SendItem(string Path, bool Directory)
    {
        public override string ToString()
        {
            string name = System.IO.Path.GetFileName(System.IO.Path.TrimEndingDirectorySeparator(Path));
            return name.Length == 0 ? Path : name;
        }
    }
}
