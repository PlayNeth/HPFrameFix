#include "common.h"
#include "speedfx.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "timing.h"
#include "interp.h"
#include "nightlamps.h"

// ---------------------------------------------------------------------------
// Speed effects. Both follow the speed of the car the view's camera follows
// (cam+4): car+0x4F4, signed, in m/s (the speedometer shows it x3.6 or
// x2.2369, 0x412EB3).
//  - Camera shake: ViewSetup (0x4DBDE0) sets a view's projection up, and the
//    centre the 3D picture is projected round is the float pair [0x5600A4],
//    [0x5600A8] (0x4BF2A0). After each ViewSetup for a race view (the view
//    struct 0x7A3D14: type 0; the mirror's is 1) that centre is moved by a
//    few pixels of smooth noise: the 3D picture shakes, the HUD doesn't. Every
//    ViewSetup sets the centre afresh, so nothing has to be put back.
//  - Speed blur, d3da.dll only: before the main view's HUD (0x4BC3F2, call
//    0x482B00) - or in the cockpit view right after the HUD has drawn the
//    dashboard (0x482CA8, after its call 0x489240), so that's blurred with the
//    road - the 3D picture is copied into a texture and drawn over itself
//    a few times, each a little larger round the middle and fading in towards
//    the edges: a radial blur that leaves the middle sharp. d3da.dll collects
//    triangles and draws them in batches, so its THRASH_flushwindow (draw the
//    batch, EndScene) comes first; the copy is a Blt from the render target,
//    outside a scene. d3da's device is at +0x10F48 and its DirectDraw
//    (IDirectDraw2) at +0x10F28 (read at +0x410E / +0x3A83). Every render
//    state set is read first and put back afterwards, so d3da's own record
//    of them stays right. The texture belongs to d3da's DirectDraw, so it's
//    released before every THRASH_setvideomode, which may tear that down.
// ---------------------------------------------------------------------------
static float g_fxAmt[3][2] = {};                  // [effect: 0 blur, 1 shake, 2 low camera's view][view]
static LONGLONG g_fxLast[3][2] = {};
bool CarListed(DWORD car) {
    long nc = Rd<long>(hp::pCarCount);
    if (nc > 16) nc = 16;
    for (long i = 0; i < nc; i++) if (Rd<DWORD>(hp::pCarList + i * 4) == car) return true;
    return false;
}
// 0..1: where the speed of the car view v follows lies between the effect's
// start (fx: 0 = speedBlurFrom, 1 = cameraShakeFrom, 2 = standing still) and
// speedEffectsFull (eased), smoothed over a quarter of a second
float FxAmount(int fx, int v) {
    float target = 0;
    DWORD cam = EffectiveCam(v);
    if (cam && Readable(cam + hp::camCar, 4)) {
        DWORD car = Rd<DWORD>(cam + hp::camCar);
        if (car && CarListed(car)) {
            float kmh = fabsf(Rd<float>(car + 0x4F4)) * 3.6f;
            float lo = fx == 0 ? (float)g_cfg.speedBlurFrom : fx == 1 ? (float)g_cfg.cameraShakeFrom : 0.0f;
            float hi = (float)g_cfg.speedEffectsFull;
            if (hi <= lo) hi = lo + 1;
            float t = (kmh - lo) / (hi - lo);
            if (t > 0) target = t >= 1 ? 1 : t * t * (3 - 2 * t);   // (NaN: 0)
        }
    }
    LONGLONG now = Qpc();
    LONGLONG& last = g_fxLast[fx][v];
    double dt = last ? (double)(now - last) / (double)g_qpf.QuadPart : 1.0;
    last = now;
    float& a = g_fxAmt[fx][v];
    if (dt >= 0.5) a = target;                    // first frame of a race, or after a long stop
    else a += (target - a) * (float)(1.0 - exp(-dt / 0.25));
    return a;
}

// The shake's clock: real time, but only while the simulation runs (the step
// index moved in the last 100 ms), so a paused game's picture stands still.
static double g_fxNoiseT = 0;
static LONGLONG g_fxNoiseLast = 0, g_fxStepSeen = 0;
static long g_fxStepIdx = -1;
static double FxNoiseTime() {
    LONGLONG now = Qpc();
    long idx = Rd<long>(hp::pSimStepIdx);
    if (idx != g_fxStepIdx) { g_fxStepIdx = idx; g_fxStepSeen = now; }
    if (g_fxNoiseLast && now - g_fxStepSeen < g_qpf.QuadPart / 10) {
        double dt = (double)(now - g_fxNoiseLast) / (double)g_qpf.QuadPart;
        if (dt > 0 && dt < 0.1) g_fxNoiseT += dt;
    }
    g_fxNoiseLast = now;
    return g_fxNoiseT;
}
extern "C" void __cdecl ShakeAfterViewSetup() {
    if (Rd<int>(0x7A3D14) != 0) return;           // the mirror
    if (g_log) {                                  // which camera type each view shows (the low camera's 4 or 6)
        static int lastType[2] = { -1, -1 };
        int vv = Rd<int>(0x7A3D18) & 1;
        DWORD c = EffectiveCam(vv);
        int t = c && Readable(c, 4) ? Rd<int>(c) : 0;
        if (t != lastType[vv]) { lastType[vv] = t; LogfAsync("camera: view %d shows camera type %d\n", vv, t); }
    }
    int v = Rd<int>(0x7A3D18) & 1;
    float a = FxAmount(1, v) * g_cfg.cameraShake;
    if (!(a > 0.001f)) return;
    if (a > 4) a = 4;
    double t = FxNoiseTime();
    const double k = 6.283185307179586;
    double sx = 0.55 * sin(k * 6.1 * t + 0.3) + 0.30 * sin(k * 11.3 * t + 1.7) + 0.15 * sin(k * 17.9 * t + 4.1);
    double sy = 0.55 * sin(k * 7.7 * t + 2.2) + 0.30 * sin(k * 13.1 * t + 0.9) + 0.15 * sin(k * 19.3 * t + 3.3);
    float h = Rd<float>(0x5600B0) * 2.0f;          // the view's height ([0x5600B0] = half of it)
    if (!(h > 0 && h < 10000)) return;
    *(volatile float*)0x5600A4 += (float)(a * sx * h * 0.0016);
    *(volatile float*)0x5600A8 += (float)(a * sy * h * 0.0024);
}

typedef int (__stdcall *TBlurSetMode_t)(DWORD, DWORD, DWORD);
typedef int (__stdcall *TFlush_t)();
static DWORD g_d3daBase = 0;
static TFlush_t g_d3daFlush = nullptr;
static TBlurSetMode_t g_oSetMode = nullptr;
static IDirect3DDevice2* g_blurDev = nullptr;
static IDirectDrawSurface* g_blurSurf = nullptr;
static IDirect3DTexture2* g_blurTex = nullptr;
static D3DTEXTUREHANDLE g_blurHandle = 0;
static int g_blurTW = 0, g_blurTH = 0;
static bool g_blurBroken = false;                 // the driver refused something: off for this session
static const GUID kIID_Texture2 = { 0x93281502, 0x8cf8, 0x11d0, { 0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29 } };

static void BlurRelease() {
    if (g_blurTex) g_blurTex->Release();
    if (g_blurSurf) g_blurSurf->Release();
    g_blurTex = nullptr; g_blurSurf = nullptr; g_blurHandle = 0; g_blurDev = nullptr;
    g_blurTW = g_blurTH = 0;
}
static int __stdcall BlurSetMode(DWORD a, DWORD b, DWORD c) {
    BlurRelease();
    return g_oSetMode(a, b, c);
}
static void BlurOff(const char* why, HRESULT hr) {
    g_blurBroken = true;
    BlurRelease();
    LogfAsync("speed blur: off - %s (0x%08lX)\n", why, (unsigned long)hr);
}
// d3da.dll set up (from D3dFogInit): checked, and its setvideomode wrapped
void SpeedFxDriverInit() {
    g_d3daBase = 0;
    if (!(g_cfg.speedBlur > 0) || g_blurBroken) return;
    DWORD about = Rd<DWORD>(0x9EF980);
    HMODULE m = nullptr;
    if (!about || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      (LPCSTR)about, &m) || !m) return;
    DWORD base = (DWORD)m, pDev = base + 0x10F48, pDD = base + 0x10F28;
    BYTE dev[5] = { 0xA1 }, dd[5] = { 0xA1 };      // mov eax, [device] / mov eax, [DirectDraw]
    memcpy(dev + 1, &pDev, 4); memcpy(dd + 1, &pDD, 4);
    g_d3daFlush = (TFlush_t)GetProcAddress(m, "_THRASH_flushwindow@0");
    if (about - base != 0x1020 || !g_d3daFlush || !Readable(base + 0x3A83, 5) || !Readable(base + 0x410E, 5) ||
        memcmp((void*)(base + 0x410E), dev, 5) || memcmp((void*)(base + 0x3A83), dd, 5)) {
        LogfAsync("speed blur: off - d3da.dll isn't the analysed version\n");
        return;
    }
    DWORD sm = Rd<DWORD>(0x9EF958);
    if (!sm) return;
    if (sm != (DWORD)&BlurSetMode) { g_oSetMode = (TBlurSetMode_t)sm; *(volatile DWORD*)0x9EF958 = (DWORD)&BlurSetMode; }
    g_d3daBase = base;
    LogfAsync("speed blur: on\n");
}

static const int kBlurGX = 16, kBlurGY = 10, kBlurPasses = 4;
struct BlurVtx { float x, y, z, rhw; DWORD color, specular; float u, v; };   // D3DTLVERTEX
static BlurVtx g_blurV[(kBlurGX + 1) * (kBlurGY + 1)];
static WORD g_blurI[kBlurGX * kBlurGY * 6];
static DWORD g_blurStates[][3] = {                // state, value, the game's value
    { 1,  0, 0 },     // TEXTUREHANDLE: the copy (set below)
    { 3,  3, 0 },     // TEXTUREADDRESS: clamp
    { 5,  0, 0 },     // WRAPU off
    { 6,  0, 0 },     // WRAPV off
    { 7,  0, 0 },     // ZENABLE off
    { 9,  2, 0 },     // SHADEMODE: Gouraud (the alpha fades across the grid)
    { 14, 0, 0 },     // ZWRITEENABLE off
    { 15, 0, 0 },     // ALPHATESTENABLE off
    { 17, 2, 0 },     // TEXTUREMAG: linear
    { 18, 2, 0 },     // TEXTUREMIN: linear
    { 19, 5, 0 },     // SRCBLEND: source alpha
    { 20, 6, 0 },     // DESTBLEND: 1 - source alpha
    { 21, 4, 0 },     // TEXTUREMAPBLEND: modulate alpha (the copy has none: the vertex's)
    { 22, 1, 0 },     // CULLMODE: none
    { 27, 1, 0 },     // ALPHABLENDENABLE on
    { 28, 0, 0 },     // FOGENABLE off
    { 29, 0, 0 },     // SPECULARENABLE off
    { 33, 0, 0 },     // STIPPLEDALPHA off
    { 41, 0, 0 },     // COLORKEYENABLE off
};
static const int kBlurStateN = sizeof(g_blurStates) / sizeof(g_blurStates[0]);
static int Pow2Ceil(int v) { int p = 1; while (p < v && p < (1 << 13)) p *= 2; return p; }

static void BlurRun() {
    float a = FxAmount(0, 0) * g_cfg.speedBlur;
    if (!(a > 0.02f)) return;
    if (a > 4) a = 4;
    IDirect3DDevice2* dev = Rd<IDirect3DDevice2*>(g_d3daBase + 0x10F48);
    IDirectDraw2* dd = Rd<IDirectDraw2*>(g_d3daBase + 0x10F28);
    if (!dev || !dd) return;
    if (dev != g_blurDev) {                       // a new device without a mode set seen: the old
        g_blurTex = nullptr; g_blurSurf = nullptr; // objects may be gone with it - dropped, not released
        g_blurHandle = 0; g_blurTW = g_blurTH = 0;
        g_blurDev = dev;
    }
    IDirectDrawSurface* rt = nullptr;
    if (FAILED(dev->GetRenderTarget(&rt)) || !rt) return;
    DDSURFACEDESC rd = {}; rd.dwSize = sizeof(rd);
    if (FAILED(rt->GetSurfaceDesc(&rd))) { rt->Release(); return; }
    if (!(rd.dwFlags & DDSD_PIXELFORMAT)) {
        rd.ddpfPixelFormat.dwSize = sizeof(rd.ddpfPixelFormat);
        if (FAILED(rt->GetPixelFormat(&rd.ddpfPixelFormat))) { rt->Release(); return; }
    }
    // the main view's rect (x, y, w, h), within the render target
    const int* vr = (const int*)0x7CDB88;
    int vx = vr[0], vy = vr[1], vw = vr[2], vh = vr[3];
    if (vx < 0) { vw += vx; vx = 0; }
    if (vy < 0) { vh += vy; vy = 0; }
    if (vx + vw > (int)rd.dwWidth) vw = (int)rd.dwWidth - vx;
    if (vy + vh > (int)rd.dwHeight) vh = (int)rd.dwHeight - vy;
    if (vw < 64 || vh < 64) { rt->Release(); return; }

    if (!g_blurSurf || vw > g_blurTW || vh > g_blurTH) {
        if (g_blurTex) g_blurTex->Release();
        if (g_blurSurf) g_blurSurf->Release();
        g_blurTex = nullptr; g_blurSurf = nullptr; g_blurHandle = 0;
        int tw = Pow2Ceil(vw), th = Pow2Ceil(vh);
        if (tw < vw || th < vh) { rt->Release(); BlurOff("screen too large", 0); return; }
        DDSURFACEDESC sd = {}; sd.dwSize = sizeof(sd);
        sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        sd.dwWidth = tw; sd.dwHeight = th;
        sd.ddpfPixelFormat = rd.ddpfPixelFormat;
        sd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
        HRESULT hr = dd->CreateSurface(&sd, &g_blurSurf, nullptr);
        if (FAILED(hr) || !g_blurSurf) { g_blurSurf = nullptr; rt->Release(); BlurOff("no texture", hr); return; }
        hr = g_blurSurf->QueryInterface(kIID_Texture2, (void**)&g_blurTex);
        if (SUCCEEDED(hr) && g_blurTex) hr = g_blurTex->GetHandle(dev, &g_blurHandle);
        if (FAILED(hr) || !g_blurTex || !g_blurHandle) { rt->Release(); BlurOff("no texture handle", hr); return; }
        g_blurTW = tw; g_blurTH = th;
        LogfAsync("speed blur: %dx%d copy in a %dx%d texture, %lu bits\n", vw, vh, tw, th,
                  (unsigned long)rd.ddpfPixelFormat.dwRGBBitCount);
    }

    g_d3daFlush();                                // the 3D picture complete, scene ended
    // the states as d3da left them (its flush sets some, the texture among them)
    for (int i = 0; i < kBlurStateN; i++)
        if (FAILED(dev->GetRenderState((D3DRENDERSTATETYPE)g_blurStates[i][0], &g_blurStates[i][2]))) {
            rt->Release(); BlurOff("render states can't be read", 0); return;
        }
    RECT src = { vx, vy, vx + vw, vy + vh }, dst = { 0, 0, vw, vh };
    HRESULT hr = g_blurSurf->Blt(&dst, rt, &src, DDBLT_WAIT, nullptr);
    rt->Release();
    if (hr == DDERR_SURFACELOST) { g_blurSurf->Restore(); return; }
    if (FAILED(hr)) { BlurOff("the picture can't be copied", hr); return; }
    if (FAILED(dev->BeginScene())) return;

    static bool idx = false;
    if (!idx) {
        int n = 0;
        for (int j = 0; j < kBlurGY; j++)
            for (int i = 0; i < kBlurGX; i++) {
                WORD p = (WORD)(j * (kBlurGX + 1) + i);
                g_blurI[n++] = p; g_blurI[n++] = (WORD)(p + 1); g_blurI[n++] = (WORD)(p + kBlurGX + 1);
                g_blurI[n++] = (WORD)(p + 1); g_blurI[n++] = (WORD)(p + kBlurGX + 2); g_blurI[n++] = (WORD)(p + kBlurGX + 1);
            }
        idx = true;
    }
    g_blurStates[0][1] = g_blurHandle;
    for (int i = 0; i < kBlurStateN; i++)
        if (g_blurStates[i][1] != g_blurStates[i][2])
            dev->SetRenderState((D3DRENDERSTATETYPE)g_blurStates[i][0], g_blurStates[i][1]);
    // Pass k: the copy k/passes of the way to its largest, at 1/(k+1), so all
    // of them and the picture underneath end up with the same weight. Round a
    // point a little above the middle (the road's vanishing point); the
    // copies fade in from a quarter of the way out to the edges.
    float cx = vw * 0.5f, cy = vh * 0.45f, iw = 1.0f / g_blurTW, ih = 1.0f / g_blurTH;
    float umax = vw - 0.5f, vmax = vh - 0.5f;     // (bilinear filtering never reaches past the copy)
    float hx = vw * 0.5f, hy = vh * 0.5f;
    for (int k = 1; k <= kBlurPasses; k++) {
        float inv = 1.0f / (1.0f + a * 0.028f * k / kBlurPasses), wk = 255.0f / (k + 1);
        BlurVtx* p = g_blurV;
        for (int j = 0; j <= kBlurGY; j++)
            for (int i = 0; i <= kBlurGX; i++, p++) {
                float x = vw * (float)i / kBlurGX, y = vh * (float)j / kBlurGY;
                float dx = (x - cx) / hx, dy = (y - cy) / hy, r = sqrtf(dx * dx + dy * dy);
                float m = (r - 0.25f) / 0.75f;
                m = m <= 0 ? 0 : m >= 1 ? 1 : m * m * (3 - 2 * m);
                p->x = vx + x - 0.5f; p->y = vy + y - 0.5f; p->z = 0; p->rhw = 1;
                p->color = ((DWORD)(m * wk + 0.5f) << 24) | 0xFFFFFF;
                p->specular = 0xFF000000;
                float su = cx + (x - cx) * inv, sv = cy + (y - cy) * inv;
                p->u = (su < 0.5f ? 0.5f : su > umax ? umax : su) * iw;
                p->v = (sv < 0.5f ? 0.5f : sv > vmax ? vmax : sv) * ih;
            }
        dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, g_blurV, (kBlurGX + 1) * (kBlurGY + 1),
                                  g_blurI, kBlurGX * kBlurGY * 6, D3DDP_DONOTCLIP | D3DDP_DONOTUPDATEEXTENTS);
    }
    for (int i = kBlurStateN - 1; i >= 0; i--)
        if (g_blurStates[i][1] != g_blurStates[i][2])
            dev->SetRenderState((D3DRENDERSTATETYPE)g_blurStates[i][0], g_blurStates[i][2]);
    dev->EndScene();
}
// The dashboard is drawn by 0x489240 when [0x7922FC] is set, there's one view
// ([0x7A3D10] = 0), view 0's camera is the cockpit's (type, cam+0, 1), it
// isn't looking back ([0x79C4A0]) and [0x6FBC4C] is 0 - the same tests here.
static bool CockpitView() {
    if (!Rd<int>(0x7922FC) || Rd<int>(0x7A3D10) || Rd<int>(0x79C4A0) || Rd<int>(0x6FBC4C)) return false;
    DWORD cam = EffectiveCam(0);
    return cam && Readable(cam, 4) && Rd<int>(cam) == 1;
}
static bool g_blurAfterDash = false;
extern "C" void __cdecl SpeedBlur() {             // the main view's HUD is next
    g_blurAfterDash = false;
    if (!g_d3daBase || g_blurBroken || Rd<DWORD>(0x9EF958) != (DWORD)&BlurSetMode) return;   // (another renderer since)
    if (Rd<int>(0x6FD3B0) == 1 || Rd<int>(0x7A3D14) != 0) return;   // split screen; not the main view
    if (CockpitView()) g_blurAfterDash = true;
    else BlurRun();
}
extern "C" void __cdecl SpeedBlurDash() {         // the HUD has drawn the dashboard
    if (!g_blurAfterDash) return;
    g_blurAfterDash = false;
    BlurRun();
}

extern "C" { DWORD g_fnHudDraw = 0x482B00; }
extern "C" void SpeedBlurStub();
extern "C" void ViewSetupTailStub();
extern "C" void SpeedBlurDashStub();
asm(
    // the HUD, after the dashboard (0x482CA8, replaces "cmp [0x7A3D10], 0")
    ".globl _SpeedBlurDashStub\n"
    "_SpeedBlurDashStub:\n"
    "    pushal\n"
    "    call _SpeedBlurDash\n"
    "    popal\n"
    "    cmpl $0, 0x7A3D10\n"
    "    ret\n"
    // the main view's HUD call (0x4BC3F2): the blur first
    ".globl _SpeedBlurStub\n"
    "_SpeedBlurStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    call _SpeedBlur\n"
    "    popal\n"
    "    popfl\n"
    "    jmp *_g_fnHudDraw\n"
    // ViewSetup's tail (0x4DBEE0, jumped to): the shake, then its own pops and ret
    ".globl _ViewSetupTailStub\n"
    "_ViewSetupTailStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    call _ShakeAfterViewSetup\n"
    "    popal\n"
    "    popfl\n"
    "    popl %ebp\n"
    "    popl %esi\n"
    "    popl %edx\n"
    "    popl %ecx\n"
    "    popl %ebx\n"
    "    ret\n");

void InstallSpeedEffects() {
    if (!g_knownExe) return;
    if (g_cfg.cameraShake > 0)
        Logf("camera shake: x%.2f from %d to %d km/h: %s\n", g_cfg.cameraShake, g_cfg.cameraShakeFrom,
             g_cfg.speedEffectsFull, PatchRel32(0x4DBEE0, 0xE9, (void*)&ViewSetupTailStub) ? "on" : "patch FAILED");
    else Logf("camera shake: off\n");
    if (g_cfg.speedBlur > 0)
        Logf("speed blur: x%.2f from %d to %d km/h: %s\n", g_cfg.speedBlur, g_cfg.speedBlurFrom, g_cfg.speedEffectsFull,
             PatchRel32(0x4BC3F2, 0xE8, (void*)&SpeedBlurStub) && PatchRel32(0x482CA8, 0xE8, (void*)&SpeedBlurDashStub) &&
             PatchBytes(0x482CAD, "\x90\x90", 2) ? "with the Direct3D renderer" : "patch FAILED");
    else Logf("speed blur: off\n");
}
