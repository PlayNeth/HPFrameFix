#include "common.h"
#include "hud.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "widescreen.h"
#include "menus.h"

// ---------------------------------------------------------------------------
// Race HUD. Its panels sit at the edges of the view rect (0x749758...), but
// every size in it - insets, sprites (0x47F650), digits, the map and lap
// list - is the screen width [0x7CDAC8] times a 1/640 factor, on both axes,
// so on a wide screen everything is wider and taller. Those reads (found by
// way of VEG's widescreen patch, which changes the same ones) get the width
// of a 4:3 screen of the same height instead: min(W, H * 4/3), the same as
// W on 4:3 screens and in the 640x480 menus. Updated after every mode set.
// ---------------------------------------------------------------------------
extern "C" void HudModeStub();
extern "C" void HudRectStub();
extern "C" void HudFracStub();
extern "C" { int g_hrPlayer, g_hrIdx, g_hrW, g_hrH; DWORD g_fnRectBody = 0x480915; }
extern "C" { int g_hudW = 640; DWORD g_fnSetModeBody = 0x4BEF56; }
bool g_hudIn = false;                      // the HUD's sizes come from g_hudW (InstallHud)
extern "C" void MirrorRectStub();
extern "C" void MirrorProjStub();
bool g_pzMeasIn = false;                   // the in-race menus measure their strings by it too (InstallMenus)
extern "C" { extern float g_pzMeasF; }
static const DWORD kHudWidthOps[] = {           // operand of fild/mov [0x7CDAC8]
    0x47F65C, 0x47F675, 0x47F6AA, 0x47F7DF, 0x47F810, 0x47F84A, 0x47F936, 0x47F980,
    0x47F9BA, 0x47FA6D, 0x47FA86, 0x47FAF1, 0x480189, 0x480FCF, 0x480FFD, 0x48101C,
    0x48103A, 0x481074, 0x48109A, 0x4810C2, 0x4810EC, 0x481324, 0x48134B, 0x481370,
    0x481391, 0x4813D2, 0x481408, 0x481436, 0x481478, 0x481AE7, 0x481B00, 0x481B1F,
    0x481B3D, 0x481B74, 0x481B99, 0x481BC1, 0x481BEB, 0x481D0E, 0x481D46, 0x481DE8,
    0x481E20, 0x481E92, 0x481EB7, 0x481EF5, 0x481F3D, 0x484B7B, 0x48723A, 0x487253,
    0x487286, 0x4872B5, 0x48A672, 0x48A716, 0x48AC08, 0x48AE11, 0x48AE89, 0x48AEA2,
    0x48D40C,
};
extern "C" void __cdecl HudUpdate() {
    int w = Rd<int>(0x7CDAC8), h = Rd<int>(0x7CDACC);
    int hw = w;
    if (w > 0 && h > 0) { int w43 = (h * 4 + 1) / 3; if (w43 < w) hw = w43; }
    if (hw <= 0) hw = 640;
    if (hw != g_hudW) LogfAsync("widescreen: HUD sized for %d wide (screen %dx%d)\n", hw, w, h);
    g_hudW = hw;
    g_pzMeasF = (g_pzMeasIn && w > hw) ? (float)w / (float)hw : 1.0f;
}
// The boxes themselves (0x480910, 23 per player at 0x749758 + player * 0x170,
// 16 bytes each: x0, y0, x1, y1) are layout fractions times the view's width
// and height. Their x is redone in 4:3 proportions, kept at the left, the
// right or the middle by where the box's middle is (like High Stakes'
// layout boxes in HSFrameFix); boxes nearly as wide as the view stay as
// they are.
static inline double HudZoneOff(double f0, double f1, double extra);
extern "C" void __cdecl HudRectPost() {
    int W = g_hrW, H = g_hrH, sw = Rd<int>(0x7CDAC8), sh = Rd<int>(0x7CDACC);
    if (W <= 0 || H <= 0 || sw <= 0 || sh <= 0 || g_hrPlayer < 0 || g_hrPlayer > 1 || g_hrIdx < 0 || g_hrIdx >= 23) return;
    double k = sw * 3.0 / (sh * 4.0);                 // screen aspect over 4:3
    if (k <= 1.001) return;
    int* r = (int*)(0x749758 + g_hrPlayer * 0x170 + g_hrIdx * 16);
    double f0 = (double)r[0] / W, f1 = (double)r[2] / W;
    if (f1 - f0 > 0.9) return;
    double w43 = W / k, extra = W - w43;
    double off = HudZoneOff(f0, f1, extra);
    r[0] = (int)lround(w43 * f0 + off);
    r[2] = (int)lround(w43 * f1 + off);
}
// The other way round. The HUD's positions can be changed in the game (the
// in-race menu's "Heads Up Display": every box becomes something to drag),
// and there the game works from the boxes as they are on the screen: each
// drag item starts at its box (0x45CA..), and 0x459DB0 turns an item's
// place back into the layout's fractions - x over the screen width - and
// has the boxes laid out again. With the boxes moved and narrowed above,
// that put the narrowed box into the layout, to be narrowed again: every
// visit squeezed the boxes further, and the result was kept with the player's
// settings (dials stretched tall, on 4:3 screens as well). So the fractions
// 0x459DB0 has worked out ([item+0x44] = left, [item+0x4C] = right) are taken
// back through the rule above before they go into the layout (0x459EBD): laid
// out again, the box is exactly where the item was dragged to.
static inline double HudZoneOff(double f0, double f1, double extra) {
    double c = (f0 + f1) * 0.5;
    return c < 0.4 ? 0.0 : c > 0.6 ? extra : extra * 0.5;
}
extern "C" void __cdecl HudFracInverse(BYTE* item) {
    int sw = Rd<int>(0x7CDAC8), sh = Rd<int>(0x7CDACC);
    if (!item || sw <= 0 || sh <= 0) return;
    double k = sw * 3.0 / (sh * 4.0);
    if (k <= 1.001) return;
    float* fx0 = (float*)(item + 0x44); float* fx1 = (float*)(item + 0x4C);
    double a = *fx0, b = *fx1;                        // the box on the screen, over the screen's width
    if (b - a > 0.9) return;                          // (left as they are, both ways)
    double w43 = sw / k, extra = sw - w43;
    static const double kOff[3] = { 0.5, 0.0, 1.0 };  // middle, left, right
    double g0 = 0, g1 = 0; bool found = false;
    for (int i = 0; i < 3 && !found; i++) {
        double off = extra * kOff[i];
        g0 = (a * sw - off) / w43; g1 = (b * sw - off) / w43;
        found = g1 - g0 <= 0.9 && fabs(HudZoneOff(g0, g1, extra) - off) < 0.5;
    }
    if (!found) {                                     // (a place no layout gives: the nearest side's rule)
        double c = (a + b) * 0.5, off = c < 1.0 / 3 ? 0.0 : c > 2.0 / 3 ? extra : extra * 0.5;
        g0 = (a * sw - off) / w43; g1 = (b * sw - off) / w43;
    }
    *fx0 = (float)g0; *fx1 = (float)g1;
}
// The rear-view mirror. Its frame is one of those boxes, so with the HUD in
// 4:3 proportions it's narrower than before - but the picture inside is a 3D
// view with a rect of its own (0x7CDB48: x, y, w, h for the projection, then
// x, y, w, h for the clip), set by the view rect setup (0x4BE5E0) as fractions
// of the screen, and it stuck out of the frame on both sides. The rect gets the
// same treatment as the boxes, after every setup; and as that gives it the
// shape it has on a 4:3 screen, its projection keeps the game's own angle
// instead of the wider Hor+ one (MirrorProjStub, WsHorPlus). Only for the
// outside views' layout (0): the cockpit's mirror (layout 1) is a hole in the
// cockpit picture, which has a transform of its own, and is left as it is.
extern "C" void __cdecl MirrorRectFix() {
    g_mirrorNarrowed = false;
    // the screen's size as this setup knows it: the main view's rect (0x7CDB88),
    // which it has just written. (The screen rect at 0x7CDAC0, which the HUD
    // code reads later, still holds the last mode's size at this point: on the
    // way into a race that's the menus' 640x480, and nothing was narrowed.)
    int sw = Rd<int>(0x7CDB90), sh = Rd<int>(0x7CDB94);
    {
        static int lw = -1, lh = -1, ll = -1;         // game thread only
        if (sw != lw || sh != lh || g_mirrorLayout != ll) {
            lw = sw; lh = sh; ll = g_mirrorLayout;
            LogfAsync("widescreen: view rects set up for %dx%d, layout %d (screen rect %dx%d, mode %dx%d)\n", sw, sh, g_mirrorLayout,
                      Rd<int>(0x7CDAC8), Rd<int>(0x7CDACC), Rd<int>(0x7CDAE0), Rd<int>(0x7CDAE4));
        }
    }
    if (g_mirrorLayout != 0 || sw <= 0 || sh <= 0 || Rd<int>(0x6FD3B0) == 1) return;   // (split screen: left as it is)
    double k = sw * 3.0 / (sh * 4.0);
    if (k <= 1.001) return;
    int* r = (int*)0x7CDB48;
    double w43 = sw / k, extra = sw - w43;
    for (int part = 0; part < 2; part++, r += 4) {                // projection rect, clip rect
        if (r[2] <= 0) continue;
        double f0 = (double)r[0] / sw, f1 = (double)(r[0] + r[2]) / sw;
        if (f0 < 0 || f1 > 1.0 || f1 - f0 > 0.9) continue;
        double c = (f0 + f1) * 0.5, off = c < 0.4 ? 0.0 : c > 0.6 ? extra : extra * 0.5;
        int x0 = (int)lround(w43 * f0 + off), x1 = (int)lround(w43 * f1 + off);
        if (part == 0) {
            g_mirrorNarrowed = true;
            static int lx = -1, lw = -1;              // game thread only
            if (x0 != lx || x1 - x0 != lw) {
                lx = x0; lw = x1 - x0;
                LogfAsync("widescreen: mirror picture x %d..%d -> %d..%d\n", r[0], r[0] + r[2], x0, x1);
            }
        }
        r[0] = x0; r[2] = x1 - x0;
    }
}
bool InstallHud() {
    for (DWORD a : kHudWidthOps) if (Rd<DWORD>(a) != 0x7CDAC8) { Logf("widescreen: HUD site 0x%06lX differs - HUD left as it is\n", (unsigned long)a); return false; }
    g_hudW = 640;
    DWORD var = (DWORD)&g_hudW;
    bool ok = true;
    for (DWORD a : kHudWidthOps) ok &= PatchBytes(a, &var, 4);
    BYTE f[6] = { 0xDB, 0x05, 0, 0, 0, 0 };          // fld [width float] -> fild [g_hudW]
    memcpy(f + 2, &var, 4);
    ok &= PatchBytes(0x48AB47, f, 6) && PatchBytes(0x48ABFA, f, 6);
    ok &= PatchRel32(0x4BEF50, 0xE9, (void*)&HudModeStub);
    ok &= PatchRel32(0x480910, 0xE9, (void*)&HudRectStub);
    static const BYTE nop2[2] = { 0x90, 0x90 };
    ok &= PatchRel32(0x459EBD, 0xE8, (void*)&HudFracStub) && PatchBytes(0x459EC2, nop2, 2);
    ok &= PatchRel32(0x4BE8C4, 0xE8, (void*)&MirrorRectStub) && PatchRel32(0x4DBE0B, 0xE8, (void*)&MirrorProjStub);
    g_hudIn = ok;
    return ok;
}

asm(".text\n"
    // SetVideoMode (0x4BEF50, eax = mode): its prologue here, then its body; on
    // the way back the HUD width is updated for the new screen
    ".globl _HudModeStub\n"
    "_HudModeStub:\n"
    "    call 1f\n"
    "    pushal\n"
    "    call _HudUpdate\n"
    "    popal\n"
    "    ret\n"
    "1:  pushl %ebx\n"
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    pushl %ebp\n"
    "    jmp *_g_fnSetModeBody\n"
    // HUD positions being changed (0x459EBD: lea edi, [ecx+eax+0x6FBC74]; the
    // item at [ebp-4]): its fractions taken back through the boxes' rule first
    ".globl _HudFracStub\n"
    "_HudFracStub:\n"
    "    pushal\n"
    "    pushl -4(%ebp)\n"
    "    call _HudFracInverse\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    leal 0x6FBC74(%ecx,%eax), %edi\n"
    "    ret\n"
    // HUD box placer (0x480910: eax = player, edx = box, ebx = view w, ecx = h)
    ".globl _HudRectStub\n"
    "_HudRectStub:\n"
    "    movl %eax, _g_hrPlayer\n"
    "    movl %edx, _g_hrIdx\n"
    "    movl %ebx, _g_hrW\n"
    "    movl %ecx, _g_hrH\n"
    "    call 1f\n"
    "    pushal\n"
    "    call _HudRectPost\n"
    "    popal\n"
    "    ret\n"
    "1:  pushl %esi\n"
    "    pushl %edi\n"
    "    pushl %ebp\n"
    "    movl %esp, %ebp\n"
    "    jmp *_g_fnRectBody\n"
    // view rects (0x4BE5E0; edi = its argument, the layout: 0 = outside views,
    // 1 = cockpit), its last call (0x4BE8C4, call 0x4BE3D0): the mirror's rect
    // is set by then, and is narrowed first
    ".globl _MirrorRectStub\n"
    "_MirrorRectStub:\n"
    "    movl %edi, _g_mirrorLayout\n"
    "    pushal\n"
    "    call _MirrorRectFix\n"
    "    popal\n"
    "    jmp *_g_fnViewRectsTail\n"
    // ViewSetup, mirror (0x4DBE0B, call 0x4BED20; eax = rect, edx = fov): the
    // projection is set up with g_wsMirror set, so it keeps the game's own angle
    ".globl _MirrorProjStub\n"
    "_MirrorProjStub:\n"
    "    movl $1, _g_wsMirror\n"
    "    call *_g_fnRectProj\n"
    "    movl $0, _g_wsMirror\n"
    "    ret\n");

// The HUD's panels (0x4844C0) are a frame (eight quads, shown by 0x47FBE0 and
// hidden by 0x47FCB0) and a see-through fill. With the software renderer
// ([0x7A3A58] bit 1) the game leaves the fills out - and for the standings
// list (frame [0x791AD8]) the frame as well: its test at 0x4847B5 jumps past
// both (0x4847BC: jne 0x484941), where the same test for the panel before it
// (0x48472B) only skips the fill. softStandingsBox: that jump goes to the
// frame's own lines (0x4847E5) instead, so the list is boxed like the map.
void InstallSoftStandingsBox() {
    if (!g_knownExe || !g_cfg.softStandingsBox) return;
    DWORD rel = 0x4847E5 - 0x4847C2;
    Logf("hud: standings frame with the software renderer: %s\n", PatchBytes(0x4847BE, &rel, 4) ? "on" : "patch FAILED");
}
