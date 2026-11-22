#include "common.h"
#include "diskspace.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// Disk space check
// ---------------------------------------------------------------------------
void InstallDiskSpaceFix() {
    // 0x4516A0 (eax = bytes wanted): 1 if the drive has them free. It multiplies
    // sectors per cluster, bytes per sector and free clusters with 32-bit imul and
    // compares signed, so a big disk fails it; and it answers 0 when the call fails.
    if (!g_knownExe || !g_cfg.noDiskSpaceCheck) { Logf("disk space check: on\n"); return; }
    static const BYTE yes[6] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };   // mov eax, 1 / ret
    Logf("disk space check: %s\n", PatchBytes(0x4516A0, yes, 6) ? "off" : "patch FAILED");
}
