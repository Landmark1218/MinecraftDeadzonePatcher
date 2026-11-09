using System.Runtime.InteropServices;

namespace Navigator;

[StructLayout(LayoutKind.Sequential)]
public struct XInputGamepad
{
    public ushort Buttons;
    public byte LeftTrigger;
    public byte RightTrigger;
    public short ThumbLX;
    public short ThumbLY;
    public short ThumbRX;
    public short ThumbRY;
}

[StructLayout(LayoutKind.Sequential)]
public struct XInputState
{
    public uint PacketNumber;
    public XInputGamepad Gamepad;
}

public static class XInput
{
    public const ushort A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;
    public static readonly Dictionary<string, ushort> ButtonByName = new(StringComparer.OrdinalIgnoreCase)
    {
        ["UP"] = 0x0001, ["DOWN"] = 0x0002, ["LEFT"] = 0x0004, ["RIGHT"] = 0x0008,
        ["START"] = 0x0010, ["BACK"] = 0x0020, ["LS"] = 0x0040, ["RS"] = 0x0080,
        ["LB"] = 0x0100, ["RB"] = 0x0200, ["A"] = A, ["B"] = B, ["X"] = X, ["Y"] = Y,
    };
    public const int LeftDeadzone = 7849, RightDeadzone = 8689;

    [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")]
    private static extern uint GetStateNative(uint index, out XInputState state);

    /// <summary>Returns the state of the first connected pad, or null if none.</summary>
    public static (int index, XInputState state)? FirstConnected()
    {
        for (uint i = 0; i < 4; i++)
        {
            if (GetStateNative(i, out var s) == 0) return ((int)i, s);
        }
        return null;
    }
}
