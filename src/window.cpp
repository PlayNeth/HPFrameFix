#include "common.h"
#include "window.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// Closing the window. The window procedure dispatches through handlers
// registered at 0x4CD7F2..: WM_CLOSE (0x10) -> 0x4CCE40 (PostQuitMessage: the
// window thread stops pumping messages, the game hangs) and WM_SYSCOMMAND
// (0x112) -> 0x4CCE90, which eats SC_CLOSE (and SC_MINIMIZE / MAXIMIZE /
// RESTORE), so Alt+F4 and the title bar / window manager close do nothing. The
// game quits through its own message 0x466 + DestroyWindow, never WM_CLOSE.
// Both handlers (stdcall, 6 arguments) are wrapped: a close ends the process.
// ---------------------------------------------------------------------------
typedef LRESULT (WINAPI *WndHandler_t)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
static void ExitFromOutside(const char* how) {
    Logf("window closed from outside the game (%s) - exiting\n", how);
    if (g_log) fflush(g_log);
    TerminateProcess(GetCurrentProcess(), 0);
}
static LRESULT WINAPI HkWmClose(DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f) {
    ExitFromOutside("WM_CLOSE");
    return ((WndHandler_t)0x4CCE40)(a, b, c, d, e, f);
}
static LRESULT WINAPI HkWmSysCommand(DWORD a, DWORD b, DWORD c, DWORD wparam, DWORD e, DWORD f) {
    if ((wparam & 0xFFF0) == SC_CLOSE) ExitFromOutside("SC_CLOSE");
    return ((WndHandler_t)0x4CCE90)(a, b, c, wparam, e, f);
}
void InstallCloseFix() {
    if (!g_knownExe || !g_cfg.closeExits) { Logf("close: original (closing the window does nothing)\n"); return; }
    DWORD c = (DWORD)&HkWmClose, s = (DWORD)&HkWmSysCommand;
    bool ok = PatchBytes(0x4CD7FF, &c, 4) && PatchBytes(0x4CD83B, &s, 4);
    Logf("close: %s\n", ok ? "closing the window ends the game" : "patch FAILED");
}
