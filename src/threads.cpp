#include "common.h"
#include "threads.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// Game threads on one core (as SilentPatch's SingleProcAffinity for NFS3): the
// main thread and every thread the game (or eacsnd.dll) creates run on the
// first core; threads of Windows/Wine and of this mod keep all cores. Pinning
// the whole process instead would also put DirectSound's mixer on that core.
// ---------------------------------------------------------------------------
typedef HANDLE (WINAPI *CreateThread_t)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
static CreateThread_t g_oCreateThread = nullptr;
volatile DWORD_PTR g_gameCore = 0;
static std::atomic<unsigned long> g_pinnedThreads{0};
// The game's threads, kept so they can be put back on their core (AffinityWatch)
static CRITICAL_SECTION g_gtLock; static bool g_gtLockInit = false;
static HANDLE g_gameThreads[64]; static int g_gameThreadN = 0;
static void NoteGameThread(HANDLE h) {
    HANDLE d = nullptr;
    if (!g_gtLockInit || !DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, 0, FALSE, DUPLICATE_SAME_ACCESS)) return;
    EnterCriticalSection(&g_gtLock);
    for (int i = 0; i < g_gameThreadN; )          // (threads that have ended make room)
        if (WaitForSingleObject(g_gameThreads[i], 0) == WAIT_OBJECT_0) { CloseHandle(g_gameThreads[i]); g_gameThreads[i] = g_gameThreads[--g_gameThreadN]; }
        else i++;
    if (g_gameThreadN < 64) g_gameThreads[g_gameThreadN++] = d; else CloseHandle(d);
    LeaveCriticalSection(&g_gtLock);
}
static HANDLE WINAPI HkCreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg,
                                    DWORD flags, LPDWORD id) {
    HANDLE h = g_oCreateThread(sa, stack, fn, arg, flags | CREATE_SUSPENDED, id);
    if (h) {
        // (under the lock: AffinityWatch either sees this thread in the list or has finished before it's pinned)
        if (g_gtLockInit) EnterCriticalSection(&g_gtLock);
        if (SetThreadAffinityMask(h, g_gameCore)) g_pinnedThreads++;
        NoteGameThread(h);
        if (g_gtLockInit) LeaveCriticalSection(&g_gtLock);
        if (!(flags & CREATE_SUSPENDED)) ResumeThread(h);
    }
    return h;
}
// Something else can pin the whole process to one core after this mod has
// loaded - a DirectDraw or Direct3D wrapper's "single core" option does that
// when the game starts its renderer. That puts the system's sound threads and
// this mod's own on the game's core after all, and leaves the game's threads
// with a core of ours that the process no longer has. Looked at once a second
// (the log writer's thread): if the process has come down to one core, it gets
// all of them back (widenAffinity), the game's threads go to the core the
// other one picked, and this mod's threads get every core again.
void AffinityWatch() {
    if (!g_gameCore || !g_cfg.widenAffinity || !g_gtLockInit) return;
    DWORD_PTR proc = 0, sys = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) || !proc) return;
    if ((proc & (proc - 1)) || !(sys & (sys - 1))) return;        // (not pinned, or there is only one core)
    static int times = 0;
    EnterCriticalSection(&g_gtLock);              // (no game thread is created and pinned half way through this)
    if (!SetProcessAffinityMask(GetCurrentProcess(), sys)) {
        LeaveCriticalSection(&g_gtLock);
        if (times++ < 3) Logf("threads: the process was pinned to one core (mask %lX) after the mod loaded - couldn't be widened\n", (unsigned long)proc);
        return;
    }
    g_gameCore = proc;
    int n = 0;
    for (int i = 0; i < g_gameThreadN; i++) if (SetThreadAffinityMask(g_gameThreads[i], proc)) n++;
    LeaveCriticalSection(&g_gtLock);
    LONG mods = g_modThreadN;
    for (LONG i = 0; i < mods && i < 8; i++) if (g_modThreads[i]) SetThreadAffinityMask(g_modThreads[i], sys);
    if (times++ < 8) Logf("threads: something pinned the whole process to one core (mask %lX) after the mod loaded - "
                          "all cores given back, the game's %d thread(s) kept on that core\n", (unsigned long)proc, n);
}
bool HookCreateThreadIn(HMODULE mod, const char* what) {
    bool hooked = false;
    ForEachImportIn(mod, "KERNEL32.dll", [&](void** s, const char* nm) {
        if (strcmp(nm, "CreateThread")) return;
        void* old = nullptr;
        if (*s != (void*)&HkCreateThread && PatchPointer(s, (void*)&HkCreateThread, &old)) {
            if (!g_oCreateThread) g_oCreateThread = (CreateThread_t)old;
            hooked = true;
        }
    });
    if (!hooked) Logf("threads: %s - CreateThread import not found\n", what);
    return hooked;
}
void InstallGameThreadsOneCore() {
    if (!g_knownExe || !g_cfg.gameThreadsOneCore) { Logf("threads: all cores\n"); return; }
    DWORD_PTR proc = 0, sys = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) || !proc) { Logf("threads: affinity unknown - all cores\n"); return; }
    // A process that's already on one core (taskset, a launcher's or wrapper's
    // single-core option) has the system's threads on that core too - under
    // Wine that's DirectSound's mixer and the audio driver's threads, which then
    // wait behind the game thread whenever it's busy, and the sound pops. It
    // gets all the system's cores back (which gives them to every thread it has
    // now), and from here on it's the usual arrangement: the game's own threads
    // on the core it was on, the rest free.
    DWORD_PTR was = proc;
    if (!(proc & (proc - 1))) {
        if (!g_cfg.widenAffinity || !(sys & (sys - 1)) || !SetProcessAffinityMask(GetCurrentProcess(), sys)) {
            Logf("threads: the game already has one core (process mask %lX, system mask %lX)%s\n", (unsigned long)proc, (unsigned long)sys,
                 !g_cfg.widenAffinity ? "" : (sys & (sys - 1)) ? " - couldn't be widened" : " - the system shows only one");
            return;
        }
        Logf("threads: the process was on one core (mask %lX) - given all of them (mask %lX)\n", (unsigned long)proc, (unsigned long)sys);
        proc = sys;
    }
    g_gameCore = (was & (was - 1)) ? proc & (~proc + 1) : was;   // (the core it was pinned to, if it was)
    InitializeCriticalSection(&g_gtLock); g_gtLockInit = true;   // (before the hook: HkCreateThread uses it)
    if (!HookCreateThreadIn(GetModuleHandleA(nullptr), "nfs3.exe")) {
        if (was != proc) SetProcessAffinityMask(GetCurrentProcess(), was);   // (can't pin the game's threads: as it was)
        g_gameCore = 0; return;
    }
    SetThreadAffinityMask(GetCurrentThread(), g_gameCore);      // DllMain runs on the game's main thread
    NoteGameThread(GetCurrentThread());
    int core = 0; while (!((g_gameCore >> core) & 1)) core++;
    Logf("threads: the game's threads on core %d, the rest on all cores\n", core);
}
