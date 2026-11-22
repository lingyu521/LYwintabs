using System.ComponentModel;
using System.Drawing.Drawing2D;
using Microsoft.Win32;

namespace LYwintabs.Transfer;

internal readonly record struct TransferColors(Color Surface, Color Sidebar, Color Panel, Color Edge,
    Color Text, Color Muted, Color Accent, Color Selection, Color Disabled, bool IsDark)
{
    public static TransferColors Light => new(Color.FromArgb(250, 251, 253), Color.FromArgb(238, 241, 247),
        Color.White, Color.FromArgb(217, 222, 232), Color.FromArgb(29, 35, 49), Color.FromArgb(83, 94, 114),
        Color.FromArgb(29, 82, 181), Color.FromArgb(227, 237, 255), Color.FromArgb(122, 132, 151), false);

    public static TransferColors Dark => new(Color.FromArgb(29, 31, 37), Color.FromArgb(36, 39, 48),
        Color.FromArgb(36, 39, 46), Color.FromArgb(63, 68, 82), Color.FromArgb(244, 246, 251), Color.FromArgb(180, 188, 204),
        Color.FromArgb(151, 186, 255), Color.FromArgb(48, 62, 87), Color.FromArgb(127, 138, 157), true);

    public static TransferColors FromWindows()
    {
        if (SystemInformation.HighContrast)
            return new(SystemColors.Window, SystemColors.Window, SystemColors.Window, SystemColors.WindowText,
                SystemColors.WindowText, SystemColors.WindowText, SystemColors.HotTrack, SystemColors.Window, SystemColors.GrayText, false);
        return Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
            "AppsUseLightTheme", 1) is 0 ? Dark : Light;
    }
}

internal enum TransferGlyph { None, Receive, Send, Settings, File, Folder, Computer, Link, Refresh, Plus }

internal static class TransferDrawing
{
    public static GraphicsPath RoundedRectangle(RectangleF bounds, float radius)
    {
        var path = new GraphicsPath();
        float diameter = Math.Min(radius * 2, Math.Min(bounds.Width, bounds.Height));
        if (diameter <= 0) return path;
        path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180, 90);
        path.AddArc(bounds.Right - diameter, bounds.Y, diameter, diameter, 270, 90);
        path.AddArc(bounds.Right - diameter, bounds.Bottom - diameter, diameter, diameter, 0, 90);
        path.AddArc(bounds.X, bounds.Bottom - diameter, diameter, diameter, 90, 90);
        path.CloseFigure();
        return path;
    }

    public static void DrawGlyph(Graphics graphics, TransferGlyph glyph, Rectangle bounds, Color ink)
    {
        if (glyph == TransferGlyph.None) return;
        GraphicsState saved = graphics.Save();
        graphics.SmoothingMode = SmoothingMode.AntiAlias;
        graphics.TranslateTransform(bounds.Left, bounds.Top);
        graphics.ScaleTransform(bounds.Width / 20f, bounds.Height / 20f);
        using var pen = new Pen(ink, 1.35f) { LineJoin = LineJoin.Round, StartCap = LineCap.Round, EndCap = LineCap.Round };
        switch (glyph)
        {
            case TransferGlyph.Receive:
                graphics.DrawLines(pen, [new(3, 11), new(3, 16), new(17, 16), new(17, 11)]);
                graphics.DrawLine(pen, 10, 3, 10, 12);
                graphics.DrawLines(pen, [new(6, 8), new(10, 12), new(14, 8)]);
                break;
            case TransferGlyph.Send:
                graphics.DrawPolygon(pen, [new(3, 8), new(17, 3), new(13, 17), new(9, 12)]);
                graphics.DrawLine(pen, 9, 12, 17, 3);
                break;
            case TransferGlyph.Settings:
                graphics.DrawLine(pen, 3, 6, 7, 6);
                graphics.DrawEllipse(pen, 7, 4, 4, 4);
                graphics.DrawLine(pen, 11, 6, 17, 6);
                graphics.DrawLine(pen, 3, 14, 10, 14);
                graphics.DrawEllipse(pen, 10, 12, 4, 4);
                graphics.DrawLine(pen, 14, 14, 17, 14);
                break;
            case TransferGlyph.File:
                graphics.DrawPolygon(pen, [new(5, 2), new(11, 2), new(15, 6), new(15, 18), new(5, 18)]);
                graphics.DrawLines(pen, [new(11, 2), new(11, 6), new(15, 6)]);
                break;
            case TransferGlyph.Folder:
                graphics.DrawPolygon(pen, [new(2, 15), new(2, 5), new(8, 5), new(10, 7), new(18, 7), new(18, 15)]);
                break;
            case TransferGlyph.Computer:
                graphics.DrawRectangle(pen, 2, 3, 16, 11);
                graphics.DrawLine(pen, 10, 14, 10, 17);
                graphics.DrawLine(pen, 6, 17, 14, 17);
                break;
            case TransferGlyph.Link:
                graphics.DrawArc(pen, 3, 7, 8, 8, 35, 255);
                graphics.DrawArc(pen, 9, 4, 8, 8, 215, 255);
                graphics.DrawLine(pen, 8, 12, 12, 8);
                break;
            case TransferGlyph.Refresh:
                graphics.DrawArc(pen, 3, 3, 14, 14, -45, 300);
                graphics.DrawLines(pen, [new(16, 2), new(16, 6), new(12, 6)]);
                break;
            case TransferGlyph.Plus:
                graphics.DrawLine(pen, 5, 10, 15, 10);
                graphics.DrawLine(pen, 10, 5, 10, 15);
                break;
        }
        graphics.Restore(saved);
    }
}

internal sealed class TransferAction : Button
{
    private TransferGlyph Glyph { get; }
    private readonly bool navigation;
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    internal Color Accent { get; set; }
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    internal Color DisabledInk { get; set; }
    private bool active;
    private bool hovered;

    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    internal bool Active
    {
        get => active;
        set { active = value; Invalidate(); }
    }

    public TransferAction(string text, TransferGlyph glyph, bool navigation = false)
    {
        Glyph = glyph;
        this.navigation = navigation;
        Text = text;
        FlatStyle = FlatStyle.Flat;
        FlatAppearance.BorderSize = 0;
        SetStyle(ControlStyles.SupportsTransparentBackColor, false);
        SetStyle(ControlStyles.Opaque | ControlStyles.OptimizedDoubleBuffer |
            ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        Cursor = Cursors.Hand;
        FitContent();
    }

    public override Size GetPreferredSize(Size proposedSize)
    {
        int iconSpace = Glyph == TransferGlyph.None ? 0 : Logical(23);
        return new Size(Math.Max(Logical(navigation ? 110 : 46), MeasureTitle() + iconSpace + Logical(20)),
            Logical(navigation ? 38 : 34));
    }

    protected override void OnTextChanged(EventArgs e)
    {
        base.OnTextChanged(e);
        FitContent();
    }

    protected override void OnFontChanged(EventArgs e)
    {
        base.OnFontChanged(e);
        FitContent();
    }

    protected override void OnDpiChangedAfterParent(EventArgs e)
    {
        base.OnDpiChangedAfterParent(e);
        FitContent();
    }

    private void FitContent()
    {
        Size preferred = GetPreferredSize(Size.Empty);
        MinimumSize = preferred;
        Size = preferred;
    }

    private int MeasureTitle()
    {
        var options = TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix | TextFormatFlags.NoPadding;
        int measured = TextRenderer.MeasureText(Text, Font, Size.Empty, options).Width;
        // The Windows font fallback can draw CJK characters wider than the selected font reports.
        int ideographWidth = (int)Math.Ceiling(Font.SizeInPoints * DeviceDpi / 72f);
        return Math.Max(measured, ideographWidth * Text.Count(character => character >= '\u2E80'));
    }

    protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); hovered = true; Invalidate(); }
    protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hovered = false; Invalidate(); }
    protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
    protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }

    protected override void OnPaint(PaintEventArgs e)
    {
        // This path replaces ButtonBase's paint adapter, so it must initialize the entire paint buffer.
        var bounds = ClientRectangle;
        using (var surface = new SolidBrush(BackColor))
            e.Graphics.FillRectangle(surface, bounds);
        Color ink = !Enabled ? DisabledInk : active || hovered ? Accent : ForeColor;
        if (ink.IsEmpty) ink = ForeColor;
        var flags = TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix | TextFormatFlags.NoPadding |
            TextFormatFlags.EndEllipsis | TextFormatFlags.VerticalCenter;
        bool left = TextAlign == ContentAlignment.MiddleLeft;
        int iconSize = Glyph == TransferGlyph.None ? 0 : Logical(16);
        int offset = iconSize == 0 ? 0 : iconSize + Logical(7);
        int textWidth = MeasureTitle();
        int start = left ? Logical(14) : Math.Max(Logical(7), (bounds.Width - textWidth - offset) / 2);
        if (active)
        {
            using var marker = new SolidBrush(Accent);
            e.Graphics.FillRectangle(marker, Logical(2), (bounds.Height - Logical(18)) / 2, Logical(2), Logical(18));
        }
        if (iconSize != 0)
            TransferDrawing.DrawGlyph(e.Graphics, Glyph, new Rectangle(start, (bounds.Height - iconSize) / 2, iconSize, iconSize), ink);
        int textX = start + offset;
        int available = Math.Max(0, bounds.Width - textX - Logical(5));
        TextRenderer.DrawText(e.Graphics, Text, Font, new Rectangle(textX, 0, available, bounds.Height), ink, flags);
        if (Enabled && (hovered || (Focused && ShowFocusCues)))
        {
            using var pen = new Pen(ink);
            if (Focused && ShowFocusCues && bounds.Width > 6 && bounds.Height > 6)
            {
                bounds.Inflate(-2, -2);
                using var outline = TransferDrawing.RoundedRectangle(bounds, Logical(6));
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                e.Graphics.DrawPath(pen, outline);
            }
            else
            {
                int underline = (bounds.Height + Font.Height) / 2;
                e.Graphics.DrawLine(pen, textX, underline, textX + Math.Min(available, textWidth), underline);
            }
        }
    }

    private int Logical(int value) => (value * DeviceDpi + 48) / 96;
}

internal sealed class TransferCard : Panel
{
    private readonly Color surface;
    private readonly Color edge;
    private readonly int radius;

    public TransferCard(Color surface, Color edge, int radius)
    {
        this.surface = surface;
        this.edge = edge;
        this.radius = radius;
        SetStyle(ControlStyles.ResizeRedraw | ControlStyles.OptimizedDoubleBuffer, true);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var bounds = new RectangleF(0.5f, 0.5f, Width - 1, Height - 1);
        if (bounds.Width <= 0 || bounds.Height <= 0) return;
        using var shape = TransferDrawing.RoundedRectangle(bounds, radius * DeviceDpi / 96f);
        using var fill = new SolidBrush(surface);
        using var outline = new Pen(edge);
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        e.Graphics.FillPath(fill, shape);
        e.Graphics.DrawPath(outline, shape);
    }
}

internal readonly record struct TransferListRow(string Title, string? Detail, TransferGlyph Glyph);

internal sealed class TransferList : ListBox
{
    private readonly bool twoLines;
    private Font? detailFont;
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    internal TransferColors Palette { get; set; }
    [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
    internal Func<object, TransferListRow>? Present { get; set; }

    public TransferList(string accessibleName, bool twoLines = false)
    {
        this.twoLines = twoLines;
        AccessibleName = accessibleName;
        Dock = DockStyle.Fill;
        BorderStyle = BorderStyle.None;
        IntegralHeight = false;
        DrawMode = DrawMode.OwnerDrawFixed;
        ItemHeight = twoLines ? 50 : 32;
    }

    protected override void OnDpiChangedAfterParent(EventArgs e)
    {
        base.OnDpiChangedAfterParent(e);
        ItemHeight = Logical(twoLines ? 50 : 32);
    }

    protected override void OnFontChanged(EventArgs e)
    {
        base.OnFontChanged(e);
        detailFont?.Dispose();
        detailFont = null;
    }

    protected override void OnDrawItem(DrawItemEventArgs e)
    {
        using var surface = new SolidBrush(BackColor);
        e.Graphics.FillRectangle(surface, e.Bounds);
        if (e.Index < 0 || e.Index >= Items.Count || Items[e.Index] is not object item) return;
        bool selected = (e.State & DrawItemState.Selected) != 0;
        var row = Present?.Invoke(item) ?? new TransferListRow(GetItemText(item) ?? "", null, TransferGlyph.File);
        var bounds = e.Bounds;
        if (selected)
        {
            var highlight = Rectangle.Inflate(bounds, -1, -2);
            using var shape = TransferDrawing.RoundedRectangle(highlight, Logical(7));
            using var tint = new SolidBrush(Palette.Selection);
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            e.Graphics.FillPath(tint, shape);
        }
        else if (e.Index < Items.Count - 1)
        {
            using var line = new Pen(Palette.Edge);
            e.Graphics.DrawLine(line, bounds.Left + Logical(34), bounds.Bottom - 1, bounds.Right - Logical(10), bounds.Bottom - 1);
        }
        int iconSize = Logical(16);
        TransferDrawing.DrawGlyph(e.Graphics, row.Glyph, new Rectangle(bounds.Left + Logical(10),
            bounds.Top + (bounds.Height - iconSize) / 2, iconSize, iconSize), selected ? Palette.Accent : Palette.Muted);
        if (selected && Focused)
        {
            using var marker = new SolidBrush(Palette.Accent);
            e.Graphics.FillRectangle(marker, bounds.Left + Logical(3), bounds.Top + Logical(7), Logical(2), bounds.Height - Logical(14));
        }
        bounds.X += Logical(36);
        bounds.Width = Math.Max(1, bounds.Width - Logical(45));
        var flags = TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding |
            TextFormatFlags.NoPrefix | TextFormatFlags.VerticalCenter;
        if (twoLines && row.Detail is not null)
        {
            detailFont ??= new Font(Font.FontFamily, Math.Max(9, Font.Size - 1));
            int stackHeight = Font.Height + detailFont.Height;
            bounds.Y += (bounds.Height - stackHeight) / 2;
            bounds.Height = Font.Height;
            TextRenderer.DrawText(e.Graphics, row.Title, Font, bounds, Palette.Text, flags);
            bounds.Y += bounds.Height;
            bounds.Height = detailFont.Height;
            TextRenderer.DrawText(e.Graphics, row.Detail, detailFont, bounds, Palette.Muted, flags);
        }
        else
        {
            if (row.Detail is not null)
            {
                detailFont ??= new Font(Font.FontFamily, Math.Max(9, Font.Size - 1));
                int width = Math.Min(bounds.Width / 2, TextRenderer.MeasureText(row.Detail, detailFont).Width);
                var detailBounds = new Rectangle(bounds.Right - width, bounds.Top, width, bounds.Height);
                TextRenderer.DrawText(e.Graphics, row.Detail, detailFont, detailBounds, Palette.Muted, flags | TextFormatFlags.Right);
                bounds.Width = Math.Max(1, bounds.Width - width - Logical(9));
            }
            TextRenderer.DrawText(e.Graphics, row.Title, Font, bounds, Palette.Text, flags);
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) detailFont?.Dispose();
        base.Dispose(disposing);
    }

    private int Logical(int value) => (value * DeviceDpi + 48) / 96;
}
