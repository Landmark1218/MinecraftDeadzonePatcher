#include "pch.h"
#include "hook.h"
#include "settings.h"
#include "log.h"

HMODULE g_hModule = NULL;
wchar_t g_iniPath[MAX_PATH];
extern volatile bool g_inMenu;

//ゲーム画面にいるかどうか
//プレイ中はマウスカーソルが1点に固定される(ClipCursorの範囲が幅0)
static bool IsPlaying()
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId())
        return false;

    RECT clip;
    if (!GetClipCursor(&clip))
        return false;

    int w = clip.right - clip.left;
    int h = clip.bottom - clip.top;
    if (w <= 2 && h <= 2)
        return true;
    return false;
}

DWORD WINAPI MainThread(LPVOID param)
{
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);
    std::wstring logPath = dllPath;
    logPath = logPath.substr(0, logPath.rfind(L'.')) + L".log";
    LogOpen(logPath.c_str());
    std::wstring dir = dllPath;
    dir = dir.substr(0, dir.rfind(L'\\') + 1);
    wcscpy_s(g_iniPath, (dir + L"MinecraftDeadzone.ini").c_str());
    Log("読み込まれた pid=%lu", GetCurrentProcessId());
    LoadSettings(g_iniPath);

    if (!InstallHooks())
        Log("フック失敗");
    else
        Log("フックOK");

    FILETIME lastTime = GetIniTime(g_iniPath);
    bool keyDown = false;
    int count = 0;
    while (true)
    {
        Sleep(50);
        count++;

        bool playing = IsPlaying();
        if (playing == g_inMenu)
        {
            g_inMenu = !playing;
            if (g_inMenu)
                Log("メニュー/他の画面 -> 補正止める");
            else
                Log("ゲーム画面 -> 補正する");
        }

        // F8でONOFF
        Settings s = GetSettings();
        bool down = false;
        if (s.toggleKey > 0 && s.toggleKey < 256)
            down = (GetAsyncKeyState(s.toggleKey) & 0x8000) != 0;
        if (down && !keyDown)
        {
            SetEnabled(!s.enabled);
            if (!s.enabled)
            {
                Log("ON");
                MessageBeep(MB_OK);
            }
            else
            {
                Log("OFF");
                MessageBeep(MB_ICONHAND);
            }
        }
        keyDown = down;
        if (count % 20 == 0)
        {
            FILETIME t = GetIniTime(g_iniPath);
            if (CompareFileTime(&t, &lastTime) != 0)
            {
                lastTime = t;
                Sleep(100);
                LoadSettings(g_iniPath);
            }
        }
        if (count % 100 == 0 && s.logStats)
            PrintStats();
    }
    return 0;
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        HMODULE dummy;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&MainThread, &dummy);
        HANDLE h = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
        if (h)
            CloseHandle(h);
        break;
    }
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
