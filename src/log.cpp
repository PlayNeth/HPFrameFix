#include "common.h"
#include "log.h"
#include "config.h"
#include "sound.h"
#include "threads.h"

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
FILE* g_log = nullptr;
CRITICAL_SECTION g_logLock;
bool g_logLockInit = false;

void Logf(const char* fmt, ...) {
    if (!g_log) return;
    if (g_logLockInit) EnterCriticalSection(&g_logLock);
    va_list ap; va_start(ap, fmt);
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
    if (g_logLockInit) LeaveCriticalSection(&g_logLock);
}

// Log lines from the game thread go through a queue and are written by a
// background thread, so the game thread never waits on the disk.
CRITICAL_SECTION g_qLock;
HANDLE g_qEvent = nullptr;
static char g_q[256][192];
static int g_qHead = 0, g_qTail = 0;
void LogfAsync(const char* fmt, ...) {
    if (!g_log || !g_qEvent) return;
    char buf[192];
    SYSTEMTIME st; GetLocalTime(&st);
    int n = snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond,
                     st.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    if (m >= (int)sizeof(buf) - n) buf[sizeof(buf) - 2] = '\n';   // cut short: still one line
    EnterCriticalSection(&g_qLock);
    int next = (g_qHead + 1) % 256;
    if (next != g_qTail) { memcpy(g_q[g_qHead], buf, sizeof(buf)); g_qHead = next; }
    LeaveCriticalSection(&g_qLock);
    SetEvent(g_qEvent);
}
void DrainLogQueue() {
    for (;;) {
        char line[192];
        EnterCriticalSection(&g_qLock);
        bool any = g_qTail != g_qHead;
        if (any) { memcpy(line, g_q[g_qTail], sizeof(line)); g_qTail = (g_qTail + 1) % 256; }
        LeaveCriticalSection(&g_qLock);
        if (!any) break;
        if (!g_log) continue;
        if (g_logLockInit) EnterCriticalSection(&g_logLock);
        fputs(line, g_log); fflush(g_log);
        if (g_logLockInit) LeaveCriticalSection(&g_logLock);
    }
}
DWORD WINAPI WriterThread(LPVOID) {
    for (;;) { WaitForSingleObject(g_qEvent, 1000); AffinityWatch(); DrainLogQueue(); CaptureFlush(); }   // (at least once a second)
}

void OpenLog() {
    if (!g_cfg.logToFile) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sHPFrameFix.log", g_asiDir);
    g_log = fopen(path, "w");
    Logf("HPFrameFix v" HPFF_VERSION " | interpolate=%d preciseTicks=%d targetFPS=%d "
         "maxStepDistance=%.1f maxStepAngle=%.0f interpDelay=%.2f unlockMenus=%d menuTargetFPS=%d "
         "menuMaxFPS=%d\n", g_cfg.interpolate, g_cfg.preciseTicks, g_cfg.targetFPS,
         g_cfg.maxStepDistance, g_cfg.maxStepAngle, g_cfg.interpDelay, g_cfg.unlockMenus,
         g_cfg.menuTargetFPS, g_cfg.menuMaxFPS);
}
