#include "common.h"
#include "menus.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "interp.h"
#include "widescreen.h"
#include "fog.h"
#include "hud.h"
#include "nightlamps.h"

// ---------------------------------------------------------------------------
// Menus at the race resolution. The game shows its menus in a 640x480 mode
// ([0x55FEC8], set at startup 0x4B60FD, back from a race 0x4A35E8 and after a
// reset 0x44B1E5) and the race in the one picked in the options
// (GetSelectedMode 0x4482F0 -> 0x4A3422). SetVideoMode (0x4BEF50) copies the
// requested mode's entry to [0x7CDAE0] - the size the game draws for - and
// calls the renderer's setvideomode(mode, depth, flag).
//
// Around the menu calls the renderer gets another mode of the same colour
// depth (the movie player writes pixels in the menu mode's format): the race
// resolution (or the desktop's). The game still draws for 640x480, so the
// renderer's draw calls - wrapped when the game looks them up (GetProcAddress,
// from ThrashLoadDriver 0x4F8390) - move every vertex from 640x480 to a 4:3
// area in the middle (or stretch it), clip rects too; the bars are cleared
// before each page flip. The movie player (MAD, 0x517580...) locks the frame
// and writes a 640x480 picture at its top left (it's told the frame is
// 640x480), which is then scaled up in place. The software renderer draws
// screen-aligned quads as unscaled blits, so its menus stay 640x480.
//
// The game saves only the race resolution's width ([0x6FBC18]) and at startup
// looks for a 4:3 15/16-bit mode of that width (0x4BEE80, at 0x47327E,
// 0x4B610C and for the defaults 0x472ECD); with every resolution listed that
// finds the wrong mode or none. So the race resolution is kept in the ini
// (wsRaceWidth/Height) and the lookup answers with the listed mode of that
// size (or of the 4:3 size its own lookup found, as the list has it).
// ---------------------------------------------------------------------------
typedef void* (WINAPI *TAbout_t)();
typedef int   (WINAPI *TSetMode_t)(int, int, int);
typedef void  (WINAPI *TVoidFn_t)();
typedef void* (WINAPI *TLockFn_t)();
typedef void  (WINAPI *TUnlockFn_t)(void*);
typedef void  (WINAPI *TP1_t)(void*);
typedef void  (WINAPI *TP2_t)(void*, void*);
typedef void  (WINAPI *TP3_t)(void*, void*, void*);
typedef void  (WINAPI *TP4_t)(void*, void*, void*, void*);
typedef void  (WINAPI *TStrip_t)(int, void*);
typedef void  (WINAPI *TMeshFn_t)(int, void*, DWORD*);
typedef int   (WINAPI *TClipFn_t)(int, int, int, int);
static TAbout_t g_mnAbout; static TSetMode_t g_mnSetMode; static TVoidFn_t g_mnFlip, g_mnClear;
static TLockFn_t g_mnLock; static TUnlockFn_t g_mnUnlock;
static TP1_t g_mnPoint; static TP2_t g_mnLine; static TP3_t g_mnTri; static TP4_t g_mnQuad;
static TStrip_t g_mnFan, g_mnStrip, g_mnLStrip, g_mnPStrip;
static TMeshFn_t g_mnTriM, g_mnQuadM, g_mnLineM, g_mnPointM;
static TClipFn_t g_mnClip;
static HMODULE g_mnMod = nullptr;
static bool g_mnSoft = false;                     // software renderer: menus left alone
static volatile bool g_mnXf = false;              // menus mapped right now
// The software renderer's menus (wsMenusSoft): it can't be given its pictures
// at another size - it fits a quad's whole texture to it whatever the u/v say,
// and as triangles the pictures come out with smeared edges and doubled
// letters. But it draws into memory, so the menus are left exactly as the game
// draws them, 640x480 at the top left of the bigger screen, and the finished
// frame is scaled to the menu area in place just before each page flip
// (MnFlip), as a movie's frames already are. g_mnSoftFrame: that's on.
static bool g_mnSoftFrame = false;
static float g_mnSx = 1, g_mnSy = 1, g_mnOx = 0, g_mnOy = 0;
static int g_mnRealW = 0, g_mnRealH = 0, g_mnRealBpp = 16, g_mnRealIdx = 0;
static int g_mnFeCall = 0, g_mnFeTarget = 0;
static int g_mnClipRect[4] = { 0, 0, 640, 480 };  // as the game set it
static int g_dispW = 0, g_dispH = 0;              // the desktop at startup

static int* ModeEntryPtr(int idx) {
    if (!g_mnAbout || idx < 1) return nullptr;
    BYTE* a = (BYTE*)g_mnAbout();
    if (!a || IsBadReadPtr(a, 0x44)) return nullptr;
    int n = *(int*)(a + 0x3C); BYTE* e = *(BYTE**)(a + 0x40);
    if (idx > n || !e || IsBadReadPtr(e + idx * 40, 40)) return nullptr;
    return (int*)(e + idx * 40);
}
// the mode of that size: bpp > 0 = that colour depth (15 and 16 count as one),
// else the one the options list would show
static int FindModeWH(int w, int h, int bpp) {
    if (!g_mnAbout || w <= 0 || h <= 0) return 0;
    BYTE* a = (BYTE*)g_mnAbout();
    if (!a || IsBadReadPtr(a, 0x44)) return 0;
    int n = *(int*)(a + 0x3C), best = 0, bestRank = 0;
    for (int i = 1; i <= n && i < 256; i++) {
        int* m = ModeEntryPtr(i);
        if (!m || m[0] != w || m[1] != h) continue;
        if (bpp > 0) {
            bool same = m[2] == bpp || ((m[2] == 15 || m[2] == 16) && (bpp == 15 || bpp == 16));
            if (same && (!best || m[2] == bpp)) best = i;
            continue;
        }
        int r = ModeRank(m);
        if (r > bestRank) { bestRank = r; best = i; }
    }
    return best;
}

// ---- draw wrappers: 640x480 -> the screen, while the menus are mapped -----
struct MnV { float f[8]; };
static bool g_pzPending = false;                  // in-race menus: the next vertex decides where its item goes (PzDecide)
static void PzDecide(float x, float y);
// alternatePixelCenter: Direct3D (as d3da.dll uses it) takes a pixel's centre to
// be at whole coordinates, where the game's 2D pictures are laid out for
// pixels that start there. So every vertex on its way to the Direct3D
// renderer is moved half a pixel up and left; pictures drawn 1:1 then land
// texel on pixel. Not the cockpit picture (0x489240: dashboard pieces,
// needles, wheel; g_xfCockpit): the game gives those pieces u and v already
// moved by half a texel (0.5/256 to 1 + 0.5/256 for a whole 256 piece), so
// they are texel on pixel as they come and the move would blur them.
static bool g_apc = false;                        // on for the renderer loaded now
// menuTexelInset: the in-race menus' pictures are laid out for 640x480, a texel
// to a pixel. Enlarged for a bigger screen and filtered, a picture's outermost
// pixels sit up to half a texel past its outermost texels' centres and take in
// what's beyond the picture (the texture's other side, or its neighbour) - a
// thin line beside a button. So while such a menu is drawn (g_mnInset, with
// the screen's size over 640x480 in g_mnInsetX/Y) a rectangle's u and v are
// pulled in by half a texel, a texel being what one 640x480 pixel covers.
//
// The front-end menus enlarged by wsMenus need the same (g_mnFeInset; there the
// game's coordinates are 640x480's, so the scale is 1). And their big pictures -
// backgrounds, titles, cut into tiles - the game draws with the filter off
// (0x4D7160: setstate(7, 0) around them when [0x7A3A58] has 0x40 and the
// picture is more than 10 wider than high or the reverse; d3da.dll's state 7 is
// D3DRENDERSTATE_TEXTUREMAG/MIN, 0 = nearest, 1 = linear), surely so the tiles
// don't show their edges. Enlarged, unfiltered looks jagged; with the tiles'
// u and v pulled in the filter can stay on (wsMenuFilter, MnFeFilterState in
// place of that call).
//
// How much half a texel is: with d3da.dll a texture's handle (setstate 1, or
// settexture) points at its record, which starts with its width and height
// (talloc, +0x3534, stores its first two arguments there), and u and v run 0
// to 1 over it. That's used when it reads sanely; otherwise a texel is taken
// to be what one 640x480 pixel of the rectangle covers, which is right for a
// picture drawn at its own size and too little for one drawn bigger.
static void* g_mnTex = nullptr;                   // the texture in use, as the game last set it
typedef void (WINAPI *TSetTex_t)(void*);
static TSetTex_t g_mnSetTex = nullptr;
int WINAPI MnSetState(int s, int v) { if (s == 1) g_mnTex = (void*)v; return g_mnSetState(s, v); }
static void WINAPI MnSetTex(void* h) { g_mnTex = h; g_mnSetTex(h); }
static bool g_mnD3D = false, g_mnInset = false, g_mnFeInset = false;
// Filtered, a picture with see-through parts gets a dark rim: the menus'
// pictures of that kind are 16-bit with one bit of alpha (THRASH type 3, the
// game's 0x7E; 0x4D5F40 picks the type), their see-through texels are black,
// and the filter mixes that black into the picture's outermost texels (the
// reason the game draws them unfiltered). So when the front-end hands such a
// picture to the renderer (tupdate, while the menus are enlarged), every
// see-through texel that touches the picture is given the average colour of
// the picture's texels around it; it stays see-through, and the rim mixes with
// its own colour. d3da.dll has colour keying off (render state 41 = 0), so the
// colour of a see-through texel matters to nothing else.
typedef int (WINAPI *TTUpdate_t)(void*, void*, int);
static TTUpdate_t g_mnTUpdate = nullptr;
static int WINAPI MnTUpdate(void* h, void* data, int lvl) {
    if (g_mnFeInset && lvl == 0 && g_mnD3D && g_cfg.wsMenuFilter && h && data && !IsBadReadPtr(h, 0x14)) {
        const int* r = (const int*)h; int w = r[0], hh = r[1];
        if (r[4] == 3 && w >= 2 && hh >= 2 && w <= 1024 && hh <= 1024 && !IsBadWritePtr(data, (size_t)w * hh * 2)) {
            static WORD* cp = nullptr;
            if (!cp) cp = (WORD*)malloc((size_t)1024 * 1024 * 2);
            if (cp) {
                WORD* px = (WORD*)data;
                memcpy(cp, px, (size_t)w * hh * 2);
                for (int y = 0; y < hh; y++) for (int x = 0; x < w; x++) {
                    if (cp[y * w + x] & 0x8000) continue;
                    int n = 0, cr = 0, cg = 0, cb = 0;
                    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                        int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= hh) continue;
                        WORD c = cp[yy * w + xx];
                        if (c & 0x8000) { n++; cr += (c >> 10) & 31; cg += (c >> 5) & 31; cb += c & 31; }
                    }
                    if (n) px[y * w + x] = (WORD)(((cr / n) << 10) | ((cg / n) << 5) | (cb / n));
                }
                static bool logged = false;
                if (!logged) { logged = true; LogfAsync("menus: see-through pictures get their rims coloured for the filter\n"); }
            }
        }
    }
    return g_mnTUpdate(h, data, lvl);
}
// The menus' thin lines (the stripes that slide about the background) are
// plain rectangles two 640x480 pixels high, drawn by the game every frame.
// Enlarged by a factor that isn't whole their edges fall inside pixels, and
// the renderer, which draws a pixel or doesn't, makes them two pixels thick
// here and three there. So in the enlarged front-end a rectangle with no
// texture, up to four game pixels thin, is drawn as its whole rows plus the
// rows its top and bottom edges cut through, those with the vertex alpha
// scaled by how much of the row the line covers - what the filter does to the
// pictures around it. (The menus draw with alpha blending on: their faded
// pictures are plain vertex alpha in the same run of calls.) Its left and
// right ends go to whole pixels. Returns false if it isn't such a rectangle.
static bool MnPlainLine(MnV* const q[4], const void* const src[4]) {
    float lo[2], hi[2], tlo[2], thi[2];
    for (int j = 0; j < 2; j++) {
        lo[j] = hi[j] = ((const float*)src[0])[j]; tlo[j] = thi[j] = q[0]->f[j];
        for (int i = 1; i < 4; i++) {
            float f = ((const float*)src[i])[j], t = q[i]->f[j];
            if (f < lo[j]) { lo[j] = f; tlo[j] = t; }
            if (f > hi[j]) { hi[j] = f; thi[j] = t; }
        }
        for (int i = 0; i < 4; i++) { float f = ((const float*)src[i])[j]; if (f != lo[j] && f != hi[j]) return false; }
        if (hi[j] <= lo[j]) return false;
    }
    if (hi[1] - lo[1] > 4.0f) return false;
    // (a pixel row r covers r to r + 1 of c = the vertex's y + 0.5, with or without alternatePixelCenter)
    float x0 = floorf(tlo[0] + 1.0f) - 0.5f, x1 = floorf(thi[0] + 1.0f) - 0.5f;
    float c0 = tlo[1] + 0.5f, c1 = thi[1] + 0.5f, r0 = floorf(c0 + 0.01f), r1 = floorf(c1 + 0.01f);
    struct Row { float a, b, cov; } rows[3]; int n = 0;
    if (r0 == r1) rows[n++] = { r0, r0 + 1.0f, c1 - c0 };
    else {
        float core = r0;
        if (c0 - r0 > 0.01f) { rows[n++] = { r0, r0 + 1.0f, r0 + 1.0f - c0 }; core = r0 + 1.0f; }
        if (r1 > core) rows[n++] = { core, r1, 1.0f };
        if (c1 - r1 > 0.01f) rows[n++] = { r1, r1 + 1.0f, c1 - r1 };
    }
    for (int k = 0; k < n; k++) {
        MnV v[4];
        for (int i = 0; i < 4; i++) {
            v[i] = *q[i];
            const float* f = (const float*)src[i];
            v[i].f[0] = f[0] == lo[0] ? x0 : x1;
            v[i].f[1] = (f[1] == lo[1] ? rows[k].a : rows[k].b) - 0.5f;
            DWORD c; memcpy(&c, &v[i].f[4], 4);
            DWORD al = (DWORD)lrintf((float)(c >> 24) * (rows[k].cov > 1.0f ? 1.0f : rows[k].cov));
            c = (c & 0x00FFFFFF) | (al << 24); memcpy(&v[i].f[4], &c, 4);
        }
        g_mnQuad(&v[0], &v[1], &v[2], &v[3]);
    }
    return true;
}
static float g_mnInsetX = 1, g_mnInsetY = 1;
static inline bool MnInsetOn() { return g_mnInset || (g_mnFeInset && g_mnXf); }
static inline bool MnActive() { return g_mnXf || g_apc || g_mnInset; }
static int WINAPI MnFeFilterState(int s, int v) {
    TSetState_t f = Rd<TSetState_t>(0x9EF96C);
    if (!f) return 0;
    if (s == 7 && v == 0 && g_cfg.wsMenuFilter && g_mnFeInset && g_mnXf) v = 1;
    return f(s, v);
}
static void MnInset(MnV* const q[4], const void* const src[4]) {
    float lo[4], hi[4];                           // x, y, u, v as the game gave them
    static const int at[4] = { 0, 1, 6, 7 };
    for (int j = 0; j < 4; j++) {
        lo[j] = hi[j] = ((const float*)src[0])[at[j]];
        for (int i = 1; i < 4; i++) {
            float f = ((const float*)src[i])[at[j]];
            if (f < lo[j]) lo[j] = f;
            if (f > hi[j]) hi[j] = f;
        }
    }
    for (int i = 0; i < 4; i++) {                 // a rectangle with its picture upright only
        const float* f = (const float*)src[i];
        if ((f[0] != lo[0] && f[0] != hi[0]) || (f[1] != lo[1] && f[1] != hi[1]) ||
            (f[6] != lo[2] && f[6] != hi[2]) || (f[7] != lo[3] && f[7] != hi[3])) return;
    }
    const float sc[2] = { g_mnInset ? g_mnInsetX : 1.0f, g_mnInset ? g_mnInsetY : 1.0f };
    int dim[2] = { 0, 0 };
    if (g_mnD3D && g_mnTex && !IsBadReadPtr(g_mnTex, 8)) {
        memcpy(dim, g_mnTex, 8);
        for (int j = 0; j < 2; j++) if (dim[j] < 1 || dim[j] > 4096 || (dim[j] & (dim[j] - 1))) dim[0] = dim[1] = 0;
    }
    for (int j = 0; j < 2; j++) {
        float d = hi[j] - lo[j], t = hi[2 + j] - lo[2 + j];
        if (d < 1.0f || t <= 0.0f) continue;
        float in = 0.5f * t * sc[j] / d;          // half of what one 640x480 pixel covers
        if (dim[j] && t <= 1.001f && lo[2 + j] >= -0.001f && hi[2 + j] <= 1.001f) in = 0.5f / (float)dim[j];
        if (in > t * 0.25f) in = t * 0.25f;
        for (int i = 0; i < 4; i++) {
            float& u = q[i]->f[6 + j];
            if (u == lo[2 + j]) u += in; else u -= in;
        }
    }
}
static inline void MnXv(MnV& d, const void* s) {
    if (g_pzPending) PzDecide(((const float*)s)[0], ((const float*)s)[1]);
    memcpy(&d, s, 32);
    if (g_mnXf) { d.f[0] = d.f[0] * g_mnSx + g_mnOx; d.f[1] = d.f[1] * g_mnSy + g_mnOy; }
    if (g_apc && !g_xfCockpit) { d.f[0] -= 0.5f; d.f[1] -= 0.5f; }
}
static inline int MnX(int x) { return (int)lrintf(x * g_mnSx + g_mnOx); }
static inline int MnY(int y) { return (int)lrintf(y * g_mnSy + g_mnOy); }
static MnV* g_mnBuf = nullptr; static int g_mnBufN = 0;
static MnV* MnArr(const void* v, int n) {
    if (n > g_mnBufN) {
        free(g_mnBuf); g_mnBufN = n + 1024;
        g_mnBuf = (MnV*)malloc(sizeof(MnV) * g_mnBufN);
        if (!g_mnBuf) { g_mnBufN = 0; return nullptr; }
    }
    for (int i = 0; i < n; i++) MnXv(g_mnBuf[i], (const BYTE*)v + i * 32);
    return g_mnBuf;
}
static void WINAPI MnPoint(void* a) { if (!MnActive()) return g_mnPoint(a); MnV x; MnXv(x, a); g_mnPoint(&x); }
static void WINAPI MnLine(void* a, void* b) { if (!MnActive()) return g_mnLine(a, b); MnV x, y; MnXv(x, a); MnXv(y, b); g_mnLine(&x, &y); }
static void WINAPI MnTri(void* a, void* b, void* c) {
    if (!MnActive()) return g_mnTri(a, b, c);
    MnV x, y, z; MnXv(x, a); MnXv(y, b); MnXv(z, c); g_mnTri(&x, &y, &z);
}
// A rectangle showing a whole texture (u and v 0 to 1), as the tiles of the
// menus' big pictures are: pulling u and v in would shift what each tile shows
// by half a texel towards its middle, a small jog where two tiles meet. For
// those the enlarged front-end has the renderer hold the edge texels instead
// (d3da.dll's state 13: 0 = D3DTADDRESS_CLAMP, 1 = WRAP; the game keeps what
// it last set in [0x554E60]) - nothing moves, and nothing from the far side
// gets in.
static bool MnWholeTex(const void* const src[4]) {
    bool x0 = false, x1 = false, y0 = false, y1 = false;
    for (int i = 0; i < 4; i++) {
        const float* f = (const float*)src[i];
        if ((f[6] != 0.0f && f[6] != 1.0f) || (f[7] != 0.0f && f[7] != 1.0f)) return false;
        (f[6] == 0.0f ? x0 : x1) = true; (f[7] == 0.0f ? y0 : y1) = true;
    }
    return x0 && x1 && y0 && y1;
}
static void WINAPI MnQuad(void* a, void* b, void* c, void* d) {
    if (!MnActive()) return g_mnQuad(a, b, c, d);
    MnV x, y, z, w; MnXv(x, a); MnXv(y, b); MnXv(z, c); MnXv(w, d);
    if (MnInsetOn()) {
        MnV* const q[4] = { &x, &y, &z, &w }; const void* const s[4] = { a, b, c, d };
        if (g_mnTex && !g_mnInset && g_mnD3D && g_mnSetState && MnWholeTex(s)) {
            g_mnSetState(13, 0);
            g_mnQuad(&x, &y, &z, &w);
            g_mnSetState(13, Rd<int>(0x554E60) == 0 ? 0 : 1);
            return;
        }
        if (g_mnTex) MnInset(q, s);
        else if (!g_mnInset && g_cfg.wsMenuFilter && MnPlainLine(q, s)) return;
    }
    g_mnQuad(&x, &y, &z, &w);
}
// strips and fans: (count, vertices); count + extra vertices in a row
static void MnStripCall(TStrip_t f, int n, void* v, int extra) {
    if (!MnActive() || n <= 0 || !v) return f(n, v);
    MnV* b = MnArr(v, n + extra);
    if (!b) return f(n, v);
    if (MnInsetOn() && f == g_mnFan && n == 2) {    // (a rectangle as a fan)
        MnV* const q[4] = { b, b + 1, b + 2, b + 3 };
        const BYTE* p = (const BYTE*)v; const void* const s[4] = { p, p + 32, p + 64, p + 96 };
        MnInset(q, s);
    }
    f(n, b);
}
static void WINAPI MnFan(int n, void* v)    { MnStripCall(g_mnFan, n, v, 2); }
static void WINAPI MnStrip(int n, void* v)  { MnStripCall(g_mnStrip, n, v, 2); }
static void WINAPI MnLStrip(int n, void* v) { MnStripCall(g_mnLStrip, n, v, 1); }
static void WINAPI MnPStrip(int n, void* v) { MnStripCall(g_mnPStrip, n, v, 0); }
static void MnMeshCall(TMeshFn_t f, int n, void* v, DWORD* idx, int per) {
    if (!MnActive() || n <= 0 || !v || !idx) return f(n, v, idx);
    DWORD mx = 0;
    for (int i = 0; i < n * per; i++) if (idx[i] > mx) mx = idx[i];
    MnV* b = mx < 65536 ? MnArr(v, (int)mx + 1) : nullptr;
    if (!b) return f(n, v, idx);
    f(n, b, idx);
}
static void WINAPI MnTriM(int n, void* v, DWORD* i)   { MnMeshCall(g_mnTriM, n, v, i, 3); }
static void WINAPI MnQuadM(int n, void* v, DWORD* i)  { MnMeshCall(g_mnQuadM, n, v, i, 4); }
static void WINAPI MnLineM(int n, void* v, DWORD* i)  { MnMeshCall(g_mnLineM, n, v, i, 2); }
static void WINAPI MnPointM(int n, void* v, DWORD* i) { MnMeshCall(g_mnPointM, n, v, i, 1); }
static int MnClipNow(int x0, int y0, int x1, int y1) {
    return g_mnXf ? g_mnClip(MnX(x0), MnY(y0), MnX(x1), MnY(y1)) : g_mnClip(x0, y0, x1, y1);
}
static int WINAPI MnClipW(int x0, int y0, int x1, int y1) {
    g_mnClipRect[0] = x0; g_mnClipRect[1] = y0; g_mnClipRect[2] = x1; g_mnClipRect[3] = y1;
    return MnClipNow(x0, y0, x1, y1);
}
// the bars: the clear only covers the clip rect, so each bar is cleared on its own
static void MnClearBars() {
    if (!g_mnClip || !g_mnClear || g_mnRealW <= 0) return;
    int x0 = MnX(0), y0 = MnY(0), x1 = MnX(640), y1 = MnY(480);
    int r[4][4], n = 0;
    if (x0 > 0)          { int q[4] = { 0, 0, x0, g_mnRealH };  memcpy(r[n++], q, sizeof q); }
    if (x1 < g_mnRealW)  { int q[4] = { x1, 0, g_mnRealW, g_mnRealH }; memcpy(r[n++], q, sizeof q); }
    if (y0 > 0)          { int q[4] = { x0, 0, x1, y0 };        memcpy(r[n++], q, sizeof q); }
    if (y1 < g_mnRealH)  { int q[4] = { x0, y1, x1, g_mnRealH }; memcpy(r[n++], q, sizeof q); }
    if (!n) return;
    for (int i = 0; i < n; i++) { g_mnClip(r[i][0], r[i][1], r[i][2], r[i][3]); g_mnClear(); }
    MnClipNow(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
}
static void MnScaleFrame(const BYTE* from = nullptr, int fromPitch = 0);
static void* g_mnLockReal = nullptr;
static BYTE* g_mnSrc = nullptr;                   // the 640x480 picture MnScaleFrame scaled last
static int   g_mnSrcBp = 0;                       // its bytes per pixel (0: none yet)
static void WINAPI MnFlip() {
    if (g_mnXf) MnClearBars();
    bool soft = g_mnSoftFrame && g_mnLock && g_mnUnlock && !g_mnLockReal;
    if (soft) {
        g_mnSrcBp = 0;
        void* p = g_mnLock();
        if (p && !IsBadReadPtr(p, 0x14)) { g_mnLockReal = p; MnScaleFrame(); g_mnLockReal = nullptr; }
        if (p) g_mnUnlock(p);
    }
    g_mnFlip();
    // The frame the game draws on next gets the unscaled picture back at its
    // top left, so a menu that only redraws what changed finds what it left.
    if (soft && g_mnSrcBp) {
        void* p = g_mnLock();
        if (p && !IsBadReadPtr(p, 0x14)) {
            BYTE* bits = *(BYTE**)p; int pitch = *(int*)((BYTE*)p + 4);
            if (bits && pitch >= 640 * g_mnSrcBp && g_mnRealH >= 480)
                for (int y = 0; y < 480; y++)
                    memcpy(bits + (size_t)y * pitch, g_mnSrc + (size_t)y * 640 * g_mnSrcBp, (size_t)640 * g_mnSrcBp);
        }
        if (p) g_mnUnlock(p);
    }
}

// ---- movies: the locked frame ------------------------------------------------
// lockwindow returns { bits, pitch, format (+8), width (+0xC), height (+0x10),
// ... }. The movie player takes all of it once when a movie starts (0x5179D0,
// its lock at 0x517A36) and works out its own values from the row length
// there (0x509A30 -> player+0x2C); each frame's lock (0x51789B, and 0x524823)
// then only updates the frame's address and row length (player+0x20, +0x28).
// While the menus are in a mode of their own the player gets its own 640x480
// buffer instead of the screen, its rows as long as the ones it took when the
// movie started, and at unlock that picture is scaled to the menu area
// (wsMovies; 0: copied to the top left as it is). Not the screen
// itself: the intro starts while the screen is still 640x480 (rows of 1280
// bytes), before the menus' mode (854x480: 1712), and its frames, written
// with rows of the new length but the old values, came out as smeared bands.
static BYTE  g_mnLockCopy[0x40];
static bool  g_mnLockMovie = false;
static BYTE* g_mvBuf = nullptr;                   // the movie's own 640x480 frame
static int   g_mvPitch = 0;                       // its rows' length: the one the player took at its start
static int   g_mvBp = 2;
static void* WINAPI MnLock() {
    DWORD ret = (DWORD)__builtin_return_address(0);
    void* p = g_mnLock();
    g_mnLockReal = p; g_mnLockMovie = false;
    if (!p || IsBadReadPtr(p, sizeof(g_mnLockCopy))) return p;
    // the movie player's three lock calls (0x51789B, 0x517A36 at a movie's start, 0x524823)
    bool movie = ret == 0x5178A1 || ret == 0x517A3C || ret == 0x524829;
    bool mapped = g_mnXf || g_mnSoftFrame;        // the menus in a mode other than the 640x480 the game asked for
    int bp = g_mnRealBpp >= 24 ? 4 : 2;
    if (ret == 0x517A3C) g_mvPitch = mapped ? 640 * bp : *(int*)((BYTE*)p + 4);   // what it takes now
    if (!mapped) return p;
    if (!movie) {
        static int logged = 0;
        if (logged < 4) { logged++; LogfAsync("widescreen: frame locked from 0x%06lX in the menus - left as it is\n", (unsigned long)ret); }
        return p;
    }
    int pitch = g_mvPitch >= 640 * bp && g_mvPitch <= 4096 * 4 ? g_mvPitch : 640 * bp;
    static size_t have = 0;
    if (!g_mvBuf || have < (size_t)pitch * 480) {
        free(g_mvBuf); have = (size_t)pitch * 480;
        g_mvBuf = (BYTE*)calloc(have, 1);
        if (!g_mvBuf) { have = 0; return p; }
    }
    g_mvPitch = pitch; g_mvBp = bp;
    memcpy(g_mnLockCopy, p, sizeof(g_mnLockCopy));
    *(BYTE**)g_mnLockCopy = g_mvBuf; *(int*)(g_mnLockCopy + 4) = pitch;
    *(int*)(g_mnLockCopy + 0x0C) = 640; *(int*)(g_mnLockCopy + 0x10) = 480;
    g_mnLockMovie = ret != 0x517A3C;              // (at a movie's start it only reads the layout: nothing to show at its unlock)
    return g_mnLockCopy;
}
// scales a 640x480 picture to the menu area of the locked frame: the one at
// the frame's own top left, or the one at from (the movie's buffer)
static void MnScaleFrame(const BYTE* from, int fromPitch) {
    BYTE* bits = *(BYTE**)g_mnLockReal; int pitch = *(int*)((BYTE*)g_mnLockReal + 4);
    int bp = g_mnRealBpp >= 24 ? 4 : 2;
    if (!bits || pitch < g_mnRealW * bp || g_mnRealW < 640 || g_mnRealH < 480) return;
    if (!from) { from = bits; fromPitch = pitch; }
    BYTE*& src = g_mnSrc;
    if (!src) src = (BYTE*)malloc((size_t)640 * 480 * 4);
    if (!src) return;
    g_mnSrcBp = bp;
    for (int y = 0; y < 480; y++) memcpy(src + (size_t)y * 640 * bp, from + (size_t)y * fromPitch, (size_t)640 * bp);
    for (int y = 0; y < g_mnRealH; y++) memset(bits + (size_t)y * pitch, 0, (size_t)g_mnRealW * bp);
    int X0 = MnX(0), Y0 = MnY(0), X1 = MnX(640), Y1 = MnY(480);
    if (X0 < 0) X0 = 0;
    if (Y0 < 0) Y0 = 0;
    if (X1 > g_mnRealW) X1 = g_mnRealW;
    if (Y1 > g_mnRealH) Y1 = g_mnRealH;
    static int sxs[8192];
    if (X1 > 8192) X1 = 8192;
    for (int x = X0; x < X1; x++) { int sx = (int)((x - g_mnOx) / g_mnSx); sxs[x] = sx < 0 ? 0 : sx > 639 ? 639 : sx; }
    for (int y = Y0; y < Y1; y++) {
        int sy = (int)((y - g_mnOy) / g_mnSy); sy = sy < 0 ? 0 : sy > 479 ? 479 : sy;
        BYTE* drow = bits + (size_t)y * pitch; const BYTE* srow = src + (size_t)sy * 640 * bp;
        if (bp == 4) { DWORD* d = (DWORD*)drow; const DWORD* s = (const DWORD*)srow; for (int x = X0; x < X1; x++) d[x] = s[sxs[x]]; }
        else         { WORD* d = (WORD*)drow;   const WORD* s = (const WORD*)srow;   for (int x = X0; x < X1; x++) d[x] = s[sxs[x]]; }
    }
}
static void WINAPI MnUnlock(void* info) {
    if (info == g_mnLockCopy && g_mnLockReal) {
        if (g_mnLockMovie && g_mvBuf) {
            if (g_mnXf && g_cfg.wsMovies) MnScaleFrame(g_mvBuf, g_mvPitch);
            else {                                // at the top left (wsMovies=0; wsMenusSoft: scaled at the flip)
                BYTE* bits = *(BYTE**)g_mnLockReal; int pitch = *(int*)((BYTE*)g_mnLockReal + 4);
                if (bits && pitch >= 640 * g_mvBp)
                    for (int y = 0; y < 480; y++) memcpy(bits + (size_t)y * pitch, g_mvBuf + (size_t)y * g_mvPitch, (size_t)640 * g_mvBp);
            }
        }
        info = g_mnLockReal;
    }
    g_mnLockReal = nullptr; g_mnLockMovie = false;
    g_mnUnlock(info);
}

static int WINAPI MnSetMode(int idx, int depth, int flag) {
    int use = idx;
    if (g_mnFeCall && g_mnFeTarget > 0 && g_mnFeTarget != idx) {
        int* m = ModeEntryPtr(g_mnFeTarget);
        if (m) {                                    // the game's own depth / flag rule for that mode (0x4BEFDB)
            use = g_mnFeTarget;
            depth = m[5] > 2 ? 2 : m[5];
            flag = depth <= m[6] ? 1 : 0;
        }
    }
    int r = g_mnSetMode(use, depth, flag);
    if (r) {
        g_mnRealIdx = use;
        if (int* m = ModeEntryPtr(use)) { g_mnRealW = m[0]; g_mnRealH = m[1]; g_mnRealBpp = m[2]; }
    }
    if (use != idx) LogfAsync("widescreen: menus in display mode %d (%dx%d, %d-bit) instead of %d%s\n",
                              use, g_mnRealW, g_mnRealH, g_mnRealBpp, idx, r ? "" : " - FAILED");
    return r;
}

typedef FARPROC (WINAPI *GPA_t)(HMODULE, LPCSTR);
static GPA_t g_oGPA = nullptr;
static FARPROC WINAPI HkGetProcAddress(HMODULE m, LPCSTR name) {
    FARPROC p = g_oGPA(m, name);
    if (!p || !name || !HIWORD((DWORD)name) || strncmp(name, "_THRASH_", 8)) return p;
    if (m != g_mnMod) {
        g_mnMod = m;
        char fn[MAX_PATH] = {}; GetModuleFileNameA(m, fn, MAX_PATH);
        const char* b = strrchr(fn, '\\'); b = b ? b + 1 : fn;
        g_mnSoft = !_strnicmp(b, "soft", 4) || !_strnicmp(b, "null", 4);
        g_mnD3D = !_strnicmp(b, "d3d", 3); g_mnTex = nullptr;
        g_apc = g_cfg.alternatePixelCenter && g_mnD3D;
        if (g_cfg.alternatePixelCenter) LogfAsync("pixel centre: %s - %s\n", b, g_apc ? "every vertex moved by (-0.5, -0.5), the cockpit picture's left as they are" : "left as it is (Direct3D only)");
        if (g_mnSoft) LogfAsync("widescreen: %s - its menus %s\n", b, g_cfg.wsMenusSoft ? "scaled up frame by frame (wsMenusSoft)" : "stay 640x480");
    }
    struct W { const char* n; void** real; void* wr; };
    static const W kW[] = {
        { "_THRASH_drawpoint@4", (void**)&g_mnPoint, (void*)&MnPoint },
        { "_THRASH_drawline@8", (void**)&g_mnLine, (void*)&MnLine },
        { "_THRASH_drawtri@12", (void**)&g_mnTri, (void*)&MnTri },
        { "_THRASH_drawquad@16", (void**)&g_mnQuad, (void*)&MnQuad },
        { "_THRASH_drawtrifan@8", (void**)&g_mnFan, (void*)&MnFan },
        { "_THRASH_drawtristrip@8", (void**)&g_mnStrip, (void*)&MnStrip },
        { "_THRASH_drawlinestrip@8", (void**)&g_mnLStrip, (void*)&MnLStrip },
        { "_THRASH_drawpointstrip@8", (void**)&g_mnPStrip, (void*)&MnPStrip },
        { "_THRASH_drawtrimesh@12", (void**)&g_mnTriM, (void*)&MnTriM },
        { "_THRASH_drawquadmesh@12", (void**)&g_mnQuadM, (void*)&MnQuadM },
        { "_THRASH_drawlinemesh@12", (void**)&g_mnLineM, (void*)&MnLineM },
        { "_THRASH_drawpointmesh@12", (void**)&g_mnPointM, (void*)&MnPointM },
        { "_THRASH_clip@16", (void**)&g_mnClip, (void*)&MnClipW },
        { "_THRASH_setvideomode@12", (void**)&g_mnSetMode, (void*)&MnSetMode },
        { "_THRASH_pageflip@0", (void**)&g_mnFlip, (void*)&MnFlip },
        { "_THRASH_lockwindow@0", (void**)&g_mnLock, (void*)&MnLock },
        { "_THRASH_unlockwindow@4", (void**)&g_mnUnlock, (void*)&MnUnlock },
        { "_THRASH_setstate@8", (void**)&g_mnSetState, (void*)&MnSetState },
        { "_THRASH_settexture@4", (void**)&g_mnSetTex, (void*)&MnSetTex },
        { "_THRASH_tupdate@12", (void**)&g_mnTUpdate, (void*)&MnTUpdate },
    };
    if (!strcmp(name, "_THRASH_clearwindow@0")) g_mnClear = (TVoidFn_t)(void*)p;
    if (!strcmp(name, "_THRASH_about@0"))       g_mnAbout = (TAbout_t)(void*)p;
    for (const W& w : kW) if (!strcmp(name, w.n)) { *w.real = (void*)p; return (FARPROC)w.wr; }
    return p;
}

// ---- the mode calls ------------------------------------------------------------
static void SaveIniInt(const char* key, int value) {     // replaces "key=..." in HPFrameFix.ini (or adds it)
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%sHPFrameFix.ini", g_asiDir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    static char buf[65536];
    size_t n = 0, kl = strlen(key); char line[512]; bool done = false, full = false;
    while (fgets(line, sizeof(line), f)) {
        char out[600];
        const char* q = line; while (*q == ' ' || *q == '\t') q++;
        if (!done && !_strnicmp(q, key, kl) && (q[kl] == '=' || q[kl] == ' ' || q[kl] == '\t')) {
            snprintf(out, sizeof(out), "%s=%d\n", key, value); done = true;
        } else snprintf(out, sizeof(out), "%s", line);
        size_t l = strlen(out);
        if (n + l < sizeof(buf)) { memcpy(buf + n, out, l); n += l; } else full = true;
    }
    fclose(f);
    if (!done) {
        char out[96]; snprintf(out, sizeof(out), "%s=%d\n", key, value);
        size_t l = strlen(out);
        if (n + l < sizeof(buf)) { memcpy(buf + n, out, l); n += l; } else full = true;
    }
    if (full) { LogfAsync("ini: too big to update %s\n", key); return; }
    if ((f = fopen(path, "w"))) { fwrite(buf, 1, n, f); fclose(f); }
}
extern "C" { DWORD g_fnSetVideoMode = 0x4BEF50, g_fnGetSelMode = 0x4482F0, g_fnModeLookup = 0x4BEE80; int g_mnFeSite = 0; }
extern "C" int __cdecl GameCall0(DWORD fn);       // a game function, no arguments; all registers but eax kept
static bool MenusOn() {
    return g_knownExe && g_cfg.widescreen && g_cfg.wsMenus && (!g_mnSoft || g_cfg.wsMenusSoft) && g_mnAbout && g_mnSetMode;
}
// eax = the menu mode the game asks for; g_mnFeSite: 1 = startup
static void PzLimitsOff();
extern "C" void __cdecl MnBeforeFeMode(int feIdx) {
    PzLimitsOff();
    g_mnFeCall = 1; g_mnFeTarget = 0;
    if (!MenusOn()) return;
    int* fe = ModeEntryPtr(feIdx);
    int bpp = fe ? fe[2] : 16, w = 0, h = 0;
    // wsMenuResolution=1: the resolution chosen in the game's settings, whatever
    // it is - 640x480 too, which is then simply the game's own menu mode. At
    // startup the settings aren't loaded yet, so it's the size kept in the ini
    // from last time (none yet: the game's 640x480). The desktop's size is only
    // used when it's asked for (2).
    if (g_cfg.wsMenuResolution == 2) { w = g_dispW; h = g_dispH; }
    else {
        if (g_mnFeSite != 1) {
            int sel = GameCall0(g_fnGetSelMode);
            if (int* m = ModeEntryPtr(sel)) { w = m[0]; h = m[1]; }
        }
        if (w <= 0 || h <= 0) { w = g_cfg.wsRaceWidth; h = g_cfg.wsRaceHeight; }
    }
    int t = (w > 640 || h > 480) ? FindModeWH(w, h, bpp) : 0;
    g_mnFeTarget = t;
    // the game doesn't call the renderer when it asks for the mode it asked for last
    if (t > 0 && t != g_mnRealIdx && Rd<int>(0x55FEC0) == feIdx) *(volatile int*)0x55FEC0 = -1;
    static int lastLogged = -1;
    if (t != lastLogged) { lastLogged = t; LogfAsync("widescreen: menus wanted at %dx%d -> display mode %d\n", w, h, t); }
}
extern "C" void __cdecl MnAfterFeMode() {
    g_mnFeCall = 0;
    int W = g_mnRealW, H = g_mnRealH;
    bool on = MenusOn() && W > 0 && H > 0 && (W != 640 || H != 480) && g_mnFeTarget > 0 && g_mnRealIdx == g_mnFeTarget;
    if (on) {
        if (g_cfg.wsMenus == 2) { g_mnSx = W / 640.0f; g_mnSy = H / 480.0f; g_mnOx = g_mnOy = 0; }
        else {
            float s = W / 640.0f < H / 480.0f ? W / 640.0f : H / 480.0f;
            g_mnSx = g_mnSy = s; g_mnOx = (W - 640.0f * s) * 0.5f; g_mnOy = (H - 480.0f * s) * 0.5f;
        }
        if (g_mnSoft) { g_mnSoftFrame = true; on = false; }   // (drawn as it is, scaled at the flip)
        else if (g_mnClip) MnClipNow(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
    } else g_mnSoftFrame = false;
    g_mnXf = on;
    g_mnFeInset = on && g_mnD3D && (g_cfg.menuTexelInset || g_cfg.wsMenuFilter) && (g_mnSx != 1.0f || g_mnSy != 1.0f);
}
extern "C" void __cdecl MnBeforeRaceMode(int raceIdx) {
    g_mnFeCall = 0;
    PzLimitsOff();
    g_mnSoftFrame = false; g_mnFeInset = false;
    if (g_mnXf) { g_mnXf = false; if (g_mnClip) g_mnClip(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]); }
    // a race in the menus' own mode number (640x480) while the screen is in
    // another: the game would skip the call, so it's made to happen
    if (g_mnRealIdx && raceIdx != g_mnRealIdx && Rd<int>(0x55FEC0) == raceIdx) *(volatile int*)0x55FEC0 = -1;
}
extern "C" void __cdecl MnAfterRaceMode() {
    int W = Rd<int>(0x7CDAE0), H = Rd<int>(0x7CDAE4);
    if (W > 0 && H > 0 && (W != g_cfg.wsRaceWidth || H != g_cfg.wsRaceHeight)) {
        g_cfg.wsRaceWidth = W; g_cfg.wsRaceHeight = H;
        SaveIniInt("wsRaceWidth", W); SaveIniInt("wsRaceHeight", H);
        LogfAsync("widescreen: race resolution %dx%d kept for next time\n", W, H);
    }
}
// width -> mode (0x4BEE80 at the three calls): the remembered size if it's that
// width, else the size the game's own lookup found - as the list shows it
extern "C" int __cdecl MnModeLookup(int w, int found) {
    if (!g_cfg.wsUnlockResolutions) return found;     // (the game's own list: its own lookup fits it)
    int h = 0;
    if (w > 0 && w == g_cfg.wsRaceWidth && g_cfg.wsRaceHeight > 0) h = g_cfg.wsRaceHeight;
    else if (int* m = ModeEntryPtr(found)) { w = m[0]; h = m[1]; }
    int i = h ? FindModeWH(w, h, 0) : 0;
    if (i && i != found) LogfAsync("widescreen: saved resolution %dx%d -> display mode %d (the game's lookup: %d)\n", w, h, i, found);
    return i ? i : found;
}
// ---- the in-race menus (pause and what it leads to) -----------------------------
// RenderFrame (0x4BC680) draws them over the race picture when it's asked to
// (0x4BC7AB: call 0x44BDD0). They're laid out for 640x480 and the game scales
// that to the whole screen, so on a wide screen they're stretched. 0x44BDD0:
//  - 0x4A75E0 draws the frame: the tinted panels, the border, the title bar;
//  - 0x44A4F0 draws the items, layer by layer, each through its own draw
//    function (0x44A5DC: call [ebx+0x2C], ebx = the item);
//  - text isn't drawn on the spot: every letter is a quad added to one of two
//    lists, one per font (0x7F74C0 and 0x81B4D0: +4 = the next free vertex,
//    counting down from 0xFFF; vertices of 32 bytes from +0x10), which 0x4D1340
//    draws and empties (13 callers; here at 0x44BDFE);
//  - 0x434B60 draws the mouse pointer (0x44BE03).
// wsPauseMenu=1: the frame is left as it is, over the whole screen. Everything
// else keeps its 4:3 shape: x' = x * 4H/3W + an offset, through the draw
// wrappers above (sprites, as they're drawn) and in the lists (letters, before
// they're drawn). The offset puts an item in the middle block - what a 4:3
// screen shows, centred - except in the bottom row (y > 85% of the height),
// where an item that starts in the left or right 30% keeps to that side
// (Continue, Quit). One offset per item, decided by the first thing it draws,
// so an item and its text move as one. The pointer is placed by the same rule,
// so it's over what it points at. wsPauseMenu=2: all of it, frame too, in the
// middle block.
// The mouse: the game keeps its place in [0x5F38F0] (x) and [0x5F38F4] (y),
// held between the limits [0x5F38E0]..[0x5F38E8] (x) as it's read (0x435310).
// The pointer is drawn by the items' rule, so it's over what it points at -
// which squeezes the whole width the mouse can have into the middle block.
// So while an in-race menu is up (wsPauseMenu=1 or 2) the x limits are widened by
// what the block leaves at each side, counted in the game's own measure: the
// pointer then reaches the screen's edges. PzLimitsOff puts them back.
// Text: these menus take their letter sizes from the HUD's three (0x451530
// picks [0x791B4C], [0x791B50] or [0x791B54], which the HUD setup works out
// from the screen width at 0x484B7B), measure their strings with them and
// place and size things by what they measure - a button is its text plus a
// margin. The game's layout here is "everything times W/640", which the rule
// above undoes; but with the HUD in 4:3 proportions those sizes already are
// what a 4:3 screen has, and the letters (and the buttons around them) came
// out narrowed twice. So with both on:
//  - what the menus are told a string measures is its width over the factor
//    (0x452020 turns pixels into 640ths with the screen width at 0x452055,
//    which becomes the HUD's width; 0x451D37, the other way to measure, gets
//    the factor on its result): narrowed with the rest, that is the string's
//    real width again, so buttons and alignment come out as on a 4:3 screen;
//  - a string's letters keep their size and spacing, and only the place the
//    string starts is moved by the rule. For that each letter's quad is noted
//    as it's asked for (0x4D06F0: where on the line, which line, how big),
//    and letters that follow one another on a line are one string.
static bool g_pzOn = false, g_pzZones = false, g_pzXfSaved = false;
static float g_pzSave[4], g_pzK = 1, g_pzW = 0, g_pzH = 0;
static bool g_pzLimOn = false;
static int g_pzLimSave[2], g_pzLimSet[2];
static unsigned long g_pzLimFrame = 0;
static void PzLimitsOff() {
    if (!g_pzLimOn) return;
    g_pzLimOn = false;
    volatile int* mn = (volatile int*)0x5F38E0; volatile int* mx = (volatile int*)0x5F38E8; volatile int* x = (volatile int*)0x5F38F0;
    if (*mn != g_pzLimSet[0] || *mx != g_pzLimSet[1]) return;     // (the game has set its own since)
    *mn = g_pzLimSave[0]; *mx = g_pzLimSave[1];
    if (*x < *mn) *x = *mn;
    if (*x > *mx) *x = *mx;
}
extern "C" void __cdecl PzLimitsCheck() {         // every frame (PreRender): the menu has gone
    if (g_pzLimOn && (unsigned long)g_renders - g_pzLimFrame > 1) PzLimitsOff();
}
// The pointer's place is kept here, as g_pzU: the game's x it would have if the
// whole screen were the middle block's rule (screen x = PzMid() + U k), from
// -ext to W + ext. Each frame the movement the game added to its stored x since
// we last wrote it goes into U, and the stored x is set to what the game must
// test its items with (PzHit): U itself above the bottom row; in the bottom
// row, whose side parts are drawn at the screen's edges, the x of whatever is
// drawn under the pointer. So the pointer never jumps, and what's under it is
// what reacts. The arrow keys don't move the stored x a little: they put it
// on the next item's x (a jump of at least a tenth of the screen in one
// frame, which the mouse never makes). Added to U that left the pointer
// short of the bottom row's side parts, over nothing; so a jump puts U where
// that item is drawn instead (PzUFromGame, the other way round from PzHit).
static float g_pzU = 0; static int g_pzHitSet = 0;
static inline float PzMid() { return g_pzW * 0.5f * (1.0f - g_pzK); }   // where the middle block starts
static inline bool PzBottomRow() { return Rd<int>(0x5F38F4) > (int)(g_pzH * 0.85f); }
static float PzHit(float U) {
    if (!g_pzZones || !PzBottomRow()) return U;   // (wsPauseMenu=2: all of it is the middle block)
    float W = g_pzW, k = g_pzK, mid = PzMid(), P = mid + U * k;
    float l = W * 0.3f, r = W * 0.7f;             // (PzDecide's bounds)
    if (P < l * k + mid * 0.5f) return P / k;                 // left part, and its half of the gap
    if (P > W - (W - r) * k - mid * 0.5f) return W - (W - P) / k;   // right part likewise
    return U < l ? l : U > r ? r : U;                         // middle part
}
static float PzUFromGame(float G) {
    if (!g_pzZones || !PzBottomRow()) return G;
    float W = g_pzW, k = g_pzK, mid = PzMid(), l = W * 0.3f, r = W * 0.7f;
    float P = G < l ? G * k : G > r ? W - (W - G) * k : mid + G * k;   // where the game's x G is drawn
    return (P - mid) / k;
}
static void PzLimitsFrame() {                     // every frame the menu is drawn
    volatile int* mn = (volatile int*)0x5F38E0; volatile int* mx = (volatile int*)0x5F38E8; volatile int* x = (volatile int*)0x5F38F0;
    if (g_pzLimOn && (*mn != g_pzLimSet[0] || *mx != g_pzLimSet[1])) g_pzLimOn = false;   // (the game's own now)
    if (!g_pzLimOn) { g_pzLimSave[0] = *mn; g_pzLimSave[1] = *mx; g_pzLimOn = true; g_pzU = (float)*x; g_pzHitSet = *x; }
    int ext = (int)lrintf(g_pzW * 0.5f * (1.0f - g_pzK) / g_pzK);
    g_pzLimSet[0] = g_pzLimSave[0] - ext; g_pzLimSet[1] = g_pzLimSave[1] + ext;
    *mn = g_pzLimSet[0]; *mx = g_pzLimSet[1];
    int d = *x - g_pzHitSet;
    if ((float)abs(d) >= g_pzW * 0.1f) g_pzU = PzUFromGame((float)*x);   // (an arrow key: onto the item)
    else g_pzU += (float)d;
    if (g_pzU < (float)g_pzLimSet[0]) g_pzU = (float)g_pzLimSet[0];
    if (g_pzU > (float)g_pzLimSet[1]) g_pzU = (float)g_pzLimSet[1];
    g_pzHitSet = (int)lrintf(PzHit(g_pzU));
    *x = g_pzHitSet;
    g_pzLimFrame = (unsigned long)g_renders;
}
static const DWORD kPzLists[2] = { 0x7F74C0, 0x81B4D0 };
static int g_pzMark[2];
extern "C" { int g_pzOnC = 0; DWORD g_fnGlyphBody = 0x4D06F6, g_fnStrWidth = 0x4D1480; float g_pzMeasF = 1.0f; }
extern "C" void PzGlyphStub();
extern "C" void PzMeasureStub();
static bool g_pzRigid = false;                    // letters keep their size (the HUD's 4:3 sizes, on a wider screen)
// The screen where the HUD's positions are changed is one of these menus, and
// each thing to drag is an item (drawn by 0x45BCB0) that shows a piece of the
// HUD where its box is on the screen. Those are left where they are (g_pzPlain,
// for the one item), and while that screen is up (g_pzCustom: such an item was
// in the menu's last frame) the pointer is too, with the mouse's own limits:
// it is over a box when the game finds it over that box. The buttons keep to
// the middle block's rule; the game takes them for a little wider than drawn.
static bool g_pzPlain = false, g_pzCustom = false, g_pzCustomSeen = false;
// which list and its next free vertex when the letter began; pen x and letter size (floats, kept as their bits:
// nothing here may touch the FPU, the game has values on its stack), the line's y
struct PzGlyph { int list, idx; DWORD xBits, sizeBits; int y; };
static inline float PzF(DWORD bits) { float f; memcpy(&f, &bits, 4); return f; }
static PzGlyph g_pzGlyph[1024]; static int g_pzGlyphN = 0;
// a[0] = pen x (float), a[1] = the line's y, a[5] = "only measuring"
extern "C" void __cdecl PzGlyphNote(const DWORD* a) {
    int list = Rd<int>(0x840184);
    if (a[5] || g_pzGlyphN >= 1024 || list < 0 || list > 1) return;
    PzGlyph& r = g_pzGlyph[g_pzGlyphN++];
    r.list = list; r.idx = Rd<int>(kPzLists[list] + 4); r.xBits = a[0]; r.sizeBits = Rd<DWORD>(0x84018C); r.y = (int)a[1];
}
static void PzDecide(float x, float y) {
    g_pzPending = false;
    if (y > g_pzH * 0.85f) {
        if (x < g_pzW * 0.3f) g_mnOx = 0.0f;
        else if (x > g_pzW * 0.7f) g_mnOx = g_pzW * (1.0f - g_pzK);
    }
}
// the letters added since the last time: moved like the item they belong to
static void PzLetters() {
    if (g_pzPlain) {                              // (left where they are)
        for (int i = 0; i < 2; i++) g_pzMark[i] = Rd<int>(kPzLists[i] + 4);
        g_pzGlyphN = 0;
        return;
    }
    for (int i = 0; i < 2; i++) {
        int cur = Rd<int>(kPzLists[i] + 4), mark = g_pzMark[i];
        g_pzMark[i] = cur;
        if (cur < -1 || mark > 0xFFF || cur >= mark) continue;       // (nothing new, or the list was emptied)
        if (g_pzPending) { const float* v = (const float*)(kPzLists[i] + 0x10 + mark * 32); PzDecide(v[0], v[1]); }
        static unsigned char rigid[0x1000];
        bool anyRigid = false;
        if (g_pzRigid && g_pzGlyphN) {
            memset(rigid, 0, sizeof rigid);
            bool have = false; float x0 = 0, px = 0, py = 0, ps = 0;
            for (int g = 0; g < g_pzGlyphN; g++) {
                const PzGlyph& r = g_pzGlyph[g];
                if (r.list != i) continue;
                float x = PzF(r.xBits), y = (float)r.y, size = PzF(r.sizeBits);
                float sz = have && ps > size ? ps : size;
                if (!have || x < px - 0.5f || x - px > 40.0f * sz || fabsf(y - py) > 12.0f * sz) x0 = x;   // a new string
                have = true; px = x; py = y; ps = size;
                int after = cur;                                    // (a letter that made no quad - a space - has none to move)
                for (int h = g + 1; h < g_pzGlyphN; h++) if (g_pzGlyph[h].list == i) { after = g_pzGlyph[h].idx; break; }
                if (after != r.idx - 4) continue;
                for (int k = r.idx; k > r.idx - 4; k--) {
                    if (k <= cur || k > mark || k < 0 || k > 0xFFF) continue;
                    float* v = (float*)(kPzLists[i] + 0x10 + k * 32);
                    v[0] += g_mnOx - x0 * (1.0f - g_pzK);
                    rigid[k] = 1; anyRigid = true;
                }
            }
        }
        for (int k = cur + 1; k <= mark; k++) {
            if (anyRigid && k >= 0 && k <= 0xFFF && rigid[k]) continue;
            float* v = (float*)(kPzLists[i] + 0x10 + k * 32);
            v[0] = v[0] * g_pzK + g_mnOx;
        }
    }
    g_pzGlyphN = 0;
}
extern "C" void __cdecl PauseXfBegin() {
    g_pzOn = false; g_mnInset = false;
    if (g_mnXf || !g_mnClip) return;
    int W = Rd<int>(0x7CDAE0), H = Rd<int>(0x7CDAE4);
    if (W <= 0 || H <= 0) return;
    if (g_cfg.menuTexelInset && g_mnD3D && (W > 640 || H > 480)) {
        g_mnInset = true; g_mnInsetX = W / 640.0f; g_mnInsetY = H / 480.0f;
        static bool logged = false;
        if (!logged) { logged = true; LogfAsync("in-race menus: pictures sampled half a texel in from their edges (%dx%d)\n", W, H); }
    }
    // (the software renderer too: only the pictures' places and sizes change here, which it follows)
    if (!g_cfg.widescreen || !g_cfg.wsPauseMenu) return;
    double f = W * 3.0 / (H * 4.0);
    if (f <= 1.0001) return;
    g_pzSave[0] = g_mnSx; g_pzSave[1] = g_mnSy; g_pzSave[2] = g_mnOx; g_pzSave[3] = g_mnOy;
    g_pzK = (float)(1.0 / f); g_pzW = (float)W; g_pzH = (float)H;
    g_mnSx = g_pzK; g_mnSy = 1.0f; g_mnOx = PzMid(); g_mnOy = 0.0f;
    g_pzZones = g_cfg.wsPauseMenu == 1;
    g_pzPending = false; g_pzOn = true;
    g_pzGlyphN = 0; g_pzOnC = 1;
    g_pzRigid = g_hudIn && g_hudW > 0 && g_hudW < W;
    g_pzPlain = false; g_pzCustomSeen = false;
    if (g_pzCustom) PzLimitsOff();
    else PzLimitsFrame();
    for (int i = 0; i < 2; i++) g_pzMark[i] = Rd<int>(kPzLists[i] + 4);
    g_mnXf = !g_pzZones;                          // (the frame comes first: left alone with zones)
    if (g_mnXf) MnClipNow(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
}
// The pointer's draw (0x434B60) reads the mouse (0x435310 gives it [0x5F38F0],
// [0x5F38F4]), builds its quad there and drops it when a corner is off the
// screen - which, with the mouse's range widened, it is whenever the pointer
// should be outside the middle block. So for that one call the stored x is the
// place on the screen the items' rule gives the mouse (always on the screen),
// the wrappers only shrink the picture about that place, and afterwards the
// stored x is the game's own again, plus whatever the mouse moved meanwhile.
extern "C" void __cdecl PzItemBegin(const BYTE* item);
extern "C" void __cdecl PzItemEnd();
static bool g_pzPtrSub = false;
static int g_pzPtrL = 0, g_pzPtrP = 0;
extern "C" void __cdecl PzPtrBegin() {
    PzItemBegin(nullptr);
    if (!g_pzOn) return;
    // (wsPauseMenu=2: the menu is clipped to the middle block; the pointer may leave it)
    if (!g_pzZones && g_mnClip) g_mnClip(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
    if (g_pzCustom) { g_pzPlain = true; g_pzPending = false; g_mnXf = false; return; }
    volatile int* gx = (volatile int*)0x5F38F0;
    g_pzPtrL = *gx;
    float W = g_pzW, k = g_pzK, P = PzMid() + (g_pzU + (float)(g_pzPtrL - g_pzHitSet)) * k;
    P = P < 0 ? 0 : P > W ? W : P;
    g_pzPtrP = (int)lrintf(P);
    *gx = g_pzPtrP;
    g_pzPending = false;
    g_mnOx = (float)g_pzPtrP * (1.0f - k);
    g_pzPtrSub = true;
}
extern "C" void __cdecl PzPtrEnd() {
    if (g_pzPtrSub) {
        volatile int* gx = (volatile int*)0x5F38F0;
        *gx = g_pzPtrL + (*gx - g_pzPtrP);
        g_pzPtrSub = false;
    }
    PzItemEnd();
    if (g_pzOn && !g_pzZones && g_mnClip) MnClipNow(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
}
extern "C" void __cdecl PauseXfEnd() {
    g_mnInset = false;
    if (!g_pzOn) return;
    g_pzOn = false; g_pzPending = false; g_mnXf = false;
    g_pzOnC = 0;
    g_pzPlain = false; g_pzCustom = g_pzCustomSeen;
    g_mnSx = g_pzSave[0]; g_mnSy = g_pzSave[1]; g_mnOx = g_pzSave[2]; g_mnOy = g_pzSave[3];
    g_mnClip(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
}
// an item's draw (0x44A5DC), and the pointer's (0x44BE03)
extern "C" void __cdecl PzItemBegin(const BYTE* item) {
    if (!g_pzOn) return;
    g_pzPending = false; g_mnOx = PzMid();
    PzLetters();                                  // (letters from outside any item: the middle block)
    if (item && *(const DWORD*)(item + 0x2C) == 0x45BCB0) {   // a HUD box to drag (the item is the one about to be drawn)
        g_pzPlain = true; g_pzCustomSeen = true; g_mnXf = false;
        return;
    }
    g_pzPending = g_pzZones;
    g_mnXf = true;
}
extern "C" void __cdecl PzItemEnd() {
    if (!g_pzOn) return;
    PzLetters();
    if (g_pzPlain) { g_pzPlain = false; g_mnXf = true; }
    g_pzPending = false; g_mnOx = PzMid(); g_mnOy = 0.0f;
}
// the text flush (0x4D1340): the letters are placed first, and then drawn as they are
extern "C" void __cdecl PzFlushBegin() {
    if (!g_pzOn) return;
    PzLetters();
    g_pzXfSaved = g_mnXf; g_mnXf = false;
}
extern "C" void __cdecl PzFlushEnd() {
    if (!g_pzOn) return;
    g_mnXf = g_pzXfSaved;
    for (int i = 0; i < 2; i++) g_pzMark[i] = Rd<int>(kPzLists[i] + 4);
}
extern "C" { DWORD g_fnRaceMenuDraw = 0x44BDD0, g_fnPointerDraw = 0x434B60, g_fnTextFlushBody = 0x4D134B; }
extern "C" void PauseMenuStub();
extern "C" void PzItemStub();
extern "C" void PzPointerStub();
extern "C" void PzFlushStub();
extern "C" void MnFeStubStartup();
extern "C" void MnFeStub();
extern "C" void MnRaceStub();
extern "C" void MnLookupStub();
asm(".text\n"
    // a string measured for the in-race menus (0x451D37: call 0x4D1480)
    ".globl _PzMeasureStub\n"
    "_PzMeasureStub:\n"
    "    call *_g_fnStrWidth\n"
    "    fmuls _g_pzMeasF\n"
    "    ret\n"
    // a letter's quad (0x4D06F0, jmp from its entry; [esp+4] on = its arguments):
    // noted while an in-race menu is drawn, then its first six bytes and on
    ".globl _PzGlyphStub\n"
    "_PzGlyphStub:\n"
    "    cmpl $0, _g_pzOnC\n"
    "    je 1f\n"
    "    pushal\n"
    "    leal 0x24(%esp), %eax\n"
    "    pushl %eax\n"
    "    call _PzGlyphNote\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "1:  pushl %ecx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    pushl %ebp\n"
    "    movl %esp, %ebp\n"
    "    jmp *_g_fnGlyphBody\n"
    // RenderFrame's call of the in-race menu's draw (0x4BC7AB)
    ".globl _PauseMenuStub\n"
    "_PauseMenuStub:\n"
    "    pushal\n"
    "    call _PauseXfBegin\n"
    "    popal\n"
    "    call *_g_fnRaceMenuDraw\n"
    "    pushal\n"
    "    call _PauseXfEnd\n"
    "    popal\n"
    "    ret\n"
    // the menu draw loop's call of an item's draw (0x44A5DC: call [ebx+0x2C];
    // test eax, eax)
    ".globl _PzItemStub\n"
    "_PzItemStub:\n"
    "    pushal\n"
    "    pushl %ebx\n"
    "    call _PzItemBegin\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    call *0x2c(%ebx)\n"
    "    pushal\n"
    "    call _PzItemEnd\n"
    "    popal\n"
    "    testl %eax, %eax\n"
    "    ret\n"
    // the pointer's draw (0x44BE03): placed like an item
    ".globl _PzPointerStub\n"
    "_PzPointerStub:\n"
    "    pushal\n"
    "    call _PzPtrBegin\n"
    "    popal\n"
    "    call *_g_fnPointerDraw\n"
    "    pushal\n"
    "    call _PzPtrEnd\n"
    "    popal\n"
    "    ret\n"
    // the text flush (0x4D1340, jmp from its entry): its first 11 bytes are
    // here, the rest runs in place and returns to this stub
    ".globl _PzFlushStub\n"
    "_PzFlushStub:\n"
    "    pushal\n"
    "    call _PzFlushBegin\n"
    "    popal\n"
    "    call 1f\n"
    "    pushal\n"
    "    call _PzFlushEnd\n"
    "    popal\n"
    "    ret\n"
    "1:  pushl %edx\n"
    "    pushl %ebp\n"
    "    movl %esp, %ebp\n"
    "    testb $2, 0x7a3a58\n"
    "    jmp *_g_fnTextFlushBody\n"
    // int GameCall0(fn)
    ".globl _GameCall0\n"
    "_GameCall0:\n"
    "    pushal\n"
    "    movl 36(%esp), %ecx\n"
    "    call *%ecx\n"
    "    movl %eax, 28(%esp)\n"
    "    popal\n"
    "    ret\n"
    // the menu mode calls (eax = mode): startup 0x4B60FD, the others
    ".globl _MnFeStubStartup\n"
    "_MnFeStubStartup:\n"
    "    movl $1, _g_mnFeSite\n"
    "    jmp 1f\n"
    ".globl _MnFeStub\n"
    "_MnFeStub:\n"
    "    movl $0, _g_mnFeSite\n"
    "1:  pushal\n"
    "    pushl %eax\n"
    "    call _MnBeforeFeMode\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    call *_g_fnSetVideoMode\n"
    "    pushal\n"
    "    call _MnAfterFeMode\n"
    "    popal\n"
    "    ret\n"
    // the race's mode call 0x4A3422
    ".globl _MnRaceStub\n"
    "_MnRaceStub:\n"
    "    pushal\n"
    "    pushl %eax\n"
    "    call _MnBeforeRaceMode\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    call *_g_fnSetVideoMode\n"
    "    pushal\n"
    "    call _MnAfterRaceMode\n"
    "    popal\n"
    "    ret\n"
    // width -> mode 0x4BEE80 (eax = width, edx = ?; returns eax)
    ".globl _MnLookupStub\n"
    "_MnLookupStub:\n"
    "    pushl %eax\n"
    "    call *_g_fnModeLookup\n"
    "    pushal\n"
    "    pushl %eax\n"                  // found
    "    pushl 36(%esp)\n"              // width (pushed above pushal)
    "    call _MnModeLookup\n"
    "    addl $8, %esp\n"
    "    movl %eax, 28(%esp)\n"
    "    popal\n"
    "    addl $4, %esp\n"
    "    ret\n"
);
// The draw wrappers: the game gets the renderer's functions by name through
// GetProcAddress, which is where it's handed the wrappers instead. Hooked once,
// for whichever of the menus, the in-race menus and alternatePixelCenter wants it.
static bool InstallDrawWrappers() {
    static int state = -1;                        // -1 = not tried yet, else whether it's in
    if (state < 0) {
        void** slot = nullptr;
        ForEachImport("KERNEL32.dll", [&](void** s, const char* nm) { if (!strcmp(nm, "GetProcAddress")) slot = s; });
        state = slot && PatchPointer(slot, (void*)&HkGetProcAddress, (void**)&g_oGPA) ? 1 : 0;
    }
    return state == 1;
}
static void InstallMenus() {
    if (!g_cfg.wsMenus) Logf("widescreen: menus 640x480 (the game's)\n");
    if (!g_cfg.wsMenus && !g_cfg.wsPauseMenu) return;             // (menuTexelInset in a race needs wsPauseMenu's hook)
    g_dispW = GetSystemMetrics(SM_CXSCREEN); g_dispH = GetSystemMetrics(SM_CYSCREEN);
    bool ok = InstallDrawWrappers();
    if (g_cfg.wsPauseMenu) {
        BYTE j[11] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        DWORD rel = (DWORD)&PzFlushStub - (0x4D1340 + 5); memcpy(j + 1, &rel, 4);
        BYTE g[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
        rel = (DWORD)&PzGlyphStub - (0x4D06F0 + 5); memcpy(g + 1, &rel, 4);
        bool pz = ok && PatchRel32(0x44A5DC, 0xE8, (void*)&PzItemStub) && PatchRel32(0x44BE03, 0xE8, (void*)&PzPointerStub) &&
                  PatchBytes(0x4D1340, j, 11) && PatchBytes(0x4D06F0, g, 6) && PatchRel32(0x4BC7AB, 0xE8, (void*)&PauseMenuStub);
        if (pz && g_hudIn) {                      // (the HUD's letter sizes: strings measured to match)
            DWORD var = (DWORD)&g_hudW;
            g_pzMeasIn = PatchBytes(0x452057, &var, 4) && PatchRel32(0x451D37, 0xE8, (void*)&PzMeasureStub);
            if (!g_pzMeasIn) pz = false;
        }
        Logf("widescreen: in-race menus, %s: %s\n", g_cfg.wsPauseMenu == 2 ? "4:3 in the middle" : "frame over the screen, items in 4:3 proportions",
             pz ? "on" : "patch FAILED");
    }
    if (!g_cfg.wsMenus) return;
    ok = ok && PatchRel32(0x4B60FD, 0xE8, (void*)&MnFeStubStartup) && PatchRel32(0x4A35E8, 0xE8, (void*)&MnFeStub) &&
         PatchRel32(0x44B1E5, 0xE8, (void*)&MnFeStub) && PatchRel32(0x4A3422, 0xE8, (void*)&MnRaceStub) &&
         PatchRel32(0x4B610C, 0xE8, (void*)&MnLookupStub) && PatchRel32(0x472ECD, 0xE8, (void*)&MnLookupStub) &&
         PatchRel32(0x47327E, 0xE8, (void*)&MnLookupStub);
    if (ok && g_cfg.wsMenuFilter) {
        BYTE c[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
        DWORD rel = (DWORD)&MnFeFilterState - (0x4D7252 + 5); memcpy(c + 1, &rel, 4);
        Logf("widescreen: enlarged menus' big pictures filtered (Direct3D): %s\n", PatchBytes(0x4D7252, c, 6) ? "on" : "patch FAILED");
    }
    Logf("widescreen: menus at the %s resolution (%s): %s\n", g_cfg.wsMenuResolution == 2 ? "desktop's" : "chosen",
         g_cfg.wsMenus == 2 ? "stretched" : "4:3 in the middle", ok ? "on" : "patch FAILED");
}

void InstallWidescreen() {
    // (the cockpit picture is told apart by the wrapper around its draw call, whether or not anything else wants that)
    if (g_knownExe && g_cfg.alternatePixelCenter &&
        !(InstallDrawWrappers() && PatchRel32(0x482CA3, 0xE8, (void*)&XfWrapCockpit))) Logf("pixel centre: patch FAILED\n");
    if (!g_knownExe || !g_cfg.widescreen) { Logf("widescreen: off\n"); return; }
    if (g_cfg.wsUnlockResolutions) {
        BYTE b[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
        DWORD rel = (DWORD)&ModeFilterStub - (0x4BEDDD + 5); memcpy(b + 1, &rel, 4);
        Logf("widescreen: every display mode listed%s: %s\n", g_cfg.ws32bit ? " (32-bit too)" : "",
             PatchBytes(0x4BEDDD, b, 9) ? "on" : "patch FAILED");
    }
    if (g_cfg.wsCockpit)
        Logf("widescreen: cockpit picture keeps its shape: %s\n",
             PatchRel32(0x482CA3, 0xE8, (void*)&XfWrapCockpit) &&
             PatchRel32(0x4894EC, 0xE8, (void*)&XfNeedlesStub) ? "on" : "patch FAILED");
    if (g_cfg.wsHud) Logf("widescreen: HUD in 4:3 proportions: %s\n", InstallHud() ? "on" : "off");
    InstallMenus();
    if (g_cfg.wsLoading)
        Logf("widescreen: loading screen 4:3: %s\n",
             PatchRel32(0x4A3468, 0xE8, (void*)&XfWrapLoadScreen) ? "on" : "patch FAILED");
    if (g_cfg.wsFixFOV) {
        BYTE b[13] = { 0xE8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        DWORD rel = (DWORD)&WsProjStub - (0x4DBD2B + 5); memcpy(b + 1, &rel, 4);
        static const BYTE fld[3] = { 0xD9, 0x45, 0xF8 };
        bool ok = PatchBytes(0x4DBD3F, fld, 3) && PatchBytes(0x4DBD48, fld, 3) && PatchBytes(0x4DBD2B, b, 13);
        Logf("widescreen: Hor+ 3D view %s\n", ok ? "on" : "patch FAILED");
        if (ok) {
            static const BYTE nop = 0x90;
            Logf("widescreen: Hor+ for the cameras at the race's start and finish: %s\n",
                 PatchRel32(0x4DBEA4, 0xE8, (void*)&WsZoomStub) && PatchBytes(0x4DBEA9, &nop, 1) ? "on" : "patch FAILED");
            static const DWORD rows[] = { 0x4796FF, 0x47971D, 0x479A51, 0x479A6F, 0x479D33, 0x479D50, 0x479D6D };
            bool hz = true;
            for (DWORD a : rows) hz = hz && PatchRel32(a, 0xE8, (void*)&HorizonRowStub);
            hz = hz && PatchRel32(0x47A90E, 0xE8, (void*)&HorizonStub0) && PatchRel32(0x47A92D, 0xE8, (void*)&HorizonStub1) &&
                 PatchRel32(0x47A93D, 0xE8, (void*)&HorizonStub2);
            Logf("widescreen: horizon extended to the screen's edges: %s\n", hz ? "on" : "patch FAILED");
        }
    }
}
