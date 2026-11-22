#include "common.h"
#include "loading.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "timing.h"

// ---------------------------------------------------------------------------
// Loading bar throttle
// ---------------------------------------------------------------------------
extern "C" { int g_loadDraw = 1; DWORD g_fnPoll = 0x4E7630; int g_wsLoading = 0; DWORD g_xfTarget = 0; }
static LONGLONG g_loadLastCall = 0, g_loadLastDraw = 0, g_loadStart = 0;
static unsigned long g_loadCalls = 0, g_loadDraws = 0;
static bool g_loadActive = false;
CRITICAL_SECTION g_loadLock;

extern "C" void __cdecl LoadBarDecide(float pct) {
    LONGLONG now = Qpc();
    EnterCriticalSection(&g_loadLock);
    if (!g_loadActive || now - g_loadLastCall > 2 * g_qpf.QuadPart) {   // new load
        g_loadActive = true; g_loadStart = now; g_loadCalls = 0; g_loadDraws = 0; g_loadLastDraw = 0;
    }
    g_loadLastCall = now;
    g_loadCalls++;
    LONGLONG minGap = (LONGLONG)g_cfg.loadScreenRenderMs * g_qpf.QuadPart / 1000;
    bool draw = g_cfg.loadScreenRenderMs <= 0 || !g_loadLastDraw || pct >= 100.0f ||
                now - g_loadLastDraw >= minGap;
    if (draw) { g_loadLastDraw = now; g_loadDraws++; }
    if (!g_cfg.speedUpLoading) draw = true;      // stub there only for wsLoading
    g_loadDraw = draw ? 1 : 0;
    LeaveCriticalSection(&g_loadLock);
}
void LoadStatsTick() {                 // called by the stats thread
    EnterCriticalSection(&g_loadLock);
    if (g_loadActive && Qpc() - g_loadLastCall > 2 * g_qpf.QuadPart) {
        Logf("load: %lu loading-bar updates, %lu drawn, first->last %.0f ms\n", g_loadCalls,
             g_loadDraws, QpcMs(g_loadLastCall - g_loadStart));
        g_loadActive = false;
    }
    LeaveCriticalSection(&g_loadLock);
}

extern "C" void LoadBarStub();
asm(".text\n"
    // entry of the loading bar (stack: ret, float percent)
    ".globl _LoadBarStub\n"
    "_LoadBarStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    pushl 40(%esp)\n"            // percent
    "    call _LoadBarDecide\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    popfl\n"
    "    cmpl $0, _g_loadDraw\n"
    "    jne 3f\n"
    "    pushal\n"                    // skipped: keep the callback poll it starts with
    "    xorl %eax, %eax\n"
    "    call *_g_fnPoll\n"
    "    popal\n"
    "    ret $4\n"
    "3:  cmpl $0, _g_wsLoading\n"
    "    jne 5f\n"
    "4:  pushl %ebx\n"                 // original prologue, then continue
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    pushl %ebp\n"
    "    pushl $0x495236\n"
    "    ret\n"
    "5:  pushal\n"                    // drawn 4:3: the bar inside XfBegin/XfEnd
    "    pushl $1\n"
    "    call _XfBegin\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    pushl 4(%esp)\n"              // percent again, for the call below
    "    call 4b\n"                   // the original (ret 4 drops the copy)
    "    pushal\n"
    "    call _XfEnd\n"
    "    popal\n"
    "    ret $4\n");

void InstallLoading() {
    bool ws = g_knownExe && g_cfg.widescreen && g_cfg.wsLoading;
    if (!g_knownExe || (!g_cfg.speedUpLoading && !ws)) { Logf("loading: original\n"); return; }
    bool ok = PatchRel32(hp::fnLoadBar, 0xE9, (void*)&LoadBarStub);
    if (ok && ws) g_wsLoading = 1;
    if (g_cfg.speedUpLoading)
        Logf("loading: %s (loading bar redrawn at most every %d ms)\n",
             ok ? "sped up" : "patch FAILED", g_cfg.loadScreenRenderMs);
}
