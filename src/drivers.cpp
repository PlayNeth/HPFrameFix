#include "common.h"
#include "drivers.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "sound.h"
#include "detail.h"
#include "widescreen.h"
#include "portable.h"
#include "threads.h"

// ---------------------------------------------------------------------------
// Renderer DLLs from the drivers folder
// ---------------------------------------------------------------------------
typedef HMODULE (WINAPI *LoadLibraryA_t)(LPCSTR);
static LoadLibraryA_t g_oLoadLibraryA = nullptr;
static char g_driversDir[MAX_PATH] = {};     // with a trailing backslash
static bool g_driversExist = false;

static bool IsRendererDll(LPCSTR name) {
    for (const char* d : kThrashDlls) if (!_stricmp(name, d)) return true;
    return false;
}
// eacsnd.dll (the game's DirectSound driver, RVAs below) has three ways to play:
//  - primary: it mixes straight into DirectSound's primary buffer
//    (DSSCL_WRITEPRIMARY), 20 ms ahead of the play cursor. Used unless
//    DirectSound reports an emulated driver.
//  - emulated (DSCAPS_EMULDRIVER): a 1.9 s looping secondary buffer written
//    8192 samples (~370 ms) ahead: no crackling, but sound lags far behind.
//  - secondary: what it falls back to when it can't get the primary buffer: a
//    0.4 s secondary buffer written 20 ms ahead, with the same adaptive lead as
//    the primary mode (it grows if the play cursor catches up).
// The primary and secondary modes are left to the game's choice: with the
// lead fixed below and the system's sound threads off the game's core
// (widenAffinity), the primary mode plays cleanly. Their lead in ms is the
// immediate at 0x1CA6 ("mov [lead], 20"): soundLatencyMs.
// The crackle: the adaptive lead (0x21E4, run by every serve outside the
// emulated mode) grows the lead by the shortfall after an underrun, but also
// shrinks it: once 3000 serves (~23 s) have passed without an underrun, every
// 10th serve takes 16 samples off the lead as long as the least data left
// ahead of the play cursor over those 10 serves was over 16 samples. So
// whatever the starting lead, it's worn down to about one serve interval
// (7.8 ms) plus 16 samples, and then any late serve - a menu switch's ~30 ms
// gap, or a system mixer reading in 10 ms blocks - lets the play cursor
// overtake the data: it plays the buffer's old contents from one lap back, a
// pop, and the lead grows, only to be worn down again. "cmp [min left],16 /
// jle keep" at 0x2266 -> always jmp: the lead never shrinks, so it stays at
// soundLatencyMs and only grows after an underrun.
static void PatchEacsnd(HMODULE h) {
    static HMODULE done = nullptr;
    if (!h || h == done) return;
    done = h;
    BYTE* b = (BYTE*)h;
    BYTE* lead  = b + 0x1CA0;    // C7 05 <&lead> 14 00 00 00
    BYTE* keep  = b + 0x2266;    // 83 3D <&min left> 10 7E 56
    bool known = !IsBadReadPtr(lead, 10) && !IsBadReadPtr(keep, 9) &&
        keep[0] == 0x83 && keep[1] == 0x3D && *(DWORD*)(keep + 2) == (DWORD)b + 0xA8DC &&
        keep[6] == 0x10 && keep[7] == 0x7E && keep[8] == 0x56 &&
        lead[0] == 0xC7 && lead[1] == 0x05 && *(DWORD*)(lead + 2) == (DWORD)b + 0xA8CC &&
        *(DWORD*)(lead + 6) == 20;
    if (!known) { LogfAsync("sound: eacsnd.dll isn't the known version - left as is\n"); return; }
    static const BYTE jmp = 0xEB;
    bool ok = PatchBytes((DWORD)(keep + 7), &jmp, 1);
    DWORD ms = (DWORD)(g_cfg.soundLatencyMs < 5 ? 5 : g_cfg.soundLatencyMs > 200 ? 200 : g_cfg.soundLatencyMs);
    if (ok && ms != 20) ok = PatchBytes((DWORD)(lead + 6), &ms, 4);
    if (!ok) LogfAsync("sound: patch FAILED\n");
    else {
        g_eacBuf = (void**)(b + 0xA8A8);
        if (CaptureSeconds() > 0) g_eacMixSlot = (void**)(b + 0xA904);   // (the mix is only looked at for a capture)
        g_eacLead = (volatile LONG*)(b + 0xA8CC);
        g_eacRate = (volatile LONG*)(b + 0xA900);
        LogfAsync("sound: eacsnd.dll writes %lu ms ahead of playback (the lead only grows)\n", (unsigned long)ms);
    }
}
static bool IsEacsnd(LPCSTR name) {
    const char* b = name;
    for (const char* c = name; *c; c++) if (*c == '\\' || *c == '/' || *c == ':') b = c + 1;
    return !_stricmp(b, "eacsnd.dll") || !_stricmp(b, "eacsnd");
}

static HMODULE WINAPI HkLoadLibraryA(LPCSTR name) {
    if (name && IsEacsnd(name)) {
        HMODULE h = g_oLoadLibraryA(name);
        if (h && g_cfg.fixSound) PatchEacsnd(h);
        if (h && g_gameCore) {                       // its threads are the game's too
            static HMODULE hooked = nullptr;
            if (h != hooked && HookCreateThreadIn(h, "eacsnd.dll")) { hooked = h; LogfAsync("threads: eacsnd.dll's threads too\n"); }
        }
        return h;
    }
    if (name && IsRendererDll(name)) {
        const char* b = name;
        for (const char* q = name; *q; q++) if (*q == '\\' || *q == '/' || *q == ':') b = q + 1;
        g_softRenderer = !_strnicmp(b, "soft", 4);
        g_texBig = !_strnicmp(b, "d3d", 3);
    }
    if (name && g_driversDir[0] && !strpbrk(name, "\\/:") && IsRendererDll(name)) {
        static char seen[8][24]; static int nSeen = 0;            // log each DLL once
        bool first = true;
        for (int i = 0; i < nSeen; i++) if (!_stricmp(seen[i], name)) first = false;
        if (first && nSeen < 8) lstrcpynA(seen[nSeen++], name, 24);
        char path[MAX_PATH];
        if (snprintf(path, sizeof(path), "%s%s", g_driversDir, name) < (int)sizeof(path) &&
            GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
            // The DLL's own dependencies (glide2x.dll...) are looked for in the game
            // folder first, as before, then in the drivers folder.
            char prev[MAX_PATH]; DWORD pl = GetDllDirectoryA(sizeof(prev), prev);
            SetDllDirectoryA(g_driversDir);
            HMODULE h = g_oLoadLibraryA(path);
            DWORD err = h ? 0 : GetLastError();
            SetDllDirectoryA(pl && pl < sizeof(prev) ? prev : nullptr);
            if (first) LogfAsync("drivers: %s %s (error %lu)\n", path, h ? "loaded" : "FAILED", (unsigned long)err);
            if (h) return h;
        } else if (first && g_driversExist) {
            LogfAsync("drivers: %s not in %s - game folder\n", name, g_driversDir);
        }
    }
    return g_oLoadLibraryA(name);
}
// The game's LoadLibraryA: renderer DLLs from the drivers folder, eacsnd.dll patched.
void InstallLoadLibraryHook() {
    if (!g_knownExe) return;
    if (g_cfg.driversFolder[0]) {
        const char* f = g_cfg.driversFolder;
        bool absolute = strchr(f, ':') || (f[0] == '\\' && f[1] == '\\');
        int n = snprintf(g_driversDir, sizeof(g_driversDir), "%s%s", absolute ? "" : g_gameDir, f);
        if (n <= 0 || n >= (int)sizeof(g_driversDir) - 1) { Logf("drivers: path too long - game folder\n"); g_driversDir[0] = 0; }
        else {
            while (n && (g_driversDir[n - 1] == '\\' || g_driversDir[n - 1] == '/')) g_driversDir[--n] = 0;
            g_driversDir[n++] = '\\'; g_driversDir[n] = 0;
        }
    }
    if (!g_driversDir[0] && !g_cfg.fixSound && !g_gameCore) { Logf("drivers: game folder\nsound: original\n"); return; }
    void** slot = nullptr;
    ForEachImport("KERNEL32.dll", [&](void** s, const char* nm) { if (!strcmp(nm, "LoadLibraryA")) slot = s; });
    if (!slot || !PatchPointer(slot, (void*)&HkLoadLibraryA, (void**)&g_oLoadLibraryA)) {
        g_driversDir[0] = 0;
        Logf("drivers / sound: patch FAILED\n"); return;
    }
    if (g_driversDir[0]) {
        DWORD a = GetFileAttributesA(g_driversDir);
        g_driversExist = a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
        Logf("drivers: %s%s\n", g_driversDir, g_driversExist ? "" : " (not there - game folder)");
    } else Logf("drivers: game folder\n");
    if (g_cfg.fixSound) Logf("sound: eacsnd.dll patched when the game loads it\n");
    else Logf("sound: original\n");
}
