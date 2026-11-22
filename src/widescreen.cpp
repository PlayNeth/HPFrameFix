#include "common.h"
#include "widescreen.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "fog.h"
#include "hud.h"
#include "nightlamps.h"

// ---------------------------------------------------------------------------
// Widescreen.
//  - Display modes: the options' mode list (0x4BED90) skips every mode where
//    w*3 != h*4 (jne at 0x4BEDF8); wsUnlockResolutions removes that test.
//  - 3D view: ProjectionSetup (0x4DBCE0; x, y, w, h, fov) takes half the FOV
//    (45 for the default 90) as the horizontal half angle and 13/16 of it as
//    the vertical one, and sets the focal lengths w / tan(h) and h / tan(v)
//    (0x4BF260; [0x56009C], [0x5600A0]) and the view frustum (0x4FD3F0, int
//    degrees). So any screen shows the same angles: a wider screen stretches
//    the 4:3 picture. Hor+: the horizontal half angle becomes
//    atan(tan(h) * (W/H) / (4/3)) for the screen (mode [0x7CDAE0] x [0x7CDAE4]),
//    the vertical one stays: the 4:3 view with more at the sides. Every view
//    (main, split screen, mirror) gets the same factor, so they keep their
//    original proportions relative to each other.
// ---------------------------------------------------------------------------

extern "C" { int g_xfNeedle = 0; DWORD g_fnNeedles = 0x4880B0; }
extern "C" { int g_wsMirror = 0, g_mirrorLayout = 0; DWORD g_fnViewRectsTail = 0x4BE3D0, g_fnRectProj = 0x4BED20; }
bool g_mirrorNarrowed = false;             // the mirror's rect as it stands was narrowed (MirrorRectFix)
// Rain and snow (0x4DEE36 / 0x4DED7A, when [0x8CA22C] isn't 0) are drawn in a
// pass of their own with a 120 degree view (0x4DBDA0: edx = 0x78), and their
// flakes live in a space made for that view. Widened like the rest, the screen's
// outer parts show what lies beyond that space, where flakes close to the
// camera come out as big white blocks at the screen's edge. So that one pass
// keeps the game's own horizontal angle, and to keep the flakes' shape the
// vertical one is narrowed instead (tan(v) / factor): the same snow over the
// whole width, a little larger (wsWeather).
// The horizon (0x47A880: its parts 0x479680, 0x4799D0 and 0x479B90, set up
// from the track's .hrz files, with their own night and weather versions) is
// a ring of 16 pieces laid out flat: each part draws 8 of them as a strip
// facing the camera, its rows 9 points each (0x479500 lines them up, 0x479370
// projects a row), the pieces as wide as a sixteenth of the ring's
// circumference. The strip covers about 57 degrees either side of the view's
// middle and slides by up to a piece as the camera turns (which pieces it
// shows follows the heading, 0x479160), so its right end can be as close as
// 45 degrees: enough for the game's 90 degree view, not for a wider one. At
// night and on snow tracks the sky behind it is darker than the horizon's
// glow, and the right side of the screen showed a dark rectangle where the
// strip ended. So on a screen wider than 4:3 each part is drawn twice more,
// its strip moved by its own length to the right and to the left (its rows'
// points moved before they're projected), the pieces' textures moved by 8 as
// well (the ring's rotation [0x55D1CC] + 0.5): three strips end to end, the
// ring's next pieces either side, at the same depth.
extern "C" { float g_wsHalfF = 45.0f, g_wsHalfVF = 36.0f; int g_wsVertI = 36; }
static double WsFactor() {
    int w = Rd<int>(0x7CDAE0), h = Rd<int>(0x7CDAE4);
    if (w <= 0 || h <= 0) return 1.0;
    return (w * 3.0) / (h * 4.0);
}
extern "C" int __cdecl WsHorPlus(int half, int vert) {
    if (g_cfg.wsHud) HudUpdate();                 // the HUD width follows the view too
    g_wsHalfVF = (float)vert; g_wsVertI = vert;
    double f = g_wsMirror && g_mirrorNarrowed ? 1.0 : WsFactor();   // (a narrowed mirror has its 4:3 shape: the game's own angle)
    if (f <= 1.0001 || half <= 0 || half >= 89) { g_wsHalfF = (float)half; return half; }
    const double kDeg = 3.14159265358979323846 / 180.0;
    if (half == 60 && g_cfg.wsWeather && Rd<int>(0x8CA22C) != 0 && vert > 0 && vert < 89) {
        double vv = atan(tan(vert * kDeg) / f) / kDeg;
        g_wsHalfF = (float)half; g_wsHalfVF = (float)vv; g_wsVertI = (int)ceil(vv);
        static bool logged = false;
        if (!logged) { logged = true; LogfAsync("widescreen: rain and snow keep their %d degree width (vertical half angle %d -> %.1f)\n", half * 2, vert, vv); }
        return half;
    }
    double hh = atan(tan(half * kDeg) * f) / kDeg;
    if (hh > 88.0) hh = 88.0;
    g_wsHalfF = (float)hh;
    static int lastHalf = 0; static double lastF = 0;
    if (half != lastHalf || fabs(f - lastF) > 1e-6) {
        lastHalf = half; lastF = f;
        LogfAsync("widescreen: %dx%d - horizontal FOV %d -> %.1f degrees\n",
                  Rd<int>(0x7CDAE0), Rd<int>(0x7CDAE4), half * 2, hh * 2);
    }
    return (int)ceil(hh);
}
// A camera with a zoom of its own (cam+0x54: the cameras at the start and the
// finish of a race) doesn't use the angle: after ProjectionSetup, ViewSetup
// (0x4DBE8F) sets the float focal lengths [0x56009C] and [0x5600A0] to half
// the view's width and height in pixels times the zoom. On a wider screen x
// came out as many times longer as the screen is wider than 4:3, and the
// picture was stretched. x is divided by that factor, which gives the 4:3
// proportions, as WsHorPlus does with the angle.
extern "C" void __cdecl WsZoomFocal() {
    double f = WsFactor();
    if (f > 1.0001) *(volatile float*)0x56009C = (float)(Rd<float>(0x56009C) / f);
}
// a horizon part (Watcom: eax = the horizon), then on a wider screen its two more strips
static inline void HorizonCall(DWORD fn, DWORD hz) {
    __asm__ volatile("call *%1" : "+a"(hz) : "r"(fn) : "memory", "cc");
}
extern "C" { int g_hzShift = 0; DWORD g_fnHzRow = 0x479370; }
static int g_hzRow = 0;                           // rows projected so far in this drawing of the part
extern "C" void __cdecl HorizonDraw(DWORD fn, DWORD hz) {
    HorizonCall(fn, hz);
    if (WsFactor() <= 1.0001) return;
    volatile float* rot = (volatile float*)0x55D1CC;
    float save = *rot;
    *rot = save + 0.5f;
    g_hzShift = 1;  g_hzRow = 0; HorizonCall(fn, hz);
    g_hzShift = -1; g_hzRow = 0; HorizonCall(fn, hz);
    g_hzShift = 0;
    *rot = save;
}
// a row's points (0x479370: eax = count, ebx = 3 floats each) moved by the strip's
// length times g_hzShift, in a copy: the game's own points stay as they are. The
// length comes from the part's first row, the strip's edge, all 9 points set: in
// 0x479B90 the other two rows are worked out piece by piece, 8 points, the 9th
// never written.
extern "C" const float* __cdecl HorizonRow(int n, const float* p) {
    static float row[9 * 3], d[3];
    if (n != 9) return p;
    if (g_hzRow++ == 0)
        for (int c = 0; c < 3; c++) d[c] = (p[8 * 3 + c] - p[c]) * (float)g_hzShift;
    for (int i = 0; i < 9 * 3; i++) row[i] = p[i] + d[i % 3];
    return row;
}
// ---------------------------------------------------------------------------
// 2D pictures drawn with a transform. Between XfBegin(mode) and XfEnd the
// THRASH draw calls (drawquad [0x9EF94C], drawtri [0x9EF974], drawtrifan
// [0x9EF960]) go through wrappers that copy the vertices (32 bytes each,
// screen x at +0, y at +4) and move them, and THRASH_clip [0x9EF92C]
// (x0, y0, x1, y1; set by 0x4BEBE0, e.g. around the steering wheel) gets
// the same move so the clipped parts line up with the moved picture:
//  1 = 4:3 in the middle: x' = cx + (x - cx) * 4H / 3W (loading screen, whose
//      picture and bar the game scales to the whole screen: 0x494FA0, 0x495230);
//  2 = zoom: y' = cy + (y - cy) * 3W / 4H, x unchanged (cockpit picture
//      0x489240, scaled by the game to the screen's width and height): it keeps
//      its shape, centred, its top and bottom beyond the screen. The needles
//      (0x4880B0) are placed and sized with W/640 on both axes - already the
//      zoomed size, but from the top - so they only move: y' = y - cy (k - 1).
// The table entries are swapped in for the call only; nothing else is touched.
// ---------------------------------------------------------------------------

typedef int  (WINAPI *TClip_t)(int, int, int, int);
static TDraw4_t g_oQuad; static TDraw3_t g_oTri; static TDrawN_t g_oFan; static TClip_t g_oClip;
static int g_xfMode = 0, g_xfDepth = 0, g_xfW, g_xfH;
bool g_xfCockpit = false;                  // the cockpit picture (0x489240) is being drawn
static float g_xfCx, g_xfCy, g_xfK;
static inline void XfVertex(BYTE* dst, const void* src) {
    memcpy(dst, src, kVtx);
    float* v = (float*)dst;
    if (g_xfMode == 1)  v[0] = g_xfCx + (v[0] - g_xfCx) * g_xfK;
    else if (g_xfNeedle) v[1] += g_xfCy * (1.0f - g_xfK);     // already at the zoomed size
    else                v[1] = g_xfCy + (v[1] - g_xfCy) * g_xfK;
}
static int XfClipCoord(int c, float mid, int lim) {
    double r = floor(mid + (c - mid) * g_xfK + 0.5);
    return r < 0 ? 0 : r > lim ? lim : (int)r;
}
static int WINAPI XfClip(int x0, int y0, int x1, int y1) {
    if (g_xfMode == 1) { x0 = XfClipCoord(x0, g_xfCx, g_xfW); x1 = XfClipCoord(x1, g_xfCx, g_xfW); }
    else               { y0 = XfClipCoord(y0, g_xfCy, g_xfH); y1 = XfClipCoord(y1, g_xfCy, g_xfH); }
    return g_oClip(x0, y0, x1, y1);
}
// d3da draws with D3DDP_DONOTCLIP, so a vertex moved off the screen gives
// garbage (stray triangles across the picture): a polygon that leaves the
// screen is cut at its edges here (x, y, z, rhw and colours interpolated in
// screen space, u/v perspective-correct through rhw) and drawn as a fan.
struct XfV { float x, y, z, rhw; DWORD col, spec; float u, v; };
static XfV XfLerp(const XfV& a, const XfV& b, float t) {
    XfV r;
    r.x = a.x + (b.x - a.x) * t; r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t; r.rhw = a.rhw + (b.rhw - a.rhw) * t;
    DWORD c = 0, s = 0;
    for (int k = 0; k < 32; k += 8) {
        float ca = (float)((a.col >> k) & 255), cb = (float)((b.col >> k) & 255);
        float sa = (float)((a.spec >> k) & 255), sb = (float)((b.spec >> k) & 255);
        c |= (DWORD)(int)(ca + (cb - ca) * t + 0.5f) << k;
        s |= (DWORD)(int)(sa + (sb - sa) * t + 0.5f) << k;
    }
    r.col = c; r.spec = s;
    if (a.rhw > 0 && b.rhw > 0 && r.rhw > 0) {
        r.u = (a.u * a.rhw + (b.u * b.rhw - a.u * a.rhw) * t) / r.rhw;
        r.v = (a.v * a.rhw + (b.v * b.rhw - a.v * a.rhw) * t) / r.rhw;
    } else { r.u = a.u + (b.u - a.u) * t; r.v = a.v + (b.v - a.v) * t; }
    return r;
}
// one edge: axis 0 = x, 1 = y; keep the side where sign * (coord - lim) <= 0
static int XfClipEdge(const XfV* in, int n, XfV* out, int axis, float lim, float sign) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const XfV& a = in[i]; const XfV& b = in[(i + 1) % n];
        float da = sign * ((axis ? a.y : a.x) - lim), db = sign * ((axis ? b.y : b.x) - lim);
        if (da <= 0) out[m++] = a;
        if ((da < 0 && db > 0) || (da > 0 && db < 0)) out[m++] = XfLerp(a, b, da / (da - db));
    }
    return m;
}
// The software renderer, given the triangles and fans that the cut below makes
// of a picture's pieces, draws a black line along each piece's far edges: there
// it reads the texture one texel past its end (u or v = 1). It does take u/v
// from a triangle's corners - which it doesn't for a quad, where it always
// shows the whole texture, so the pieces can't be cut and kept as quads. So
// for it a piece's highest u and v are pulled in (texel: by how much, 1/256 = a
// texel of a 256 texture), and its corners go to whole pixels, so two pieces
// share their edge exactly. It also leaves the last row of a triangle's pixels
// undrawn - a see-through line where one piece ends and the next begins - so a
// piece's bottom edge (and its right one, with overlapX) goes a pixel further;
// the piece after it draws over that.
bool g_softRenderer = false;               // the renderer DLL loaded last is the software one (HkLoadLibraryA)
static void XfSoftPrep(XfV* v, int n, float texel, bool overlapX) {
    float u0 = v[0].u, u1 = v[0].u, w0 = v[0].v, w1 = v[0].v;
    for (int i = 0; i < n; i++) { v[i].x = floorf(v[i].x + 0.5f); v[i].y = floorf(v[i].y + 0.5f); }
    float x0 = v[0].x, x1 = v[0].x, y0 = v[0].y, y1 = v[0].y;
    for (int i = 1; i < n; i++) {
        if (v[i].u < u0) u0 = v[i].u;
        if (v[i].u > u1) u1 = v[i].u;
        if (v[i].v < w0) w0 = v[i].v;
        if (v[i].v > w1) w1 = v[i].v;
        if (v[i].x < x0) x0 = v[i].x;
        if (v[i].x > x1) x1 = v[i].x;
        if (v[i].y < y0) y0 = v[i].y;
        if (v[i].y > y1) y1 = v[i].y;
    }
    for (int i = 0; i < n; i++) {
        if (u1 - u0 > 4 * texel && v[i].u == u1) v[i].u -= texel;
        if (w1 - w0 > 4 * texel && v[i].v == w1) v[i].v -= texel;
        if (y1 - y0 > 2 && v[i].y == y1) v[i].y += 1.0f;
        if (overlapX && x1 - x0 > 2 && v[i].x == x1) v[i].x += 1.0f;
    }
}
static void XfDraw(XfV* v, int n) {                // already moved
    bool out = false;
    for (int i = 0; i < n && !out; i++)
        out = v[i].x < 0 || v[i].y < 0 || v[i].x > (float)g_xfW || v[i].y > (float)g_xfH;
    if (out && g_softRenderer && !g_xfNeedle) XfSoftPrep(v, n, 1.0f / 256.0f, false);
    if (!out) {
        if (n == 4)      g_oQuad(&v[0], &v[1], &v[2], &v[3]);
        else if (n == 3) g_oTri(&v[0], &v[1], &v[2]);
        else             g_oFan(n - 2, v);
        return;
    }
    // each triangle as the driver would draw it (0, i, i + 1: quads and fans
    // alike), so the corner order of a quad doesn't matter
    for (int i = 1; i + 1 < n; i++) {
        XfV a[8], b[8], t[3] = { v[0], v[i], v[i + 1] };
        int m = XfClipEdge(t, 3, a, 1, 0.0f, -1.0f);   if (m < 3) continue;
        m = XfClipEdge(a, m, b, 1, (float)g_xfH, 1.0f); if (m < 3) continue;
        m = XfClipEdge(b, m, a, 0, 0.0f, -1.0f);       if (m < 3) continue;
        m = XfClipEdge(a, m, b, 0, (float)g_xfW, 1.0f); if (m < 3) continue;
        if (m == 3) g_oTri(&b[0], &b[1], &b[2]); else g_oFan(m - 2, b);
    }
}
static void WINAPI XfQuad(void* a, void* b, void* c, void* d) {
    XfV v[4];
    XfVertex((BYTE*)&v[0], a); XfVertex((BYTE*)&v[1], b); XfVertex((BYTE*)&v[2], c); XfVertex((BYTE*)&v[3], d);
    XfDraw(v, 4);
}
static void WINAPI XfTri(void* a, void* b, void* c) {
    XfV v[3];
    XfVertex((BYTE*)&v[0], a); XfVertex((BYTE*)&v[1], b); XfVertex((BYTE*)&v[2], c);
    XfDraw(v, 3);
}
// drawtrifan(triangles, vertices): triangles + 2 vertices in a row.
static void WINAPI XfFan(int tris, void* verts) {
    static XfV v[64];                              // game thread only
    int n = tris + 2;
    if (tris <= 0 || n > 64) { g_oFan(tris, verts); return; }
    for (int i = 0; i < n; i++) XfVertex((BYTE*)&v[i], (BYTE*)verts + i * kVtx);
    XfDraw(v, n);
}
extern "C" void __cdecl XfBegin(int mode) {
    if (g_xfDepth++) return;                      // nested: the outer mode stays
    g_xfCockpit = mode == 2;
    FogPause(true);                               // 2D pictures: no fog
    if (!g_cfg.widescreen || (mode == 2 && !g_cfg.wsCockpit) || (mode == 1 && !g_cfg.wsLoading)) { g_xfMode = 0; return; }
    int w = Rd<int>(0x7CDAE0), h = Rd<int>(0x7CDAE4);
    double f = (w > 0 && h > 0) ? (w * 3.0) / (h * 4.0) : 1.0;
    // zoom only for the single full-screen view (split screen: [0x6FD3B0] = 1)
    if (f <= 1.0001 || (mode == 2 && Rd<int>(0x6FD3B0) == 1)) { g_xfMode = 0; return; }
    g_xfMode = mode;
    g_xfW = w; g_xfH = h; g_xfCx = w * 0.5f; g_xfCy = h * 0.5f;
    g_xfK = mode == 1 ? (float)(1.0 / f) : (float)f;
    g_oQuad = Rd<TDraw4_t>(0x9EF94C); g_oTri = Rd<TDraw3_t>(0x9EF974); g_oFan = Rd<TDrawN_t>(0x9EF960);
    g_oClip = Rd<TClip_t>(0x9EF92C);
    if (!g_oQuad || !g_oTri || !g_oFan || !g_oClip) { g_xfMode = 0; return; }
    *(volatile DWORD*)0x9EF94C = (DWORD)&XfQuad;
    *(volatile DWORD*)0x9EF974 = (DWORD)&XfTri;
    *(volatile DWORD*)0x9EF960 = (DWORD)&XfFan;
    *(volatile DWORD*)0x9EF92C = (DWORD)&XfClip;
}
extern "C" void __cdecl XfEnd() {
    if (!g_xfDepth || --g_xfDepth) return;
    g_xfCockpit = false;
    FogPause(false);
    if (!g_xfMode) return;
    if (Rd<DWORD>(0x9EF94C) == (DWORD)&XfQuad) *(volatile DWORD*)0x9EF94C = (DWORD)g_oQuad;
    if (Rd<DWORD>(0x9EF974) == (DWORD)&XfTri)  *(volatile DWORD*)0x9EF974 = (DWORD)g_oTri;
    if (Rd<DWORD>(0x9EF960) == (DWORD)&XfFan)  *(volatile DWORD*)0x9EF960 = (DWORD)g_oFan;
    if (Rd<DWORD>(0x9EF92C) == (DWORD)&XfClip) *(volatile DWORD*)0x9EF92C = (DWORD)g_oClip;
    g_xfMode = 0;
}

// ---------------------------------------------------------------------------
// The options' mode list (0x4BED90) walks the renderer's modes (table [ebp-4]:
// count +0x3C, entries +0x40, 40 bytes each: w, h, bpp, ?, flags, +0x14, +0x18)
// and lists a mode if +0x14 >= 2, w*3 == h*4, bpp 15 or 16, flags bit 0 set and
// bit 1 clear, and w*h*2 <= half the video memory the game assumes for its
// driver ([0x55FED0], 4 MB or less: nothing above about 1024x768). The menu
// list (0x448210) holds 20 entries. ModeOk decides instead, once per list:
// the game's +0x14 and flag tests, at least 640x480, any shape, no memory test,
// each resolution once (32-bit preferred with ws32bit, else 16-bit), and when
// there are more than 20, 640x480 plus the 19 largest.
// ---------------------------------------------------------------------------
static bool g_modeKeep[256];
// a mode the list may show (the game's +0x14 and flag tests, at least 640x480,
// 15/16-bit, or 24/32-bit with ws32bit); 0 = no, else its rank (higher = preferred)
// The software renderer (softtria.dll) lists 32-bit modes too but only ever
// writes 16-bit pixels: in a 32-bit mode its picture fills the left half of the
// screen in wrong colours. So for it only the 15/16-bit modes count.
int ModeRank(const int* m) {
    int w = m[0], h = m[1], bpp = m[2], flags = m[4], f14 = m[5];
    if (f14 < 2 || !(flags & 1) || (flags & 2) || w < 640 || h < 480) return 0;
    // (and it draws whole blocks of 16 columns: at 854 wide the picture is 848
    // with a black strip, and the HUD's right edge is cut. Only widths it fills.)
    if (g_softRenderer && (w & 15)) return 0;
    if (bpp == 15 || bpp == 16) return bpp == 16 ? 2 : 1;
    if (g_cfg.ws32bit && !g_softRenderer && (bpp == 32 || bpp == 24)) return bpp == 32 ? 4 : 3;
    return 0;
}
extern "C" int __cdecl ModeOk(int index, BYTE* table) {
    if (index == 1) {                                // first entry: decide the whole list
        memset(g_modeKeep, 0, sizeof(g_modeKeep));
        int n = *(int*)(table + 0x3C);
        BYTE* e = *(BYTE**)(table + 0x40);
        if (n > 255) n = 255;
        struct M { int i, w, h, bpp; } best[256]; int nb = 0;
        for (int i = 1; i <= n; i++) {
            int* m = (int*)(e + i * 40);
            int w = m[0], h = m[1], bpp = m[2];
            int rank = ModeRank(m);
            if (!rank) continue;
            int k = 0; while (k < nb && !(best[k].w == w && best[k].h == h)) k++;
            if (k == nb) { best[nb++] = { i, w, h, bpp }; continue; }
            int rk = ModeRank((int*)(e + best[k].i * 40));
            if (rank > rk) best[k] = { i, w, h, bpp };
        }
        while (nb > 20) {                            // drop the smallest (never 640x480)
            int s = -1;
            for (int k = 0; k < nb; k++) {
                if (best[k].w == 640 && best[k].h == 480) continue;
                if (s < 0 || best[k].w * best[k].h < best[s].w * best[s].h) s = k;
            }
            if (s < 0) break;
            best[s] = best[--nb];
        }
        for (int k = 0; k < nb; k++) g_modeKeep[best[k].i] = true;
        LogfAsync("widescreen: %d display modes listed\n", nb);
    }
    return index >= 0 && index < 256 && g_modeKeep[index];
}

asm(".text\n"
    // a game function called with XfBegin(mode) / XfEnd around it, all
    // registers passed through: the call site's "call target" becomes
    // "call _XfWrapN" (N = which).
    ".globl _XfWrapCockpit\n"
    "_XfWrapCockpit:\n"
    "    pushal\n"
    "    pushl $2\n"
    "    call _XfBegin\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movl $0x489240, _g_xfTarget\n"
    "    jmp 6f\n"
    // cockpit needles (0x4894EC, call 0x4880B0 inside 0x489240; eax = car)
    ".globl _XfNeedlesStub\n"
    "_XfNeedlesStub:\n"
    "    movl $1, _g_xfNeedle\n"
    "    call *_g_fnNeedles\n"
    "    movl $0, _g_xfNeedle\n"
    "    ret\n"
    ".globl _XfWrapLoadScreen\n"
    "_XfWrapLoadScreen:\n"
    "    pushal\n"
    "    pushl $1\n"
    "    call _XfBegin\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movl $0x494FA0, _g_xfTarget\n"
    "6:  call *_g_xfTarget\n"
    "    pushal\n"
    "    call _XfEnd\n"
    "    popal\n"
    "    ret\n"
    // mode list filter (0x4BEDDD): esi = index, edi = w, ebx = h, ecx = bpp,
    // edx = [+0x14], [ebp-8] = flags, [ebp-4] = the THRASH mode table
    ".globl _ModeFilterStub\n"
    "_ModeFilterStub:\n"
    "    pushal\n"
    "    pushl -4(%ebp)\n"
    "    pushl %esi\n"
    "    call _ModeOk\n"
    "    addl $8, %esp\n"
    "    testl %eax, %eax\n"
    "    popal\n"
    "    je 7f\n"
    "    pushl $0x4BEE32\n"           // listed: on to the callback
    "    ret\n"
    "7:  pushl $0x4BEE63\n"           // skipped
    "    ret\n"
    // ViewSetup, a camera with a zoom (0x4DBEA4: fstp [0x56009C], 6 bytes): its x focal length
    ".globl _WsZoomStub\n"
    "_WsZoomStub:\n"
    "    fstps 0x56009C\n"
    "    pushal\n"
    "    call _WsZoomFocal\n"
    "    popal\n"
    "    ret\n"
    // a horizon row projected (the 7 calls to 0x479370): on the extra strips its points moved
    ".globl _HorizonRowStub\n"
    "_HorizonRowStub:\n"
    "    cmpl $0, _g_hzShift\n"
    "    je 1f\n"
    "    pushal\n"
    "    pushl %ebx\n"
    "    pushl %eax\n"
    "    call _HorizonRow\n"
    "    addl $8, %esp\n"
    "    movl %eax, 0x10(%esp)\n"     // the saved ebx
    "    popal\n"
    "1:  jmp *_g_fnHzRow\n"
    // the horizon's parts (calls at 0x47A90E, 0x47A92D, 0x47A93D; eax = the horizon)
    ".globl _HorizonStub0\n"
    "_HorizonStub0:\n"
    "    pushl $0x479680\n"
    "    jmp 1f\n"
    ".globl _HorizonStub1\n"
    "_HorizonStub1:\n"
    "    pushl $0x4799D0\n"
    "    jmp 1f\n"
    ".globl _HorizonStub2\n"
    "_HorizonStub2:\n"
    "    pushl $0x479B90\n"
    "1:  pushal\n"
    "    pushl %eax\n"                // the horizon
    "    pushl 0x24(%esp)\n"          // the part
    "    call _HorizonDraw\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    addl $4, %esp\n"
    "    ret\n"
    // ProjectionSetup 0x4DBD2B (replaces "mov edx,[ebp-0x18] / mov eax,esi /
    // mov [ebp-8],esi / call 0x4FD3F0"; esi = half the FOV in degrees): Hor+.
    // [ebp-8] gets the horizontal half FOV as a float (its fild is now a fld).
    ".globl _WsProjStub\n"
    "_WsProjStub:\n"
    "    pushl %ecx\n"
    "    pushl -0x18(%ebp)\n"       // vertical half FOV (int)
    "    pushl %esi\n"
    "    call _WsHorPlus\n"          // eax = frustum half FOV (int); g_wsHalfF, g_wsHalfVF, g_wsVertI set
    "    addl $8, %esp\n"
    "    popl %ecx\n"
    "    movl _g_wsHalfF, %edx\n"
    "    movl %edx, -8(%ebp)\n"
    "    movl _g_wsHalfVF, %edx\n"
    "    movl %edx, -0x18(%ebp)\n"   // (a float now: its fild at 0x4DBD48 is a fld too)
    "    movl _g_wsVertI, %edx\n"    // vertical half FOV for the frustum (int)
    "    pushl $1f\n"
    "    pushl $0x4FD3F0\n"          // frustum (eax, edx)
    "    ret\n"
    "1:  ret\n");
