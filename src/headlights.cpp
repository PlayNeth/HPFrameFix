#include "common.h"
#include "headlights.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// Projected headlights (the game's projtex [0x6FBC3C] = 1, at night: [0x7A3A60]
// set). Each car's light (car+0x664, set up at 0x42D2xx) gets, in that mode,
// a distance where it starts to fade and one where it's gone, and a colour:
// the player's 10 m / 40 m / 0xB0B0B0B0 (immediates at 0x42D26D / 0x42D277 /
// 0x42D281), the others' 20 m / 60 m / 0xE0E0E0E0 (0x42D2E0 / 0x42D2EA /
// 0x42D2F4). Their light is a pass of its own over the track, the light's
// texture times that colour, mapped by the light's frustum: 0x41A550 puts
// every vertex into the light's space (x, y, z at [ebp-0x1C..-0x14] once
// 0x41A603 has added the light's position) and works out from them the
// texture's u, v ((x/z + 1) / 2), which side of the frustum it lies on, and
// its brightness. projLightReach scales both distances, projLightBrightness
// the colours, and projLightWidth widens the beam: x and y are divided by it
// right there (0x41A606, "mov eax,[ebp-0x14] / test eax,0x7FFFFFFF", whose
// work the stub then does), so the texture, the frustum sides and the
// brightness all see a wider light - only in this mode.
// ---------------------------------------------------------------------------
extern "C" { float g_projInvW = 1.0f; }
extern "C" void ProjWidthStub();
asm(
    ".globl _ProjWidthStub\n"
    "_ProjWidthStub:\n"
    "    cmpl $1, 0x6FBC3C\n"            // projected headlights
    "    jne 1f\n"
    "    cmpl $0, 0x7A3A60\n"            // at night
    "    je 1f\n"
    "    flds -0x1C(%ebp)\n"
    "    fmuls _g_projInvW\n"
    "    fstps -0x1C(%ebp)\n"
    "    flds -0x18(%ebp)\n"
    "    fmuls _g_projInvW\n"
    "    fstps -0x18(%ebp)\n"
    "1:  movl -0x14(%ebp), %eax\n"
    "    testl $0x7FFFFFFF, %eax\n"
    "    ret\n");
static DWORD ScaleColour(DWORD c, float k) {
    DWORD r = 0;
    for (int i = 0; i < 32; i += 8) {
        float v = ((c >> i) & 0xFF) * k + 0.5f;
        r |= (DWORD)(v > 255 ? 255 : v < 0 ? 0 : v) << i;
    }
    return r;
}
void InstallProjLights() {
    if (!g_knownExe) return;
    float reach = g_cfg.projLightReach, bright = g_cfg.projLightBrightness, width = g_cfg.projLightWidth;
    reach  = !(reach > 0) ? 1 : reach > 4 ? 4 : reach;
    bright = !(bright >= 0) ? 1 : bright > 2 ? 2 : bright;
    width  = !(width > 0) ? 1 : width > 3 ? 3 : width;
    bool ok = true;
    if (reach != 1.0f) {
        static const DWORD kDist[4] = { 0x42D26D, 0x42D277, 0x42D2E0, 0x42D2EA };
        for (DWORD a : kDist) { float v = Rd<float>(a) * reach; ok &= PatchBytes(a, &v, 4); }
    }
    if (bright != 1.0f) {
        static const DWORD kCol[2] = { 0x42D281, 0x42D2F4 };
        for (DWORD a : kCol) { DWORD v = ScaleColour(Rd<DWORD>(a), bright); ok &= PatchBytes(a, &v, 4); }
    }
    if (width != 1.0f) {
        g_projInvW = 1.0f / width;
        static const BYTE nop3[3] = { 0x90, 0x90, 0x90 };
        ok &= PatchRel32(0x41A606, 0xE8, (void*)&ProjWidthStub) && PatchBytes(0x41A60B, nop3, 3);
    }
    if (reach == 1.0f && bright == 1.0f && width == 1.0f) { Logf("projected headlights: the game's\n"); return; }
    Logf("projected headlights: reach x%.2f, brightness x%.2f, width x%.2f: %s\n", reach, bright, width,
         ok ? "on" : "patch FAILED");
}
