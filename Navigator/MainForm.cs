namespace Navigator;

/// <summary>Shows the live XInput state of the first connected controller.</summary>
public sealed class MainForm : Form
{
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 16 };
    private readonly StickView _left = new("L 移動"), _right = new("R 視点");
    private readonly Label _leftInfo = NewInfoLabel(), _rightInfo = NewInfoLabel();
    private readonly Label _status = new() { AutoSize = true, Font = new Font("Yu Gothic UI", 11), ForeColor = Color.LightGray };
    private readonly Label _extra = new() { AutoSize = true, Font = new Font("Consolas", 11), ForeColor = Color.LightGray };
    private readonly CheckBox _topMost = new() { Text = "最前面", Checked = true, AutoSize = true, ForeColor = Color.LightGray };

    private static readonly (string name, ushort mask)[] ButtonNames =
        XInput.ButtonByName.Select(kv => (kv.Key, kv.Value)).OrderBy(b => b.Value).ToArray();

    public MainForm()
    {
        Text = "Stick Viewer";
        BackColor = Color.FromArgb(30, 30, 34);
        ClientSize = new Size(620, 470);
        MinimumSize = new Size(560, 440);
        TopMost = true;

        var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 3, Padding = new Padding(14) };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        var top = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        top.Controls.Add(_status);
        top.Controls.Add(_topMost);
        _topMost.CheckedChanged += (_, _) => TopMost = _topMost.Checked;
        root.Controls.Add(top, 0, 0);
        root.SetColumnSpan(top, 2);

        root.Controls.Add(Column(_left, _leftInfo), 0, 1);
        root.Controls.Add(Column(_right, _rightInfo), 1, 1);
        root.Controls.Add(_extra, 0, 2);
        root.SetColumnSpan(_extra, 2);
        Controls.Add(root);

        _timer.Tick += (_, _) => Tick();
        _timer.Start();
    }

    private static Label NewInfoLabel() =>
        new() { AutoSize = true, Font = new Font("Consolas", 12), ForeColor = Color.Gainsboro, Margin = new Padding(0, 8, 0, 0) };

    private static Control Column(Control stick, Control info)
    {
        var col = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false };
        col.Controls.Add(stick);
        col.Controls.Add(info);
        return col;
    }

    private void Tick()
    {
        var pad = XInput.FirstConnected();
        if (pad is not { } p)
        {
            _status.Text = "XInput コントローラー未接続";
            _left.Set(0, 0, false);
            _right.Set(0, 0, false);
            _leftInfo.Text = _rightInfo.Text = _extra.Text = "";
            return;
        }

        var g = p.state.Gamepad;
        _status.Text = $"コントローラー #{p.index}";
        _left.Set(g.ThumbLX, g.ThumbLY, true);
        _right.Set(g.ThumbRX, g.ThumbRY, true);
        _leftInfo.Text = Describe(g.ThumbLX, g.ThumbLY);
        _rightInfo.Text = Describe(g.ThumbRX, g.ThumbRY);

        var pressed = ButtonNames.Where(b => (g.Buttons & b.mask) != 0).Select(b => b.name);
        _extra.Text = $"LT {g.LeftTrigger / 255.0:0.00}   RT {g.RightTrigger / 255.0:0.00}   ボタン: {string.Join(" ", pressed)}";
    }

    private static string Describe(short x, short y)
    {
        double mag = Math.Sqrt((double)x * x + (double)y * y) / 32767.0;
        return $"倒し量 {mag,6:0.000}\nX {x,6} ({x / 32767.0,6:0.000})\nY {y,6} ({y / 32767.0,6:0.000})";
    }
}

/// <summary>Draws one stick: unit circle, reference rings, current position.</summary>
internal sealed class StickView : Control
{
    // Reference rings (stick magnitude): XInput default deadzone and Minecraft's measured deadzones.
    private static readonly (float r, Color color, string label)[] Rings =
    [
        (7849f / 32767f, Color.FromArgb(160, 120, 120, 120), ""),
        (0.34f, Color.FromArgb(200, 90, 160, 220), "0.34"),
        (0.40f, Color.FromArgb(200, 220, 140, 60), "0.40"),
    ];

    private readonly string _name;
    private short _x, _y;
    private bool _connected;

    public StickView(string name)
    {
        _name = name;
        Size = new Size(240, 240);
        DoubleBuffered = true;
        BackColor = Color.FromArgb(22, 22, 26);
    }

    public void Set(short x, short y, bool connected)
    {
        if (x == _x && y == _y && connected == _connected) return;
        (_x, _y, _connected) = (x, y, connected);
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        float cx = Width / 2f, cy = Height / 2f, r = Math.Min(Width, Height) / 2f - 10;

        using var rim = new Pen(Color.Gray, 1.5f);
        g.DrawEllipse(rim, cx - r, cy - r, 2 * r, 2 * r);
        g.DrawLine(Pens.DimGray, cx - r, cy, cx + r, cy);
        g.DrawLine(Pens.DimGray, cx, cy - r, cx, cy + r);

        using var small = new Font("Consolas", 8);
        foreach (var (ring, color, label) in Rings)
        {
            using var pen = new Pen(color, 1f) { DashStyle = System.Drawing.Drawing2D.DashStyle.Dash };
            float rr = r * ring;
            g.DrawEllipse(pen, cx - rr, cy - rr, 2 * rr, 2 * rr);
            if (label.Length > 0)
            {
                using var brush = new SolidBrush(color);
                g.DrawString(label, small, brush, cx + rr * 0.71f, cy - rr * 0.71f - 12);
            }
        }

        using var font = new Font("Yu Gothic UI", 9);
        g.DrawString(_name, font, Brushes.Gray, 4, 4);
        if (!_connected) return;

        float px = cx + r * _x / 32767f, py = cy - r * _y / 32767f;
        using var line = new Pen(Color.FromArgb(120, 0, 191, 255), 2f);
        g.DrawLine(line, cx, cy, px, py);
        g.FillEllipse(Brushes.DeepSkyBlue, px - 7, py - 7, 14, 14);
    }
}
