#include "common.h"
#include "fog.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "interp.h"
#include "speedfx.h"
#include "nightlamps.h"
#include "widescreen.h"
#include "menus.h"

// ---------------------------------------------------------------------------
// Direct3D fog. The driver setup (0x4B59C0, one case per renderer; [0x7A3A58]
// = 1 << renderer index) sets the game's fog mode [0x7A3A64]: 4 for the Glide
// drivers, 0 for the rest. With 0 the game fades distant colours itself
// (0x479860); otherwise it sends fog colour (THRASH state 15), density (14)
// and, per polygon, fog mode (21) = [0x7A3A64] or 0 (cached in [0x554E5C]).
//
// On Glide (voodoo2a) the vertex's rhw is Glide's oow, and the fog table
// (type 4) is fog(w) = (1 - e^(-w/v)) / (1 - e^(-w63/v)) for w = 1/oow, v =
// the density value. Density 0 = no fog (the game sends it at every track
// load, 0x470B90, and for clear weather).
//
// d3da.dll has D3D vertex fog: state 21 = 1 turns it on (FOGENABLE), 0 off,
// 15 sets FOGCOLOR; as it copies each vertex it writes specular =
// table[round(z * 255)] << 24 (255 = no fog) - by z, not w, in 256 steps.
// Its table builder (21 = 4) doesn't match this game's z, and density 0 is
// 1/0 = infinitely thick to it.
//
// So for d3da: the game's mode becomes 4 like Glide's; its setstate is
// wrapped (21 = 4 -> 21 = 1, or 0 while the density is 0; 14 is kept here);
// d3da's 16 specular writes are taken out (its own lookup does nothing then);
// and the draw calls are wrapped to give every vertex, while fog is on,
// Glide's fog for its own w = 1/rhw. Same curve as on Glide, per vertex.
// ---------------------------------------------------------------------------
extern "C" void D3dFogStub();
typedef void (WINAPI *TDraw2_t)(void*, void*);
typedef void (WINAPI *TMesh_t)(int, void*, DWORD*);
static TSetState_t g_oSetState = nullptr;
// the renderer's own setstate and the wrapper the game gets for it (the menus'
// draw wrappers further down: MnSetState notes the texture in use)
TSetState_t g_mnSetState = nullptr;
static TDraw4_t g_fogQuad; static TDraw3_t g_fogTri; static TDrawN_t g_fogFan; static TDraw2_t g_fogLine;
static TMesh_t  g_fogQuadM, g_fogTriM, g_fogLineM;
static int    g_fogValue = 0;               // game's density value (0 = no fog)
static bool   g_fogOn = false;              // polygons drawn with fog right now
static double g_fogD = 0, g_fogNorm = 1;    // density, Glide's normalisation
// specular alpha (255 = clear) for a vertex: Glide's table value for w = 1/rhw
static inline DWORD FogSpec(const BYTE* vtx) {
    float rhw = ((const float*)vtx)[3];
    if (!(rhw > 0.0f) || g_fogD <= 0) return 0xFF000000u;
    double f = (1.0 - exp(-g_fogD / rhw)) / g_fogNorm;
    int a = 255 - (int)(f * 255.0 + 0.5);
    a = a < 0 ? 0 : a > 255 ? 255 : a;
    return (DWORD)a << 24;
}
static inline void FogCopy(BYTE* dst, const void* src) {
    memcpy(dst, src, 32); *(DWORD*)(dst + 0x14) = FogSpec(dst);
}
static void WINAPI FogQuad(void* a, void* b, void* c, void* d) {
    if (!g_fogOn) return g_fogQuad(a, b, c, d);
    BYTE v[4][32]; FogCopy(v[0], a); FogCopy(v[1], b); FogCopy(v[2], c); FogCopy(v[3], d);
    g_fogQuad(v[0], v[1], v[2], v[3]);
}
static void WINAPI FogTri(void* a, void* b, void* c) {
    if (!g_fogOn) return g_fogTri(a, b, c);
    BYTE v[3][32]; FogCopy(v[0], a); FogCopy(v[1], b); FogCopy(v[2], c);
    g_fogTri(v[0], v[1], v[2]);
}
static void WINAPI FogLine(void* a, void* b) {
    if (!g_fogOn) return g_fogLine(a, b);
    BYTE v[2][32]; FogCopy(v[0], a); FogCopy(v[1], b);
    g_fogLine(v[0], v[1]);
}
// growing vertex buffer (game thread only)
static BYTE* g_fogBuf = nullptr; static int g_fogBufN = 0;
static BYTE* FogBuf(int n) {
    if (n > g_fogBufN) {
        free(g_fogBuf); g_fogBufN = n + 1024;
        g_fogBuf = (BYTE*)malloc((size_t)g_fogBufN * 32);
        if (!g_fogBuf) g_fogBufN = 0;
    }
    return g_fogBuf;
}
// drawtrifan(triangles, vertices): triangles + 2 vertices in a row
static void WINAPI FogFan(int tris, void* verts) {
    int n = tris + 2;
    BYTE* b = (g_fogOn && tris > 0 && verts) ? FogBuf(n) : nullptr;
    if (!b) return g_fogFan(tris, verts);
    for (int i = 0; i < n; i++) FogCopy(b + i * 32, (BYTE*)verts + i * 32);
    g_fogFan(tris, b);
}
// meshes: (count, vertices, DWORD indices: count * per)
static void FogMesh(TMesh_t f, int n, void* verts, DWORD* idx, int per) {
    if (!g_fogOn || n <= 0 || !verts || !idx) return f(n, verts, idx);
    DWORD mx = 0;
    for (int i = 0; i < n * per; i++) if (idx[i] > mx) mx = idx[i];
    BYTE* b = mx < 65536 ? FogBuf((int)mx + 1) : nullptr;
    if (!b) return f(n, verts, idx);
    for (DWORD i = 0; i <= mx; i++) FogCopy(b + i * 32, (BYTE*)verts + i * 32);
    f(n, b, idx);
}
static void WINAPI FogQuadM(int n, void* v, DWORD* i) { FogMesh(g_fogQuadM, n, v, i, 4); }
static void WINAPI FogTriM(int n, void* v, DWORD* i)  { FogMesh(g_fogTriM, n, v, i, 3); }
static void WINAPI FogLineM(int n, void* v, DWORD* i) { FogMesh(g_fogLineM, n, v, i, 2); }

// The cockpit picture and the loading screen (drawn between XfBegin and
// XfEnd) are 2D: on Glide their w is tiny so fog never touches them. Fog is
// off while they're drawn and what the game asks for meanwhile is applied
// afterwards.
static bool g_fogPaused = false, g_fogWanted = false;
static int WINAPI FogSetState(int s, int v);
void FogPause(bool pause) {
    if (!g_oSetState || pause == g_fogPaused) return;
    if (pause) {
        g_fogWanted = g_fogOn;
        g_fogPaused = true;
        if (g_fogOn) { g_fogOn = false; g_oSetState(21, 0); }
    } else {
        g_fogPaused = false;
        if (g_fogWanted) FogSetState(21, 4);
    }
}
static int WINAPI FogSetState(int s, int v) {
    if (g_fogPaused && s == 21) { g_fogWanted = v == 4; return 1; }
    if (s == 21 && v == 4) {
        if (g_fogValue <= 0) { g_fogOn = false; return g_oSetState(21, 0); }
        g_fogOn = true;
        return g_oSetState(21, 1);
    }
    if (s == 21) g_fogOn = false;
    if (s == 14) {
        if (v != g_fogValue) {
            g_fogValue = v;
            g_fogD = v > 0 ? 1.0 / v : 0.0;
            g_fogNorm = g_fogD > 0 ? 1.0 - exp(-52428.8 * g_fogD) : 1.0;   // w63 = 2^18 / 5
            if (g_fogNorm <= 0) g_fogNorm = 1;
        }
        return 1;
    }
    return g_oSetState(s, v);
}
// d3da.dll as analysed: setstate at +0x2B58, and the 16 places that write the
// table's fog into a vertex's specular (mov [..+0x14], reg)
struct FogSite { DWORD rva; BYTE len; BYTE b[4]; };
static const FogSite kFogSites[] = {
    { 0x43C5, 4, { 0x89, 0x4C, 0x06, 0x14 } }, { 0x4443, 4, { 0x89, 0x54, 0x06, 0x14 } },
    { 0x44C9, 4, { 0x89, 0x54, 0x01, 0x14 } }, { 0x459C, 4, { 0x89, 0x4C, 0x06, 0x14 } },
    { 0x4632, 4, { 0x89, 0x4C, 0x07, 0x14 } }, { 0x46D0, 3, { 0x89, 0x48, 0x14 } },
    { 0x47A9, 4, { 0x89, 0x74, 0x01, 0x14 } }, { 0x4813, 4, { 0x89, 0x74, 0x01, 0x14 } },
    { 0x4882, 4, { 0x89, 0x44, 0x29, 0x14 } }, { 0x48ED, 4, { 0x89, 0x74, 0x01, 0x14 } },
    { 0x49FF, 4, { 0x89, 0x4C, 0x06, 0x14 } }, { 0x4A62, 4, { 0x89, 0x4C, 0x06, 0x14 } },
    { 0x4AC5, 4, { 0x89, 0x4C, 0x06, 0x14 } }, { 0x4B1E, 4, { 0x89, 0x5C, 0x01, 0x14 } },
    { 0x4CCA, 3, { 0x89, 0x42, 0xF4 } },       { 0x4D89, 4, { 0x89, 0x44, 0x1E, 0x14 } },
};
static bool FogPatchDriver(DWORD setstate) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)setstate, &m) || !m) return false;
    DWORD base = (DWORD)m;
    if (setstate - base != 0x2B58) return false;
    for (const FogSite& s : kFogSites)
        if (!Readable(base + s.rva, s.len) || memcmp((void*)(base + s.rva), s.b, s.len)) return false;
    for (const FogSite& s : kFogSites) {
        static const BYTE nop[4] = { 0x90, 0x90, 0x90, 0x90 };
        if (!PatchBytes(base + s.rva, nop, s.len)) return false;
    }
    return true;
}
extern "C" void __cdecl D3dFogInit() {
    *(volatile DWORD*)0x7A3A64 = 0;
    SpeedFxDriverInit();
    if (!g_cfg.d3dFog) return;
    DWORD ss = Rd<DWORD>(0x9EF96C);
    TDraw4_t q = Rd<TDraw4_t>(0x9EF94C); TDraw3_t tr = Rd<TDraw3_t>(0x9EF974); TDrawN_t fan = Rd<TDrawN_t>(0x9EF960);
    TDraw2_t ln = Rd<TDraw2_t>(0x9EF928);
    TMesh_t qm = Rd<TMesh_t>(0x9EF930), tm = Rd<TMesh_t>(0x9EF924), lm = Rd<TMesh_t>(0x9EF90C);
    if (!ss || !q || !tr || !fan || !ln || !qm || !tm || !lm || ss == (DWORD)&FogSetState) return;
    if (!FogPatchDriver(ss == (DWORD)&MnSetState ? (DWORD)g_mnSetState : ss)) { LogfAsync("d3d fog: off - d3da.dll isn't the analysed version\n"); return; }
    g_fogValue = 0; g_fogD = 0; g_fogNorm = 1; g_fogOn = false;
    g_oSetState = (TSetState_t)ss; g_fogQuad = q; g_fogTri = tr; g_fogFan = fan; g_fogLine = ln;
    g_fogQuadM = qm; g_fogTriM = tm; g_fogLineM = lm;
    *(volatile DWORD*)0x9EF94C = (DWORD)&FogQuad;
    *(volatile DWORD*)0x9EF974 = (DWORD)&FogTri;
    *(volatile DWORD*)0x9EF960 = (DWORD)&FogFan;
    *(volatile DWORD*)0x9EF928 = (DWORD)&FogLine;
    *(volatile DWORD*)0x9EF930 = (DWORD)&FogQuadM;
    *(volatile DWORD*)0x9EF924 = (DWORD)&FogTriM;
    *(volatile DWORD*)0x9EF90C = (DWORD)&FogLineM;
    *(volatile DWORD*)0x9EF96C = (DWORD)&FogSetState;
    *(volatile DWORD*)0x7A3A64 = 4;
    LogfAsync("d3d fog: on\n");
}

asm(".text\n"
    // d3da setup (0x4B5D51, "mov [0x7A3A64], esi" = 0): fog mode and setstate wrapper
    ".globl _D3dFogStub\n"
    "_D3dFogStub:\n"
    "    pushal\n"
    "    call _D3dFogInit\n"
    "    popal\n"
    "    ret\n");

void InstallD3dFog() {
    if (!g_knownExe || !g_cfg.d3dFog) Logf("d3d fog: off\n");
    if (!g_knownExe || !(g_cfg.d3dFog || g_cfg.speedBlur > 0)) return;
    BYTE b[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
    DWORD rel = (DWORD)&D3dFogStub - (0x4B5D51 + 5); memcpy(b + 1, &rel, 4);
    bool ok = PatchBytes(0x4B5D51, b, 6);
    // the cockpit picture is drawn without fog (XfBegin/XfEnd around it)
    if (ok && g_cfg.d3dFog && !(g_cfg.widescreen && g_cfg.wsCockpit)) ok = PatchRel32(0x482CA3, 0xE8, (void*)&XfWrapCockpit);
    if (g_cfg.d3dFog) Logf("d3d fog: %s\n", ok ? "turned on when the Direct3D renderer starts" : "patch FAILED");
    else if (!ok) Logf("speed blur: d3da.dll setup patch FAILED\n");
}
