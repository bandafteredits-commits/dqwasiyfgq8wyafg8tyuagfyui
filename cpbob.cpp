// cpbob.cpp - CheesyProxxy "Sword bob" overlay DLL (loaded into Minecraft Bedrock by cpbob_inject.exe).
// Cosmetic only: it never touches networking, server data or gameplay.
//
// Coexisting with other clients / DLLs (Oderso, ...):
//  - loaded with a plain LoadLibraryW, so it never replaces or unloads anything else
//  - unique module name (cpbob.dll) and unique exports (cpbob_*)
//  - installs NO hook while the toggle is OFF, and removes its hook when switched OFF
//  - no global Windows hooks, no console, no allocations on the render path
//
// Build (x64 Native Tools Command Prompt):  see build.bat   (cl /O2 /MT /EHsc /LD cpbob.cpp)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "cpbob_core.h"

static const wchar_t* kCfgPath = L"C:\\Users\\Public\\CheesyProxxy\\bob.cfg";

static HMODULE       g_self   = nullptr;
static volatile LONG g_on     = 0;   // toggle written by CheesyProxxy.exe
static volatile LONG g_unload = 0;   // set by cpbob_request_unload()
static volatile LONG g_hooked = 0;
static cpbob::BobAnim g_anim;        // only touched from the render thread

// ---------------------------------------------------------------------------------------------
// HOOK POINT.  This is the only part that depends on the exact Minecraft Bedrock build.
// To make the sword actually move, hooks_enable() has to detour the function that builds the
// first-person held-item matrix and call  cpbob_frame(...)  from it, adding the returned value to
// the item's Y translation. Do it CHAIN-SAFE (copy the original prologue to a trampoline and
// call it) so another client that hooked the same function keeps working.
// Until those addresses are filled in, switching the button ON loads the DLL and tracks the
// toggle, but nothing changes on screen.
// ---------------------------------------------------------------------------------------------
static void hooks_enable()  { InterlockedExchange(&g_hooked, 1); }
static void hooks_disable() { InterlockedExchange(&g_hooked, 0); g_anim.reset(); }

// Call this from the hook every frame. Returns the Y offset to ADD to the held item (<= 0 = down).
// hit = nonzero on the frame an attack lands.
extern "C" __declspec(dllexport)
float cpbob_frame(float dtSeconds, int onGround, int inLiquid, int onLadder, int riding,
                  float fallDistance, int hit) {
    cpbob::Settings s;
    s.enabled = (g_on != 0);
    if (!s.enabled) return 0.0f;
    if (hit) {
        cpbob::PlayerState p;
        p.onGround = onGround != 0; p.inLiquid = inLiquid != 0;
        p.onLadder = onLadder != 0; p.riding = riding != 0; p.fallDistance = fallDistance;
        if (cpbob::isCrit(p)) g_anim.trigger();
    }
    return g_anim.step(s, dtSeconds);
}

extern "C" __declspec(dllexport) int  cpbob_active()         { return g_on != 0 && g_hooked != 0; }
extern "C" __declspec(dllexport) int  cpbob_enabled()        { return g_on != 0; }
extern "C" __declspec(dllexport) void cpbob_request_unload() { InterlockedExchange(&g_unload, 1); }

static bool readToggle(bool* on) {
    HANDLE h = CreateFileW(kCfgPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned char buf[16] = {0}; DWORD got = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf), &got, nullptr);
    CloseHandle(h);
    return ok && cpbob::parseCfg(buf, got, on);
}

static DWORD WINAPI worker(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    bool last = false;
    while (!g_unload) {
        bool on = false;
        if (!readToggle(&on)) on = false;          // missing / unreadable / wrong file = OFF
        if (on != last) {
            InterlockedExchange(&g_on, on ? 1 : 0);
            if (on) hooks_enable(); else hooks_disable();   // OFF releases everything
            last = on;
        }
        Sleep(250);                                  // polled 4x/second, like cpcull.dll
    }
    InterlockedExchange(&g_on, 0);
    hooks_disable();
    FreeLibraryAndExitThread(g_self, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = (HMODULE)inst;
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_on, 0);
        InterlockedExchange(&g_unload, 1);
    }
    return TRUE;
}
