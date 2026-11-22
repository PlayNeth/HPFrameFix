#include "common.h"
#include "detail.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// Car detail. 0x4B9DF0 (car drawing) takes the car's distance and the table
// [0x55FDF4 + carDetail*16] (carDetail = [0x6FBC1C], 0 = high): 4 floats,
// beyond [0] model LOD 1, beyond [1] LOD 2, beyond [2] LOD 3, beyond [3] not
// drawn. 0x4BB5D0 reads the same table (x0.75 in split screen).
// 0x4BBAD0: every car at detail level L (car+0x8B4, 0..3) costs 4-L points;
// while the total is over the budget [0x4B7A50 + carDetail*4] (10, 8, 6) the
// farthest car at 1 goes to 2, else the farthest at 0 goes to 1.
// ---------------------------------------------------------------------------
extern "C" void TexSizeStub();
void InstallCarDetail() {
    if (!g_knownExe) return;
    float* lod = (float*)0x55FDF4;
    bool changed = false;
    for (int lvl = 0; lvl < 3; lvl++) {
        float* t = lod + lvl * 4;
        if (g_cfg.carDrawDistance > 0) { t[3] = (float)g_cfg.carDrawDistance; changed = true; }
        if (g_cfg.carLodScale > 0.0f && g_cfg.carLodScale != 1.0f) {
            for (int i = 0; i < 3; i++) t[i] = t[i] * g_cfg.carLodScale;
            changed = true;
        }
        for (int i = 0; i < 3; i++) if (t[i] > t[3]) t[i] = t[3];
    }
    if (changed)
        Logf("cars: LOD switches (high detail) at %.0f / %.0f / %.0f m, drawn up to %.0f m\n",
             lod[0], lod[1], lod[2], lod[3]);
    else Logf("cars: the game's LOD distances\n");
    if (g_cfg.fullDetailCars) {
        static const int kBudget[3] = { 64, 64, 64 };   // 16 cars x 4 points: never over
        bool ok = PatchBytes(0x4B7A50, kBudget, sizeof(kBudget));
        Logf("cars: %s\n", ok ? "no detail budget (every car keeps its detail)" : "budget patch FAILED");
    }
}

// ---------------------------------------------------------------------------
// Texture sizes. Textures are loaded square, at a power of two: 0x49CEA0 picks
// the largest one <= the image's size, at most the caller's limit and at most
// 256. Callers' limits: 128 for a set loaded with the track (0x41DB10, with
// the loading bar; 0x41DB7B), 256 for the
// player's car and 128 for every other car (0x4B7D30). maxTextureSize raises
// all of them - with the Direct3D renderer. What a renderer says of itself is
// no help: _THRASH_about gives 256 as the largest texture for d3da.dll and
// softtria.dll alike (+0x14 width, +0x20 height), and d3da.dll takes larger
// ones all the same, where softtria.dll draws them as stripes (HD cars). So any
// other renderer keeps the game's 256 (g_texBig, set as the renderer is loaded).
// ---------------------------------------------------------------------------
static int Pow2Floor(int v) { int p = 1; while (p * 2 <= v && p < (1 << 14)) p *= 2; return p; }
static int g_texCfg = 256;
bool g_texBig = false;                     // the renderer DLL loaded last is the Direct3D one (HkLoadLibraryA)
extern "C" int __cdecl TexCap() {
    int cap = g_texBig ? g_texCfg : 256;          // (g_texCfg is 256 or more)
    static int logged = 0;
    if (cap != logged) {
        logged = cap;
        LogfAsync("textures: up to %dx%d%s\n", cap, cap, cap < g_texCfg ? " (this renderer takes no larger ones)" : "");
    }
    return cap;
}
void InstallTextureSize() {
    if (!g_knownExe) return;
    int want = g_cfg.maxTextureSize;
    if (want < 256) want = 256;
    g_texCfg = Pow2Floor(want > 4096 ? 4096 : want);
    if (g_texCfg == 256) {
        if (g_cfg.fullDetailCars) {     // other cars' textures like the player's
            static const BYTE b[5] = { 0xBB, 0x00, 0x01, 0x00, 0x00 };
            Logf("textures: other cars %s\n", PatchBytes(0x4B7D5B, b, 5) ? "256x256 like yours" : "patch FAILED");
        }
        Logf("textures: the game's sizes (256x256 at most)\n");
        return;
    }
    BYTE lim[5] = { 0xBB }; memcpy(lim + 1, &g_texCfg, 4);
    bool ok = PatchRel32(0x49CEB9, 0xE9, (void*)&TexSizeStub) &&
              PatchBytes(0x41DB7B, lim, 5) && PatchBytes(0x4B7D54, lim, 5) &&
              (!g_cfg.fullDetailCars || PatchBytes(0x4B7D5B, lim, 5));
    Logf("textures: up to %dx%d (track and %s cars too; 256 with any renderer but Direct3D): %s\n",
         g_texCfg, g_texCfg, g_cfg.fullDetailCars ? "all" : "your", ok ? "on" : "patch FAILED");
}

asm(".text\n"
    // texture size (0x49CEB9, jmp from "cmp ebx,0x100"): eax = size struct,
    // ebx = the image's size, already <= the caller's limit. Largest power of
    // two <= ebx, at most TexCap() (the game: at most 256), into [eax], [eax+4].
    ".globl _TexSizeStub\n"
    "_TexSizeStub:\n"
    "    pushl %ecx\n"
    "    pushal\n"
    "    call _TexCap\n"
    "    movl %eax, 24(%esp)\n"      // -> ecx
    "    popal\n"
    "1:  cmpl $1, %ecx\n"
    "    jbe 2f\n"
    "    cmpl %ebx, %ecx\n"
    "    jle 2f\n"
    "    shrl $1, %ecx\n"
    "    jmp 1b\n"
    "2:  movl %ecx, 4(%eax)\n"
    "    movl %ecx, (%eax)\n"
    "    popl %ecx\n"
    "    pushl $0x49CF75\n"
    "    ret\n");
