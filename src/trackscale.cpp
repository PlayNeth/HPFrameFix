#include "common.h"
#include "trackscale.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "nightlamps.h"

// ---------------------------------------------------------------------------
// Track scale. High Stakes brought this game's tracks over shrunk by exactly
// 0.7692, every position in them (Hometown's .frd vertices, block centres and
// road nodes, its heights.sim, its trackside cameras); this does the same, by
// each track's size (below), as each track loads. Each file is scaled in the
// buffer it's loaded into, before the game reads anything out of it, so all
// the game works out from them follows. Each is walked twice: once only to
// check that its layout is the one expected, to its last byte, then to scale
// it; a file that doesn't fit is left as it is. Positions are floats in metres
// or 16.16 fixed point; normals, directions, polygons and textures stay as
// they are.
//  - trNN.frd (0x419C20 reads it from the buffer 0x4E0EC0 loaded at 0x419C3A;
//    its size: [0x5643F8]): per block its centre and bounding points, its
//    vertices, its object references, sound and light sources; then the
//    objects (4 lists per block and one for the whole track): their place and
//    vertices, and the 16.16 path of the animated ones (type 3, whose place
//    field isn't one).
//  - trNN.col (copied without its 16-byte header to [0x552E18] by 0x4313E0, at
//    0x41DD24; section offsets first, [0x5EFF14] of them; its size
//    [0x5EFF10]): road nodes (section 15: 16.16 place, then the two walls'
//    distances), the objects' shapes (8: float vertices) and objects (7, and
//    the alike 9 and 18: a 16.16 place, or an animation path).
//  - simhts.dat (0x41E322): a float per road node, scaled as a whole.
//  - trNN.ccm (0x41E1C5): the trackside cameras, 28 bytes each after an
//    8-byte header, a 16.16 place first.
//  - speedsF.bin / speedsR.bin (0x40E035, the race's direction): the AI's
//    target speed in m/s, a byte per two road nodes ((nodes + 1) / 2 of them,
//    read by 0x40E080), then a byte per node of other things (0x40E0C0); one
//    byte more at the end with an even number of nodes. The speeds come from
//    the corners and the braking before them, and both go with the square
//    root of the distances (a corner's radius, a braking zone's length), so
//    each is multiplied by the square root of the scale. Not 88, the most
//    the game ever sets: its AI's ceiling, there even in curves (on Hometown
//    a third of the nodes). On the smaller tracks the AI slowed for the
//    corners as on the big ones.
//  - The .can files (camera paths at the start and finish) High Stakes left as
//    they were: they follow the car.
// Each track has a size of its own (g_tsScale), looked up by the number in all
// its files' paths ([0x6FD4AC], "%sTRK%03x\..."): trackScale, or the ini's
// line for that track. trackScale is 0.85 by default, between the game's size
// and High Stakes'. The narrowest roads, a quarter of Rocky Pass (3) and a
// fifth of Summit (7), are 8 m of asphalt at the game's size, two 4 m lanes,
// where the other tracks' two lanes are 10 to 16 m (the lane lines are the
// .frd's polygon list 6: 5 m lanes on Atlantica and Aquatica, 5.5 m on the
// rest of Rocky Pass and Summit, 8 m on the other five). The widest cars are
// about 2.07 m (the CLK-GTR's model), so three side by side need 6.2 m: 8 m
// makes 6.15 at 0.7692, 6.8 at 0.85.
// ---------------------------------------------------------------------------
namespace ts {
struct Walk {
    BYTE* p; size_t n; size_t o; bool ok; bool apply; float s;
    bool Need(size_t k) {
        if (!ok || o > n || k > n - o) { ok = false; return false; }
        return true;
    }
    int I32() { int v = 0; if (Need(4)) { memcpy(&v, p + o, 4); o += 4; } return v; }
    int Count(size_t each) {                      // a record count: sane, and its records within the buffer
        int c = I32();
        if (c < 0 || (size_t)c > (n - (o < n ? o : n)) / (each ? each : 1)) ok = false;
        return ok ? c : 0;
    }
    void Skip(size_t k) { if (Need(k)) o += k; }
    void Floats(size_t k) {                       // k floats in metres
        if (!Need(4 * k)) return;
        if (apply) for (size_t i = 0; i < k; i++) {
            float v; memcpy(&v, p + o + 4 * i, 4); v *= s; memcpy(p + o + 4 * i, &v, 4);
        }
        o += 4 * k;
    }
    void Fixed(size_t k) {                        // k 16.16 fixed-point values in metres
        if (!Need(4 * k)) return;
        if (apply) for (size_t i = 0; i < k; i++) {
            int v; memcpy(&v, p + o + 4 * i, 4); v = (int)lround(v * (double)s); memcpy(p + o + 4 * i, &v, 4);
        }
        o += 4 * k;
    }
};
static void FrdObjects(Walk& w) {
    int c = w.Count(36);
    for (int i = 0; i < c && w.ok; i++) {
        int type = w.I32(); w.Skip(8);
        if (type == 3) w.Skip(12); else w.Floats(3);
        int ab = w.Count(1);                      // animation: 8-byte header, then 20 bytes a step
        if (ab >= 8) {
            size_t end = w.o + ab;
            w.Skip(8);
            for (int k = 0; k < (ab - 8) / 20 && w.ok; k++) { w.Fixed(3); w.Skip(8); }
            if (w.ok) w.o = end;
        } else w.Skip(ab);
        int nv = w.Count(16); w.Floats(3 * (size_t)nv); w.Skip(4 * (size_t)nv);
        int np = w.Count(14); w.Skip(14 * (size_t)np);
    }
}
static void Frd(Walk& w) {
    w.Skip(28);
    int nb = w.Count(1300);                       // the last block's number (blocks 0..nb)
    for (int b = 0; b <= nb && w.ok; b++) {
        w.Floats(15);                             // centre, 4 bounding points
        int nv = w.Count(16); w.Skip(20);
        w.Floats(3 * (size_t)nv); w.Skip(4 * (size_t)nv);
        w.Skip(0x4B0);                            // neighbours
        int c[8];
        for (int k = 0; k < 8; k++) c[k] = w.Count(1);
        if (!w.ok) break;
        w.Skip(8 * (size_t)c[1]); w.Skip(8 * (size_t)c[2]); w.Skip(12 * (size_t)c[3]);   // positions, polygon flags, road normals
        for (int k = 0; k < c[4] && w.ok; k++) { w.Fixed(3); w.Skip(8); }               // object references
        w.Skip(20 * (size_t)c[5]);
        for (int k = 0; k < c[6] && w.ok; k++) { w.Fixed(3); w.Skip(4); }               // sound sources
        for (int k = 0; k < c[7] && w.ok; k++) { w.Fixed(3); w.Skip(4); }               // light sources
    }
    for (int b = 0; b <= nb && w.ok; b++)         // polygons: 11 lists per block
        for (int l = 0; l < 11 && w.ok; l++) {
            if (w.I32() == 0) continue;
            if (l <= 6) { int m = w.Count(14); w.Skip(14 * (size_t)m); continue; }
            int c = w.Count(4);
            for (int k = 0; k < c && w.ok; k++)
                if (w.I32() == 1) { int m = w.Count(14); w.Skip(14 * (size_t)m); }
        }
    for (int b = 0; b <= nb && w.ok; b++)
        for (int l = 0; l < 4 && w.ok; l++) FrdObjects(w);
    FrdObjects(w);                                // the whole track's
    int nt = w.Count(47); w.Skip(47 * (size_t)nt);  // textures
}
static void ColObjects(Walk& w, size_t end, int nrec) {
    for (int i = 0; i < nrec && w.ok; i++) {
        size_t at = w.o;
        if (!w.Need(8)) break;
        WORD size; memcpy(&size, w.p + at, 2);
        BYTE type = w.p[at + 2];
        if (size < 4 || size > end - at) { w.ok = false; break; }
        if (type == 1) {                          // a 16.16 place
            if (size < 16) { w.ok = false; break; }
            w.o = at + 4; w.Fixed(3);
        }
        else if (type == 3) {
            WORD len; memcpy(&len, w.p + at + 4, 2);
            if (8 + 20 * (size_t)len > size) { w.ok = false; break; }
            w.o = at + 8;
            for (int k = 0; k < len && w.ok; k++) { w.Fixed(3); w.Skip(8); }
        }
        w.o = at + size;
    }
}
static void Col(Walk& w, int nsec) {
    if (nsec <= 0 || nsec > 64 || !w.Need(4 * (size_t)nsec)) { w.ok = false; return; }
    for (int i = 0; i < nsec && w.ok; i++) {
        int off; memcpy(&off, w.p + 4 * i, 4);
        if (off < 4 * nsec || (size_t)off > w.n || w.n - off < 8) { w.ok = false; break; }
        int size; WORD xbid, nrec;
        memcpy(&size, w.p + off, 4); memcpy(&xbid, w.p + off + 4, 2); memcpy(&nrec, w.p + off + 6, 2);
        if (size < 8 || (size_t)size > w.n - off) { w.ok = false; break; }
        size_t end = off + (size_t)size;
        w.o = off + 8;
        if (xbid == 15) {                         // road nodes, 36 bytes
            if (36 * (size_t)nrec > (size_t)size - 8) { w.ok = false; break; }
            for (int k = 0; k < nrec; k++) { w.Fixed(3); w.Skip(16); w.Fixed(2); }
        } else if (xbid == 8) {                   // objects' shapes
            for (int k = 0; k < nrec && w.ok; k++) {
                size_t at = w.o;
                if (!w.Need(8)) break;
                int rs; WORD nv; memcpy(&rs, w.p + at, 4); memcpy(&nv, w.p + at + 4, 2);
                if (rs < 8 || (size_t)rs > end - at || 8 + 16 * (size_t)nv > (size_t)rs) { w.ok = false; break; }
                w.o = at + 8;
                for (int v = 0; v < nv; v++) { w.Floats(3); w.Skip(4); }
                w.o = at + rs;
            }
        } else if (xbid == 7 || xbid == 9 || xbid == 18) ColObjects(w, end, nrec);
    }
}
// the AI's target speeds (the first part: a byte per two of the n road nodes)
static bool Speeds(BYTE* p, size_t size, int nodes, float s) {
    const int kCeiling = 88;
    if (!p || nodes <= 0) return false;
    size_t half = ((size_t)nodes + 1) / 2;
    if (size != half + (size_t)nodes && size != half + (size_t)nodes + 1) return false;
    double k = sqrt((double)s);
    for (size_t i = 0; i < half; i++)
        if (p[i] < kCeiling) {
            long v = lround(p[i] * k);
            p[i] = (BYTE)(v < 1 ? 1 : v > kCeiling ? kCeiling : v);
        }
    return true;
}
static void Heights(Walk& w) { if (w.n % 4) w.ok = false; else w.Floats(w.n / 4); }
static void Cameras(Walk& w) {
    w.Skip(4);
    int c = w.Count(28);
    for (int k = 0; k < c && w.ok; k++) { w.Fixed(3); w.Skip(16); }
}
// checks the layout first, then scales; true if it was scaled
static bool Scale(void (*fn)(Walk&), BYTE* p, size_t n, float s, int nsec = 0) {
    if (!p || !n) return false;
    Walk w = { p, n, 0, true, false, s };
    auto run = [&](Walk& x) { if (nsec) Col(x, nsec); else fn(x); };
    run(w);
    if (!w.ok || (!nsec && w.o != n)) return false;
    Walk a = { p, n, 0, true, true, s };
    run(a);
    return a.ok;
}
}  // namespace ts
// a size from the ini: 0.5 to 2; 1 for 0, negative, NaN, or within 1e-4 of 1
static float TsClamp(float s) {
    if (!(s > 0.0f)) return 1.0f;
    s = s < 0.5f ? 0.5f : s > 2.0f ? 2.0f : s;
    return fabsf(s - 1.0f) < 1e-4f ? 1.0f : s;
}
// the sizes by track number, [9] for any other number
static void TsScales(float trackScale, const float own[9], float out[10]) {
    float all = TsClamp(trackScale);
    for (int t = 0; t < 9; t++) out[t] = own[t] > 0.0f ? TsClamp(own[t]) : all;
    out[9] = all;
}
float g_tsScale[10] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
int TrackNum() { int t = Rd<int>(0x6FD4AC); return t >= 0 && t < 9 ? t : 9; }
static float TsScaleNow() { return g_tsScale[TrackNum()]; }
// the track's name for the log (any other number: "track N")
void TrackName(char* name, size_t size) {
    int t = TrackNum();
    if (t < 9) snprintf(name, size, "%s", kTrackName[t]);
    else snprintf(name, size, "track %d", Rd<int>(0x6FD4AC));
}
static void TsLog(const char* what, bool ok) {
    LogfAsync("track scale: %s %s\n", what, ok ? "scaled" : "left as it is (not the layout expected)");
}
bool g_tsOn = false;                        // the track scale's hooks are in
extern "C" void __cdecl TsFrd(BYTE* p) {
    float s = TsScaleNow();
    if (g_tsOn) {
        char name[24]; TrackName(name, sizeof(name));
        if (s == 1.0f) LogfAsync("track scale: %s at the game's size\n", name);
        else LogfAsync("track scale: %s x%.4f\n", name, s);
        if (p && s != 1.0f) TsLog(".frd", ts::Scale(ts::Frd, p, Rd<int>(0x5643F8), s));
    }
    NlFrd(p, p ? (size_t)Rd<int>(0x5643F8) : 0, s);     // (the night lamps: the lights in the .frd)
}
extern "C" void __cdecl TsHeights(BYTE* p) {
    float s = TsScaleNow();
    if (p && s != 1.0f) TsLog("simhts.dat", ts::Scale(ts::Heights, p, Rd<int>(0x5643F8), s));
}
extern "C" void __cdecl TsCameras(BYTE* p) {
    float s = TsScaleNow();
    if (p && s != 1.0f) TsLog(".ccm", ts::Scale(ts::Cameras, p, Rd<int>(0x5643F8), s));
}
extern "C" void __cdecl TsSpeeds(BYTE* p) {
    float s = TsScaleNow();
    if (p && s != 1.0f) TsLog("AI speeds", ts::Speeds(p, Rd<int>(0x5643F8), Rd<int>(0x5DD940), s));
}
extern "C" void __cdecl TsCol() {
    BYTE* p = Rd<BYTE*>(0x552E18); int size = Rd<int>(0x5EFF10); float s = TsScaleNow();
    if (p && size > 16 && s != 1.0f) TsLog(".col", ts::Scale(nullptr, p, size - 16, s, Rd<int>(0x5EFF14)));
}
extern "C" { DWORD g_fnLoadFile = 0x4E0EC0, g_fnLoadFileNc = 0x4E0FE0, g_fnColCopy = 0x4313E0; } extern "C" void TsHeightsStub(); extern "C" void TsCamerasStub(); extern "C" void TsColStub();
extern "C" void TsSpeedsStub();
__asm__(
    // a file loaded (eax = its buffer, 0 if it failed), then scaled
    ".globl _TsFrdStub\n"
    "_TsFrdStub:\n"
    "    call *_g_fnLoadFile\n"
    "    pushal\n"
    "    pushl %eax\n"
    "    call _TsFrd\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    ret\n"
    ".globl _TsHeightsStub\n"
    "_TsHeightsStub:\n"
    "    call *_g_fnLoadFile\n"
    "    pushal\n"
    "    pushl %eax\n"
    "    call _TsHeights\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    ret\n"
    ".globl _TsSpeedsStub\n"
    "_TsSpeedsStub:\n"
    "    call *_g_fnLoadFile\n"
    "    pushal\n"
    "    pushl %eax\n"
    "    call _TsSpeeds\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    ret\n"
    ".globl _TsCamerasStub\n"
    "_TsCamerasStub:\n"
    "    call *_g_fnLoadFileNc\n"
    "    pushal\n"
    "    pushl %eax\n"
    "    call _TsCameras\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    ret\n"
    // the .col copied into [0x552E18] (eax = 1)
    ".globl _TsColStub\n"
    "_TsColStub:\n"
    "    call *_g_fnColCopy\n"
    "    pushal\n"
    "    call _TsCol\n"
    "    popal\n"
    "    ret\n"
);
void InstallTrackScale() {
    if (!g_knownExe) return;
    float* sc = g_tsScale;
    TsScales(g_cfg.trackScale, g_cfg.trackScaleOwn, sc);
    bool any = false;
    for (int t = 0; t < 10; t++) any |= sc[t] != 1.0f;
    if (!any) { Logf("track scale: the game's\n"); return; }
    char own[320] = "";                           // the tracks whose size isn't trackScale's
    for (int t = 0, n = 0; t < 9; t++)
        if (sc[t] != sc[9] && n < (int)sizeof(own))
            n += snprintf(own + n, sizeof(own) - n, "%s%s x%.4f", n ? ", " : " (", kTrackName[t], sc[t]);
    if (own[0]) strncat(own, ")", sizeof(own) - strlen(own) - 1);
    g_tsOn = true;
    bool ok = PatchRel32(0x419C3A, 0xE8, (void*)&TsFrdStub) && PatchRel32(0x41DD24, 0xE8, (void*)&TsColStub) &&
              PatchRel32(0x41E322, 0xE8, (void*)&TsHeightsStub) && PatchRel32(0x41E1C5, 0xE8, (void*)&TsCamerasStub) &&
              PatchRel32(0x40E035, 0xE8, (void*)&TsSpeedsStub);
    if (sc[9] == 1.0f) Logf("track scale: the game's size%s: %s\n", own, ok ? "on" : "patch FAILED");
    else Logf("track scale: x%.4f%s: %s\n", sc[9], own, ok ? "on" : "patch FAILED");
}
