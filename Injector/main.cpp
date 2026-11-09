#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <io.h>
#include <fcntl.h>

namespace
{
    constexpr wchar_t kProcessName[] = L"Minecraft.Windows.exe";
    constexpr wchar_t kDllName[] = L"MinecraftDeadzone.dll";

    DWORD FindProcess()
    {
        DWORD pid = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W pe{ sizeof(pe) };
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        {
            if (_wcsicmp(pe.szExeFile, kProcessName) == 0) { pid = pe.th32ProcessID; break; }
        }
        CloseHandle(snap);
        return pid;
    }

    bool IsLoaded(DWORD pid, const std::wstring& dllPath)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE) return false;
        bool found = false;
        MODULEENTRY32W me{ sizeof(me) };
        for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
        {
            if (_wcsicmp(me.szExePath, dllPath.c_str()) == 0) { found = true; break; }
        }
        CloseHandle(snap);
        return found;
    }

    std::wstring DefaultDllPath()
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir(exe);
        dir.resize(dir.find_last_of(L"\\/") + 1);
        return dir + kDllName;
    }

    int Inject(DWORD pid, const std::wstring& dllPath)
    {
        HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
        if (!proc) { wprintf(L"OpenProcess に失敗しました (error %lu)\n", GetLastError()); return 1; }

        const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
        void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote) { wprintf(L"VirtualAllocEx に失敗しました (error %lu)\n", GetLastError()); CloseHandle(proc); return 1; }

        int rc = 1;
        if (!WriteProcessMemory(proc, remote, dllPath.c_str(), bytes, nullptr))
        {
            wprintf(L"WriteProcessMemory に失敗しました (error %lu)\n", GetLastError());
        }
        else
        {
            // kernel32 is mapped at the same address in every process of a boot session.
            auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
                GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
            HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadLibrary, remote, 0, nullptr);
            if (!thread)
            {
                wprintf(L"CreateRemoteThread に失敗しました (error %lu)\n", GetLastError());
            }
            else
            {
                WaitForSingleObject(thread, 10000);
                DWORD exitCode = 0;
                GetExitCodeThread(thread, &exitCode); // low 32 bits of the HMODULE, 0 = failure
                CloseHandle(thread);
                if (exitCode != 0 || IsLoaded(pid, dllPath)) { wprintf(L"注入に成功しました。\n"); rc = 0; }
                else wprintf(L"LoadLibraryW が失敗しました（DLL のパスや依存関係を確認してください）。\n");
            }
        }
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return rc;
    }
}

int wmain(int argc, wchar_t** argv)
{
    _setmode(_fileno(stdout), _O_U16TEXT);

    std::wstring dllPath;
    bool pause = true;
    for (int i = 1; i < argc; ++i)
    {
        if (_wcsicmp(argv[i], L"--nopause") == 0) pause = false;
        else dllPath = argv[i];
    }
    if (dllPath.empty()) dllPath = DefaultDllPath();

    wchar_t full[MAX_PATH];
    GetFullPathNameW(dllPath.c_str(), MAX_PATH, full, nullptr);
    dllPath = full;

    int rc = 1;
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        wprintf(L"DLL が見つかりません: %s\n", dllPath.c_str());
    }
    else
    {
        wprintf(L"DLL: %s\n", dllPath.c_str());
        DWORD pid = FindProcess();
        if (!pid)
        {
            wprintf(L"%s の起動を待っています...（Ctrl+C で中止）\n", kProcessName);
            while (!(pid = FindProcess())) Sleep(1000);
            Sleep(3000); // let the process finish its early startup
        }
        wprintf(L"%s pid=%lu\n", kProcessName, pid);

        if (IsLoaded(pid, dllPath)) { wprintf(L"すでに注入済みです。\n"); rc = 0; }
        else rc = Inject(pid, dllPath);
        if (rc == 0) wprintf(L"設定ファイル: DLL と同じフォルダの MinecraftDeadzone.ini（保存すると即反映、F8 で有効/無効）\n");
    }

    if (pause)
    {
        wprintf(L"\nEnter キーで終了します。\n");
        (void)getwchar();
    }
    return rc;
}
