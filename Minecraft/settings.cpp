#include "pch.h"
#include "settings.h"
#include "log.h"

static Settings g_settings = { true, VK_F8, false, true, { 0.15f, 0.34f, 1.0f }, { 0.14f, 0.40f, 1.0f } };
static std::mutex g_settingsLock;
static const wchar_t* DEFAULT_INI =
L"; MinecraftDeadzone 設定ファイル\r\n"
L"; 保存すると1秒くらいでゲームに反映される\r\n"
L"; 倒し量は 0.0 = 真ん中 / 1.0 = 一番倒した所\r\n"
L"; ※実測だとゲーム側のデッドゾーンは 視点(右)が約0.40、移動(左)が約0.34\r\n"
L"\r\n"
L"[General]\r\n"
L"Enabled=1             ; 1 = 補正する / 0 = 補正しない\r\n"
L"ToggleKey=0x77        ; 補正のON/OFFを切り替えるキー(仮想キーコード) 0x77 = F8 / 0 = 使わない\r\n"
L"PauseInMenu=1         ; 1 = メニューや設定画面を開いてる間は補正しない(ゲーム画面に戻ると補正に戻る) / 0 = 常に補正\r\n"
L"\r\n"
L"[LeftStick]\r\n"
L"; 左スティック(移動)\r\n"
L"InnerDeadzone=0.15    ; これ以下の倒し量は0にする(ドリフト対策) 手を離しても勝手に歩くなら上げる\r\n"
L"GameDeadzone=0.34     ; ゲーム側のデッドゾーン。少し倒しただけでこの値まで持ち上げる 反応しないなら上げる / 少しで速すぎるなら下げる\r\n"
L"Curve=1.0             ; 倒し量と速さの関係 1.0 = 比例 / 1より大きいと少し倒した時ゆっくりになる\r\n"
L"\r\n"
L"[RightStick]\r\n"
L"; 右スティック(視点)\r\n"
L"InnerDeadzone=0.14    ; これ以下の倒し量は0にする(ドリフト対策) 手を離しても視点が勝手に回るなら上げる\r\n"
L"GameDeadzone=0.40     ; ゲーム側のデッドゾーン。少し倒しただけでこの値まで持ち上げる 反応しないなら上げる / 少しで速すぎるなら下げる\r\n"
L"Curve=1.0             ; 倒し量と回る速さの関係 1.0 = 比例 / 1より大きいと少し倒した時ゆっくりになる\r\n"
L"\r\n"
L"[Debug]\r\n"
L"LogStats=0            ; 1 = 5秒ごとにログ(MinecraftDeadzone.log)に呼ばれた回数とスティックの値を書く\r\n";

static float ReadFloat(const wchar_t* ini, const wchar_t* section, const wchar_t* key, float def, float min, float max)
{
    wchar_t buf[64] = { 0 };
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini);
    if (buf[0] == 0)
        return def;

    float v = (float)_wtof(buf);
    if (v < min) v = min;
    if (v > max) v = max;
    return v;
}

static int ReadInt(const wchar_t* ini, const wchar_t* section, const wchar_t* key, int def)
{
    wchar_t buf[64] = { 0 };
    GetPrivateProfileStringW(section, key, L"", buf, 64, ini);
    if (buf[0] == 0)
        return def;
    return (int)wcstol(buf, NULL, 0);
}

void LoadSettings(const wchar_t* iniPath)
{
    if (GetFileAttributesW(iniPath) == INVALID_FILE_ATTRIBUTES)
    {
        FILE* f = NULL;
        _wfopen_s(&f, iniPath, L"wb");
        if (f)
        {
            unsigned short bom = 0xFEFF;
            fwrite(&bom, 2, 1, f);
            fwrite(DEFAULT_INI, sizeof(wchar_t), wcslen(DEFAULT_INI), f);
            fclose(f);
            Log("ini作った");
        }
    }

    Settings s;
    s.enabled = ReadInt(iniPath, L"General", L"Enabled", 1) != 0;
    s.toggleKey = ReadInt(iniPath, L"General", L"ToggleKey", VK_F8);
    s.logStats = ReadInt(iniPath, L"Debug", L"LogStats", 0) != 0;
    s.pauseInMenu = ReadInt(iniPath, L"General", L"PauseInMenu", 1) != 0;
    s.left.inner = ReadFloat(iniPath, L"LeftStick", L"InnerDeadzone", 0.15f, 0.0f, 0.9f);
    s.left.game = ReadFloat(iniPath, L"LeftStick", L"GameDeadzone", 0.34f, 0.0f, 0.95f);
    s.left.curve = ReadFloat(iniPath, L"LeftStick", L"Curve", 1.0f, 0.2f, 5.0f);
    s.right.inner = ReadFloat(iniPath, L"RightStick", L"InnerDeadzone", 0.14f, 0.0f, 0.9f);
    s.right.game = ReadFloat(iniPath, L"RightStick", L"GameDeadzone", 0.40f, 0.0f, 0.95f);
    s.right.curve = ReadFloat(iniPath, L"RightStick", L"Curve", 1.0f, 0.2f, 5.0f);
    g_settingsLock.lock();
    g_settings = s;
    g_settingsLock.unlock();
    Log("設定読み込み enabled=%d key=0x%X menu=%d stats=%d | L %.2f %.2f %.2f | R %.2f %.2f %.2f",
        s.enabled, s.toggleKey, s.pauseInMenu, s.logStats,
        s.left.inner, s.left.game, s.left.curve,
        s.right.inner, s.right.game, s.right.curve);
}

Settings GetSettings()
{
    g_settingsLock.lock();
    Settings s = g_settings;
    g_settingsLock.unlock();
    return s;
}

void SetEnabled(bool enabled)
{
    g_settingsLock.lock();
    g_settings.enabled = enabled;
    g_settingsLock.unlock();
}

FILETIME GetIniTime(const wchar_t* iniPath)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    FILETIME zero = { 0, 0 };
    if (!GetFileAttributesExW(iniPath, GetFileExInfoStandard, &data))
        return zero;
    return data.ftLastWriteTime;
}
