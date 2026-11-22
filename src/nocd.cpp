#include "common.h"
#include "nocd.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// CD check
// ---------------------------------------------------------------------------
// The startup check asks 0x4B62A0 whether install.win's line 2 is the hidden
// word "remote" (an install type that also turns off parts of the game via
// [0x6FBB54], so it's not used); if not, it wants the CD in a drive. The call
// becomes "mov eax,1" (= remote) at this one site, so only the CD search goes;
// [0x6FBB54] is set by its own call (0x472B38) and still reads "local".
void InstallNoCD() {
    if (!g_knownExe || !g_cfg.noCDCheck) { Logf("CD check: on\n"); return; }
    static const BYTE kMovEax1[5] = { 0xB8, 0x01, 0x00, 0x00, 0x00 };
    bool ok = PatchBytes(hp::cdCheckCall, kMovEax1, sizeof(kMovEax1));
    Logf("CD check: %s\n", ok ? "off" : "patch FAILED");
}
