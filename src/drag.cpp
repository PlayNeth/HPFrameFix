#include "common.h"
#include "drag.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// Air drag. 0x4A2260, once per sim step for each car (from 0x4A2871): while
// the car is in the air (word car+0x164 != 0; set by 0x42F4A2, cleared on
// landing by 0x4A2630) it works out a drag from the car's horizontal velocity
// (car+0xA4 x, +0xAC z) squared, weighted by how the car's axes (car+0xC0..)
// line up with it, times the float at [0x53C8B4] (0.03125), and takes it off
// both components (0x4A25E2, 0x4A260E). That float is read only there
// (0x4A25A3), so that instruction reads airDrag x 0.03125 from here instead.
// ---------------------------------------------------------------------------
static float g_airDragK = 0.03125f;
void InstallAirDrag() {
    if (!g_knownExe) return;
    float k = g_cfg.airDrag;
    if (!(k >= 0.0f)) k = 0.0f;                   // (negative or NaN: none)
    if (k > 10.0f) k = 10.0f;
    if (k == 1.0f) { Logf("air drag: the game's\n"); return; }
    g_airDragK = 0.03125f * k;
    BYTE b[6] = { 0xD9, 0x05 };                   // fld dword ptr [&g_airDragK]
    float* at = &g_airDragK; memcpy(b + 2, &at, 4);
    Logf("air drag: x%.2f: %s\n", k, PatchBytes(0x4A25A3, b, 6) ? "on" : "patch FAILED");
}
