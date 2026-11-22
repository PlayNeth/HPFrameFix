#include "common.h"
#include "movie.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// Movie stream thread: let it finish exiting before KillThread
// ---------------------------------------------------------------------------
// ph = KillThread's argument (the stream struct's thread handle, on the heap);
// KillThread itself treats 0 and -1 as "no thread" and reads *ph otherwise.
extern "C" void __cdecl WaitStreamThread(HANDLE* ph) {
    if (!ph || ph == (HANDLE*)-1 || !*ph) return;
    if (WaitForSingleObject(*ph, 2000) == WAIT_TIMEOUT)
        LogfAsync("movies: stream thread still running after 2 s - killed as before\n");
}

extern "C" void StreamKillStub();
asm(".text\n"
    // call site of KillThread for the movie stream thread (eax = &handle): wait
    // for the thread to end first, then the original KillThread (return address
    // still the caller's)
    ".globl _StreamKillStub\n"
    "_StreamKillStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    pushl %eax\n"
    "    call _WaitStreamThread\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    popfl\n"
    "    pushl $0x4DF870\n"
    "    ret\n");

void InstallMovieFix() {
    if (!g_knownExe || !g_cfg.fixMovieFreeze) { Logf("movies: original stream thread shutdown\n"); return; }
    bool ok = PatchRel32(hp::streamKillCall, 0xE8, (void*)&StreamKillStub);
    Logf("movies: %s\n", ok ? "stream thread allowed to exit before it's killed" : "patch FAILED");
}
