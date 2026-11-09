#include "pch.h"
#include "hook.h"
#include "settings.h"
#include "log.h"

//0x14EFC9698のGUID
static const GUID IID_IGameInput_New = { 0xbbaa66d2, 0x837a, 0x40f7, { 0xa3, 0x03, 0x91, 0x7d, 0x50, 0x09, 0x55, 0xf4 } };
#define SLOT_GETCURRENTREADING 4   //QI, AddRef, Release, GetCurrentTimestampの次
#define SLOT_GAMEPAD_V0 22         //GameInput.hのIGameInputReadingの並び通り
#define SLOT_GAMEPAD_NEW 18        //新しい方はGetSequenceNumberとかTouch系が無く18番目
typedef bool(__stdcall* GetGamepadState_t)(IGameInputReading* self, GameInputGamepadState* state);
typedef HRESULT(__stdcall* GetCurrentReading_t)(void* self, GameInputKind kind, IGameInputDevice* device, IGameInputReading** reading);
GetGamepadState_t oGetGamepadState_v0 = NULL;
GetGamepadState_t oGetGamepadState_new = NULL;
volatile bool g_inMenu = false;
volatile LONG g_callsV0 = 0;
volatile LONG g_callsNew = 0;
float g_lastRawRX = 0, g_lastRawRY = 0, g_lastOutRX = 0, g_lastOutRY = 0;
static void FixStick(float& x, float& y, StickSetting s)
{
    float len = sqrtf(x * x + y * y);
    if (len < 0.000001f)
    {
        x = 0;
        y = 0;
        return;
    }
    float l = len;
    if (l > 1.0f)
        l = 1.0f;
    if (l <= s.inner)
    {
        x = 0;
        y = 0;
        return;
    }
    //inner〜1.0を0〜1 にしてそれを game〜1.0に引き伸ばす
    float t = (l - s.inner) / (1.0f - s.inner);
    if (s.curve != 1.0f)
        t = powf(t, s.curve);
    float newLen = s.game + (1.0f - s.game) * t;
    if (newLen > 1.0f)
        newLen = 1.0f;
    x = x / len * newLen;
    y = y / len * newLen;
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    if (y > 1.0f) y = 1.0f;
    if (y < -1.0f) y = -1.0f;
}
static void ApplyDeadzone(GameInputGamepadState* state)
{
    g_lastRawRX = state->rightThumbstickX;
    g_lastRawRY = state->rightThumbstickY;

    Settings s = GetSettings();
    bool doFix = s.enabled;
    if (s.pauseInMenu && g_inMenu)
        doFix = false;

    if (doFix)
    {
        FixStick(state->leftThumbstickX, state->leftThumbstickY, s.left);
        FixStick(state->rightThumbstickX, state->rightThumbstickY, s.right);
    }

    g_lastOutRX = state->rightThumbstickX;
    g_lastOutRY = state->rightThumbstickY;
}

bool __stdcall hkGetGamepadState_v0(IGameInputReading* self, GameInputGamepadState* state)
{
    InterlockedIncrement(&g_callsV0);
    bool ret = oGetGamepadState_v0(self, state);
    if (ret && state != NULL)
        ApplyDeadzone(state);
    return ret;
}

bool __stdcall hkGetGamepadState_new(IGameInputReading* self, GameInputGamepadState* state)
{
    InterlockedIncrement(&g_callsNew);
    bool ret = oGetGamepadState_new(self, state);
    if (ret && state != NULL)
        ApplyDeadzone(state);
    return ret;
}

static void** GetVtable(void* obj)
{
    return *(void***)obj;
}

static IGameInputReading* GetPadReading(void* gameInput)
{
    GetCurrentReading_t fn = (GetCurrentReading_t)GetVtable(gameInput)[SLOT_GETCURRENTREADING];
    IGameInputReading* reading = NULL;
    HRESULT hr = fn(gameInput, GameInputKindGamepad, NULL, &reading);
    if (FAILED(hr))
        return NULL;
    return reading;
}

static bool IsInModule(HMODULE mod, void* p)
{
    MODULEINFO info = { 0 };
    if (!GetModuleInformation(GetCurrentProcess(), mod, &info, sizeof(info)))
        return false;
    BYTE* start = (BYTE*)info.lpBaseOfDll;
    BYTE* end = start + info.SizeOfImage;
    return (BYTE*)p >= start && (BYTE*)p < end;
}

static bool Near(float a, float b)
{
    return fabsf(a - b) <= 0.02f;
}

//同じタイミングでv0のGetGamepadStateと値が一緒ならOK
//違うスロットだったらフックしないGameInputが更新されて並びが変わった時用
static bool CheckNewSlot(void* giV0, void* giNew, GetGamepadState_t fnV0, GetGamepadState_t fnNew)
{
    for (int i = 0; i < 20; i++)
    {
        IGameInputReading* r0 = GetPadReading(giV0);
        IGameInputReading* r1 = GetPadReading(giNew);

        bool ok = false;
        if (r0 && r1)
        {
            GameInputGamepadState a = { 0 };
            unsigned char b[sizeof(GameInputGamepadState) + 64];
            memset(b, 0xCC, sizeof(b));

            bool ra = fnV0(r0, &a);
            bool rb = fnNew(r1, (GameInputGamepadState*)b);
            bool overflow = false;
            for (int j = sizeof(GameInputGamepadState); j < (int)sizeof(b); j++)
            {
                if (b[j] != 0xCC)
                    overflow = true;
            }

            GameInputGamepadState* bs = (GameInputGamepadState*)b;
            if (ra && rb && !overflow &&
                a.buttons == bs->buttons &&
                Near(a.leftTrigger, bs->leftTrigger) && Near(a.rightTrigger, bs->rightTrigger) &&
                Near(a.leftThumbstickX, bs->leftThumbstickX) && Near(a.leftThumbstickY, bs->leftThumbstickY) &&
                Near(a.rightThumbstickX, bs->rightThumbstickX) && Near(a.rightThumbstickY, bs->rightThumbstickY))
            {
                ok = true;
            }

            if (!ok && i == 19)
                Log("チェック失敗 ra=%d rb=%d overflow=%d  v0 L(%.3f,%.3f) new L(%.3f,%.3f)",
                    ra, rb, overflow, a.leftThumbstickX, a.leftThumbstickY, bs->leftThumbstickX, bs->leftThumbstickY);
        }

        if (r0) r0->Release();
        if (r1) r1->Release();

        if (ok)
            return true;
        Sleep(10);
    }
    return false;
}

bool InstallHooks()
{
    HMODULE mod = NULL;
    const char* modName = "";
    int waitCount = 0;
    while (true)
    {
        mod = GetModuleHandleW(L"GameInputRedist.dll");
        modName = "GameInputRedist.dll";
        if (mod == NULL)
        {
            mod = GetModuleHandleW(L"GameInput.dll");
            modName = "GameInput.dll";
        }
        if (mod != NULL)
            break;

        if (waitCount == 0)
            Log("GameInputのdllがまだ読み込まれてないので待つ");
        waitCount++;
        Sleep(500);
    }
    Log("%s = %p", modName, mod);

    typedef HRESULT(WINAPI* GameInputInitialize_t)(REFIID riid, void** ppv);
    typedef HRESULT(WINAPI* GameInputCreate_t)(IGameInput** gi);

    IGameInput* gi = NULL;
    while (gi == NULL)
    {
        GameInputInitialize_t init = (GameInputInitialize_t)GetProcAddress(mod, "GameInputInitialize");
        if (init != NULL)
            init(__uuidof(IGameInput), (void**)&gi);

        if (gi == NULL)
        {
            GameInputCreate_t create = (GameInputCreate_t)GetProcAddress(mod, "GameInputCreate");
            if (create != NULL)
                create(&gi);
        }

        if (gi == NULL)
        {
            Log("IGameInputが取れない、リトライ");
            Sleep(2000);
        }
    }

    //readingはパッドが繋がってないと取れない
    IGameInputReading* r = NULL;
    waitCount = 0;
    while ((r = GetPadReading(gi)) == NULL)
    {
        if (waitCount == 0)
            Log("コントローラー待ち...");
        waitCount++;
        Sleep(500);
    }
    GetGamepadState_t fnV0 = (GetGamepadState_t)GetVtable(r)[SLOT_GAMEPAD_V0];
    r->Release();
    Log("v0 GetGamepadState = %s+0x%llX", modName, (unsigned long long)((BYTE*)fnV0 - (BYTE*)mod));

    if (!IsInModule(mod, fnV0))
    {
        Log("v0のGetGamepadStateがGameInputの外を指してる。他に何かフックしてる？ やめとく");
        return false;
    }

    //新しい方
    GetGamepadState_t fnNew = NULL;
    void* giNew = NULL;
    if (SUCCEEDED(gi->QueryInterface(IID_IGameInput_New, &giNew)) && giNew != NULL)
    {
        IGameInputReading* r2 = GetPadReading(giNew);
        if (r2 != NULL)
        {
            fnNew = (GetGamepadState_t)GetVtable(r2)[SLOT_GAMEPAD_NEW];
            r2->Release();
        }
    }
    else
    {
        Log("新しいIGameInputが無い(ランタイムが古い？) v0だけフックする");
    }

    if (fnNew != NULL)
    {
        if (!IsInModule(mod, fnNew) || !CheckNewSlot(gi, giNew, fnV0, fnNew))
        {
            Log("新しい方の%d番目がGetGamepadStateっぽくない、フックしない", SLOT_GAMEPAD_NEW);
            fnNew = NULL;
        }
        else
        {
            Log("新しい方 GetGamepadState = +0x%llX (チェックOK)", (unsigned long long)((BYTE*)fnNew - (BYTE*)mod));
        }
    }

    MH_STATUS st = MH_Initialize();
    if (st != MH_OK)
    {
        Log("MH_Initialize失敗 %s", MH_StatusToString(st));
        return false;
    }

    st = MH_CreateHook((void*)fnV0, (void*)&hkGetGamepadState_v0, (void**)&oGetGamepadState_v0);
    Log("MH_CreateHook(v0) %s", MH_StatusToString(st));

    if (fnNew != NULL && fnNew != fnV0)
    {
        st = MH_CreateHook((void*)fnNew, (void*)&hkGetGamepadState_new, (void**)&oGetGamepadState_new);
        Log("MH_CreateHook(new) %s", MH_StatusToString(st));
    }

    st = MH_EnableHook(MH_ALL_HOOKS);
    Log("MH_EnableHook %s", MH_StatusToString(st));
    return st == MH_OK;
}

void PrintStats()
{
    static LONG lastV0 = 0, lastNew = 0;
    LONG v0 = g_callsV0;
    LONG nw = g_callsNew;
    Log("5秒間の呼び出し v0=%ld new=%ld | 右スティック (%.3f, %.3f) -> (%.3f, %.3f)",
        v0 - lastV0, nw - lastNew, g_lastRawRX, g_lastRawRY, g_lastOutRX, g_lastOutRY);
    lastV0 = v0;
    lastNew = nw;
}
