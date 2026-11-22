#include "common.h"
#include "helpers.h"
#include "game.h"
#include "config.h"
#include "log.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
bool PatchBytes(DWORD at, const void* bytes, size_t len) {
    DWORD old;
    if (!VirtualProtect((void*)at, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)at, bytes, len);
    DWORD d; VirtualProtect((void*)at, len, old, &d);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, len);
    return true;
}
bool PatchRel32(DWORD at, BYTE op, void* target) {   // E8 call / E9 jmp
    BYTE b[5]; b[0] = op;
    DWORD rel = (DWORD)target - (at + 5);
    memcpy(b + 1, &rel, 4);
    return PatchBytes(at, b, 5);
}
bool PatchPointer(void** slot, void* nv, void** ov) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    if (ov) *ov = *slot;
    *slot = nv;
    DWORD d; VirtualProtect(slot, sizeof(void*), old, &d);
    return true;
}
// This mod's own threads run on every core the process has, even if they're
// created from a game thread pinned to one core (gameThreadsOneCore).
HANDLE g_modThreads[8]; volatile LONG g_modThreadN = 0;   // (handles of our own: callers close theirs)
HANDLE StartModThread(LPTHREAD_START_ROUTINE fn) {
    HANDLE h = CreateThread(nullptr, 0, fn, nullptr, CREATE_SUSPENDED, nullptr);
    if (!h) return nullptr;
    DWORD_PTR proc = 0, sys = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) && proc) SetThreadAffinityMask(h, proc);
    HANDLE d = nullptr;
    if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        LONG i = InterlockedIncrement(&g_modThreadN) - 1;
        if (i < 8) g_modThreads[i] = d; else CloseHandle(d);
    }
    ResumeThread(h);
    return h;
}

bool g_knownExe = false;
bool CheckExe() {
    if (GetModuleHandleA(nullptr) != (HMODULE)0x400000) {
        Logf("exe check: unexpected image base - disabled\n"); return false;
    }
    int bad = 0, total = 0;
    for (const auto& s : hp::kSigs) {
        total++;
        if (IsBadReadPtr((void*)s.addr, s.len)) {
            Logf("exe check: 0x%06lX unreadable\n", (unsigned long)s.addr); bad++; continue;
        }
        if (!memcmp((void*)s.addr, s.bytes, s.len)) continue;
        bad++;
        char exp[3 * 24 + 1] = {}, got[3 * 24 + 1] = {};
        const BYTE* g = (const BYTE*)s.addr;
        size_t n = s.len < 24 ? s.len : 24;
        for (size_t i = 0; i < n; i++) {
            bool d = g[i] != (BYTE)s.bytes[i];
            snprintf(exp + i * 3, 4, "%02X%c", (BYTE)s.bytes[i], d ? '*' : ' ');
            snprintf(got + i * 3, 4, "%02X%c", g[i], d ? '*' : ' ');
        }
        Logf("exe check: 0x%06lX differs (* = changed byte)\n    expected %s\n    found    %s\n",
             (unsigned long)s.addr, exp, got);
    }
    if (!bad) { Logf("exe check: nfs3.exe matches (%d checks)\n", total); return true; }
    if (g_cfg.exeCheck) {
        Logf("exe check: %d of %d checks differ - not the analysed nfs3.exe, mod disabled "
             "(exeCheck=0 in the ini patches anyway)\n", bad, total);
        return false;
    }
    Logf("exe check: %d of %d checks differ - exeCheck=0, patching anyway\n", bad, total);
    return true;
}
