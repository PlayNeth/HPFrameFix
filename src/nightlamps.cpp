#include "common.h"
#include "nightlamps.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// The track loaded: its number, in all its files' paths ([0x6FD4AC], "%sTRK%03x\..."), 9 for any but the game's
// nine; its name for the log.
static const char* const kTrackName[9] = { "Hometown", "Redrock Ridge", "Atlantica", "Rocky Pass", "Country Woods",
                                           "Lost Canyons", "Aquatica", "Summit", "Empire City" };
static int TrackNum() { int t = Rd<int>(0x6FD4AC); return t >= 0 && t < 9 ? t : 9; }
static void TrackName(char* name, size_t size) {
    int t = TrackNum();
    if (t < 9) snprintf(name, size, "%s", kTrackName[t]);
    else snprintf(name, size, "track %d", Rd<int>(0x6FD4AC));
}
// The .frd as it's loaded (0x419C20 reads it from the buffer 0x4E0EC0 loaded at 0x419C3A; its size: [0x5643F8]),
// for the night lamps' NlFrd.
static void NlFrd(const BYTE* p, size_t n, float s);
extern "C" void __cdecl NlFrdLoaded(BYTE* p) { NlFrd(p, p ? (size_t)Rd<int>(0x5643F8) : 0, 1.0f); }
extern "C" { DWORD g_fnLoadFileNl = 0x4E0EC0; }
extern "C" void NlFrdStub();
__asm__(
    ".globl _NlFrdStub\n"
    "_NlFrdStub:\n"
    "    call *_g_fnLoadFileNl\n"          // eax = the file's buffer, 0 if it failed
    "    pushal\n"
    "    pushl %eax\n"
    "    call _NlFrdLoaded\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    ret\n"
);
static float NlScale() { return 1.0f; }             // (the tracks at the game's size)
#define NL_FRD_HOOK PatchRel32(0x419C3A, 0xE8, (void*)&NlFrdStub)

// ---------------------------------------------------------------------------
// Night lamps. At night the game darkens the whole track as it loads. Each
// track's night .hrz has "ambient light percentages" (Hometown's 5, 10, 15),
// and 0x419750 (called at 0x41A108, right after the .frd is read in)
// multiplies every vertex colour by them, red, green and blue apart
// (0x419660):
//  - the track blocks' own: block 0x571370 + n * 0x5C0, n up to [0x5DD83C];
//    +0x58 the number of vertices, +0x70 them (floats, metres), +0x74 their
//    colours (0xAARRGGBB);
//  - the objects': the lists at block +0x564, +0x56C and +0x574 and the
//    track's at 0x5DD070 (a count, then a pointer to entries of 0x34 bytes:
//    +0 the type, +0xC its place, +0x20 the number of vertices, +0x24 them,
//    around the place, +0x28 their colours). The game leaves the list at
//    +0x57C as it is.
// The lamps are the blocks' light sources (+0x5B8 how many, +0x5BC them, 16
// bytes each: a 16.16 place, then the type in the low 4 bits), which the game
// only draws as a glow (0x491190), its colour, blinking and size from the
// type's 12 bytes at 0x55E3DC. At night ([0x7A3A60]), after the darkening,
// each steady white, yellow or cyan lamp (types 0-4, 8, 13-15) adds its
// glow's colour to the vertices around it, times nightLamps * (1 - (d/r)^2)^2
// at a distance d up to its reach r: nightLampReach for a street lamp's 4.5 m
// glow, in proportion for other sizes. The blinking and red ones (antennas,
// warning lights) light nothing.
// The game's own tracks (kNlTracks, known by their .frd's size) get more
// lights, found in the .frd as it's loaded (NlFrd): see NlTrack. Objects of
// type 3 are animated: they move, so the lamps leave them as they are, but
// those listed for the track get an even light of their own.
// ---------------------------------------------------------------------------
namespace nl {
struct Lamp { float x, y, z, r, g, b, reach; };
// adds the lamps' light to the colours of n vertices (x, y, z each, around the
// place o); how many it lit
static int Light(const float* v, DWORD* c, int n, const float o[3], const Lamp* L, int nl, float k) {
    int lit = 0;
    for (int i = 0; i < n; i++, v += 3) {
        float x = v[0] + o[0], y = v[1] + o[1], z = v[2] + o[2], add[3] = {};
        for (int j = 0; j < nl; j++) {
            const Lamp& l = L[j];
            float dx = x - l.x, dy = y - l.y, dz = z - l.z;
            float q = (dx * dx + dy * dy + dz * dz) / (l.reach * l.reach);
            if (q >= 1.0f) continue;
            float f = (1.0f - q) * (1.0f - q) * k;
            add[0] += l.r * f; add[1] += l.g * f; add[2] += l.b * f;
        }
        if (add[0] <= 0.0f && add[1] <= 0.0f && add[2] <= 0.0f) continue;
        DWORD old = c[i], out = old & 0xFF000000;
        for (int ch = 0; ch < 3; ch++) {
            int s = 16 - 8 * ch;
            float t = ((old >> s) & 0xFF) + add[ch] * 255.0f + 0.5f;
            out |= (DWORD)(t > 255.0f ? 255.0f : t) << s;
        }
        c[i] = out; lit++;
    }
    return lit;
}
// the lamps that reach any of n vertices around the place o, into out; how many
static int Near(const float* v, int n, const float o[3], const Lamp* L, int nl, Lamp* out) {
    if (n <= 0) return 0;
    float lo[3] = { v[0], v[1], v[2] }, hi[3] = { v[0], v[1], v[2] };
    for (int i = 1; i < n; i++)
        for (int a = 0; a < 3; a++) {
            float t = v[3 * i + a];
            if (t < lo[a]) lo[a] = t;
            if (t > hi[a]) hi[a] = t;
        }
    int m = 0;
    for (int j = 0; j < nl; j++) {
        const Lamp& l = L[j];
        const float p[3] = { l.x, l.y, l.z };
        bool in = true;
        for (int a = 0; a < 3 && in; a++) in = p[a] > lo[a] + o[a] - l.reach && p[a] < hi[a] + o[a] + l.reach;
        if (in) out[m++] = l;
    }
    return m;
}
// n colours (0xAARRGGBB): each channel times mul, plus add (blue, green, red), up to 255
static void Brighten(DWORD* c, int n, float mul, const float add[3]) {
    for (int i = 0; i < n; i++) {
        DWORD out = c[i] & 0xFF000000;
        for (int ch = 0; ch < 3; ch++) {
            float t = ((c[i] >> 8 * ch) & 0xFF) * mul + add[ch] + 0.5f;
            out |= (DWORD)(t > 255.0f ? 255.0f : t) << 8 * ch;
        }
        c[i] = out;
    }
}
}  // namespace nl
static float g_nlK = 1.0f, g_nlReach = 22.0f, g_nlFoundK = 0.0f;   // nightLamps, nightLampReach, nightScenery
static const DWORD kNlTypes = 0xE11F;               // steady white, yellow, cyan: types 0-4, 8, 13-15
static const int kNlMaxLamps = 8192, kNlMaxVerts = 1 << 20;
static nl::Lamp g_nlLamps[kNlMaxLamps], g_nlNear[kNlMaxLamps];
static int g_nlLampN = 0;

// The lights found in a track's .frd. Buildings, signs and the road are part
// of the track's blocks: each block's full-detail polygon list (the 5th of
// its 11) and its objects' (the last 4, each a number of objects with their
// own polygons, on the block's vertices). A polygon's texture (its +8) is an
// entry of the table at the file's end, 47 bytes each, with the picture's
// number in the track's .qfs at +45; the pictures below are those numbers, so
// each list is for one file of the game's, known by its size. What a polygon
// with one of them gives:
//  - a window (only on a wall, a polygon standing upright): a light 2.5 m in
//    front of its middle, on the side of the block's centre (the road's) for
//    the block's own polygons, away from the object's middle for an object's;
//    with "corners", one at each corner too when they're more than 6 m from
//    the middle (the big panels of the cities' towers, which a light at the
//    middle doesn't reach);
//  - a fitting (a lit panel or sign, a lamp with no light source), a yellow
//    one: a light at its middle, whichever way it faces;
//  - a blue beam or panel: a blue light at its foot, 2 m up;
//  - the road: street lighting 6 m over it, where the track has few lamps of
//    its own (with no colour for it, rcol, only in the tunnels below);
//  - a tunnel (blocks first to last): the lighting over the road there is a
//    fitting's (1) or a yellow one's (4), 5 m up;
//  - a power pole's lamp head: a light for the head itself, and a street
//    lamp's over the road: 6 m over the block's centre's height, at most 8 m
//    from that centre (the heads themselves are 15 to 40 m from the road);
//  - a lamp post's head (it has a light source of the game's): the head lit.
// Lights of a kind within its joining distance of one another become one.

struct NlTrack {
    int track; size_t frd;                         // the game's number, its .frd's size
    short win[40], fit[20], yellow[6], blue[8], road[6], pole[2], head[2];    // pictures (0: the end)
    short lit[4];                                  // animated objects lit all over (a ship's own lights), by
                                                   // their number of vertices
    short tunnel[6][3];                            // first block, last, kind
    float wcol[3], fcol[3], rcol[3];               // the windows', fittings' and road lighting's colours
    float wreach;                                  // a window's reach, m
    float glow;                                    // the night's darkened colours x this (a lit city's sky)
    float litk;                                    // the animated objects' light x this
    bool corners;
};
static const NlTrack kNlTracks[] = {
    { 0, 1422739,                                                                       // Hometown
      { 26, 30, 41, 50, 51, 52, 53, 54, 55, 56, 59, 60, 66, 68, 69, 89, 90, 91, 93, 97, 99 },
      { 148, 150 }, {}, {}, {}, { 163 }, { 31 }, {}, {},
      { 0.8f, 0.58f, 0.3f }, { 0.9f, 0.85f, 0.7f }, {}, 9, 1, 1, false },
    { 1, 2072468,                                                                       // Redrock Ridge
      { 15, 65, 84, 91, 93, 119, 123, 124, 126, 128, 129, 137, 138, 157, 171 },
      { 103, 114, 117, 135, 160, 162, 168, 173, 178, 179 }, {}, {}, {}, {}, {}, { 8 }, {},
      { 0.8f, 0.58f, 0.3f }, { 0.9f, 0.85f, 0.7f }, {}, 9, 1, 1, false },
    { 2, 1836142,                                                                       // Atlantica (its boat)
      { 8, 9, 10, 11, 12, 15, 16, 17, 18, 20, 24, 25, 29, 30, 31, 32, 35, 37, 51, 52, 57, 89, 92 },
      { 14, 21, 22, 33, 43, 59, 62, 83, 90, 143 }, {}, { 162 }, { 129, 132, 134, 135 }, {}, {}, { 106 }, {},
      { 0.3f, 0.38f, 0.45f }, { 0.9f, 0.9f, 0.82f }, { 0.42f, 0.44f, 0.42f }, 14, 2.5f, 1, true },
    { 3, 2136385,                                                                       // Rocky Pass
      { 78, 80, 81, 83, 84, 122, 124, 125, 126, 130, 131, 132, 133, 135, 136, 138, 139, 140, 141, 142, 144, 145,
        146, 157, 159 },
      { 127, 147, 148 }, {}, {}, {}, {}, {}, {}, {},
      { 0.8f, 0.58f, 0.3f }, { 0.9f, 0.85f, 0.7f }, {}, 9, 1, 1, false },
    { 4, 1778817,                                                                       // Country Woods (its doughnut)
      { 24, 25, 29, 47, 49, 50, 54, 55, 71, 78, 79, 90, 91, 95, 97, 101, 107, 109, 110, 111, 128, 131 },
      { 56, 57, 58, 61, 66, 67, 68, 73, 74, 75, 103, 104, 105, 114, 132, 135 }, {}, {}, {}, { 144 }, {}, { 50 }, {},
      { 0.8f, 0.58f, 0.3f }, { 0.9f, 0.85f, 0.7f }, {}, 9, 1, 1.7f, false },
    { 5, 2503824,                                                                       // Lost Canyons
      { 12, 85, 92, 94, 95, 146, 147, 150, 151, 153, 155, 156, 164, 165, 182 },
      { 71, 115, 144, 162, 185, 187, 193, 198, 202 }, { 62 }, {}, {}, {}, {}, { 8 }, {},
      { 0.8f, 0.58f, 0.3f }, { 0.9f, 0.85f, 0.7f }, {}, 9, 1, 1, false },
    { 6, 1971375,                                                                       // Aquatica (its ships)
      { 8, 9, 10, 11, 12, 13, 14, 18, 19, 20, 21, 29, 84, 87, 88, 139, 140 },
      { 60, 62, 80, 85, 86, 123, 138 }, { 25, 92, 94, 95, 130 }, { 133, 134, 141, 142, 164 },
      { 109, 112, 114, 115 }, {}, {}, { 86, 121 },
      { { 6, 16, 4 }, { 22, 27, 4 }, { 39, 44, 4 }, { 123, 125, 4 }, { 151, 153, 4 } },
      { 0.3f, 0.38f, 0.45f }, { 0.9f, 0.9f, 0.82f }, { 0.42f, 0.44f, 0.42f }, 14, 2.5f, 1, true },
    { 7, 2611012,                                                                       // Summit (its cable cars)
      { 55, 56, 63, 92, 94, 95, 97, 98, 107, 108, 115, 116, 133, 135, 136, 137, 141, 142, 143, 146, 147, 148, 149,
        150, 151, 152, 154, 155, 156, 169, 170 },
      { 138, 139, 157, 158 }, {}, {}, {}, {}, {}, { 72 }, {},
      { 0.8f, 0.58f, 0.3f }, { 0.9f, 0.85f, 0.7f }, {}, 9, 1, 1, false },
    { 8, 1470662,                                                                       // Empire City
      { 10, 12, 13, 16, 20, 24, 25, 26, 27, 28, 29, 31, 34, 37, 60, 61, 64, 65, 68, 71, 92, 93, 94, 95, 100, 101,
        104, 105, 197, 198 },
      { 151, 163, 167, 170, 199, 203, 204, 205, 206, 207, 208, 210, 213, 217, 218, 220, 245 }, { 84, 88 },
      { 211, 212 }, {}, {}, {}, {}, {},
      { 0.6f, 0.48f, 0.28f }, { 0.9f, 0.85f, 0.7f }, {}, 12, 1, 1, true },
};
static const NlTrack* g_nlTrack = nullptr;        // the loaded track's, if it's one of the game's own files
static const int kNlMaxFound = 6144;
static struct { float at[3]; int joined, kind; } g_nlFound[kNlMaxFound];
static int g_nlFoundN = 0;
struct NlFile {                                   // reads the .frd, never past its end
    const BYTE* p; size_t n, o; bool ok;
    int I() { int v = 0; if (n - o < 4) { ok = false; o = n; } else { memcpy(&v, p + o, 4); o += 4; } return v; }
    void Skip(int count, size_t each) {           // (a negative count: not this layout)
        if (count < 0 || (size_t)count > (n - o) / each) { ok = false; o = n; } else o += count * each;
    }
};
static bool NlHas(const short* list, size_t n, short pic) {
    for (size_t i = 0; i < n; i++) if (list[i] && list[i] == pic) return true;
    return false;
}
#define NL_HAS(list, pic) NlHas(list, sizeof(list) / sizeof(list[0]), pic)
// a light found: joined to one of its kind within that kind's distance (their places averaged), or a new one
static void NlFound(const float at[3], int kind, float join) {
    for (int j = 0; j < g_nlFoundN; j++) {
        auto& w = g_nlFound[j];
        float dx = w.at[0] - at[0], dy = w.at[1] - at[1], dz = w.at[2] - at[2];
        if (w.kind == kind && dx * dx + dy * dy + dz * dz < join * join) {
            for (int a = 0; a < 3; a++) w.at[a] = (w.at[a] * w.joined + at[a]) / (w.joined + 1);
            w.joined++;
            return;
        }
    }
    if (g_nlFoundN == kNlMaxFound) return;
    auto& w = g_nlFound[g_nlFoundN++];
    memcpy(w.at, at, 12); w.joined = 1; w.kind = kind;
}
// a polygon's 4 vertices (of nv at verts) and their middle; false if it has one outside them
static bool NlQuad(const BYTE* poly, const BYTE* verts, int nv, float v[4][3], float c[3]) {
    short q[4]; memcpy(q, poly, 8);
    for (int k = 0; k < 4; k++) {
        if (q[k] < 0 || q[k] >= nv) return false;
        memcpy(v[k], verts + 12 * (size_t)q[k], 12);
    }
    for (int a = 0; a < 3; a++) c[a] = (v[0][a] + v[1][a] + v[2][a] + v[3][a]) / 4;
    return true;
}
// The lights of m polygons of block b (its centre bc), on nv vertices; tex: the texture table's nt entries. A
// window's light goes on the side of ref, or away from it; with ref null, away from the polygons' own middle
// (an object's). s: the track's scale.
static void NlPolygons(const BYTE* polys, int m, const BYTE* verts, int nv, const BYTE* tex, int nt, const float* ref,
                       bool toward, float s, int b, const float* bc) {
    const NlTrack& T = *g_nlTrack;
    float v[4][3], c[3], mid[3] = {};
    if (!ref) {
        int k = 0;
        for (int i = 0; i < m; i++)
            if (NlQuad(polys + 14 * (size_t)i, verts, nv, v, c)) { for (int a = 0; a < 3; a++) mid[a] += c[a]; k++; }
        if (!k) return;
        for (float& x : mid) x /= k;
        ref = mid;
    }
    for (int i = 0; i < m; i++) {
        const BYTE* poly = polys + 14 * (size_t)i;
        short t; memcpy(&t, poly + 8, 2);
        if (t < 0 || t >= nt) continue;
        short pic; memcpy(&pic, tex + 47 * (size_t)t + 45, 2);
        int kind = -1, tunnel = 0;
        if (NL_HAS(T.blue, pic)) kind = kNlBlue;
        else if (NL_HAS(T.road, pic)) {
            for (const short* r : T.tunnel) if (r[1] && b >= r[0] && b <= r[1]) tunnel = r[2];
            if (tunnel || T.rcol[0] > 0.0f) kind = kNlRoad;
        }
        if (kind < 0)
            kind = NL_HAS(T.yellow, pic) ? kNlYellow : NL_HAS(T.pole, pic) ? kNlPole : NL_HAS(T.head, pic) ? kNlHead
                 : NL_HAS(T.fit, pic) ? kNlFitting : NL_HAS(T.win, pic) ? kNlWindow : -1;
        if (kind < 0 || !NlQuad(poly, verts, nv, v, c)) continue;
        float join = kNlKind[tunnel ? kNlFitting : kind].join * s;
        if (kind == kNlBlue) {                                        // its foot, 2 m up
            for (int k = 0; k < 4; k++) if (v[k][1] < c[1]) c[1] = v[k][1];
            c[1] += 2.0f * s;
        } else if (kind == kNlPole) {
            NlFound(c, kNlHead, join);                                // (the head itself)
            float dx = bc[0] - c[0], dz = bc[2] - c[2], d = sqrtf(dx * dx + dz * dz);
            if (d > 8.0f * s) { float k = (d - 8.0f * s) / d; c[0] += dx * k; c[2] += dz * k; }
            c[1] = bc[1] + 6.0f * s;
        } else if (kind == kNlRoad) {
            c[1] += (tunnel ? 5.0f : 6.0f) * s;
            if (tunnel) kind = tunnel;
        } else if (kind == kNlWindow) {
            float e1[3], e2[3];
            for (int a = 0; a < 3; a++) { e1[a] = v[1][a] - v[0][a]; e2[a] = v[3][a] - v[0][a]; }
            float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2];
            float nz = e1[0] * e2[1] - e1[1] * e2[0], h = sqrtf(nx * nx + nz * nz);
            if (h < 1e-6f || fabsf(ny) > h) continue;                 // not a wall
            nx /= h; nz /= h;
            if (((ref[0] - c[0]) * nx + (ref[2] - c[2]) * nz < 0) == toward) { nx = -nx; nz = -nz; }
            float far2 = 0;
            for (int k = 0; k < 4; k++) {
                float d2 = 0;
                for (int a = 0; a < 3; a++) d2 += (v[k][a] - c[a]) * (v[k][a] - c[a]);
                if (d2 > far2) far2 = d2;
            }
            c[0] += nx * 2.5f * s; c[2] += nz * 2.5f * s;
            NlFound(c, kind, join);
            if (T.corners && far2 > 36.0f * s * s)
                for (int k = 0; k < 4; k++) {
                    const float at[3] = { v[k][0] + nx * 2.5f * s, v[k][1], v[k][2] + nz * 2.5f * s };
                    NlFound(at, kind, join);
                }
            continue;
        }
        NlFound(c, kind, join);
    }
}
// the .frd loaded (p, n bytes; after any scaling, s): the track's lights found
static void NlFrd(const BYTE* p, size_t n, float s) {
    const int kMaxBlocks = 512;
    static struct { size_t verts; int nv; float c[3]; } blk[kMaxBlocks];
    g_nlFoundN = 0; g_nlTrack = nullptr;
    for (const NlTrack& t : kNlTracks) if (t.track == TrackNum() && t.frd == n) g_nlTrack = &t;
    if (!p || !g_nlTrack || !(g_nlFoundK > 0.0f)) return;
    NlFile f = { p, n, 28, true };
    int nb = f.I() + 1;
    if (nb <= 0 || nb > kMaxBlocks) return;
    for (int b = 0; b < nb && f.ok; b++) {        // the blocks: centre, vertices
        if (n - f.o >= 12) memcpy(blk[b].c, p + f.o, 12);
        f.Skip(60, 1);
        blk[b].nv = f.I(); f.Skip(20, 1); blk[b].verts = f.o; f.Skip(blk[b].nv, 16); f.Skip(0x4B0, 1);
        int c[8]; for (int& x : c) x = f.I();
        f.Skip(c[1], 8); f.Skip(c[2], 8); f.Skip(c[3], 12); f.Skip(c[4], 20); f.Skip(c[5], 20); f.Skip(c[6], 16);
        f.Skip(c[7], 16);
    }
    size_t lists = f.o, tex = 0; int nt = 0;
    for (int pass = 0; pass < 2 && f.ok; pass++) {    // their polygon lists: checked to the file's end, then used
        f.o = lists;
        for (int b = 0; b < nb && f.ok; b++)
            for (int j = 0; j < 11 && f.ok; j++) {
                if (!f.I()) continue;
                const BYTE* verts = p + blk[b].verts;
                if (j <= 6) {
                    int m = f.I(); size_t at = f.o;
                    f.Skip(m, 14);
                    if (pass && f.ok && j == 4)
                        NlPolygons(p + at, m, verts, blk[b].nv, p + tex, nt, blk[b].c, true, s, b, blk[b].c);
                } else
                    for (int q = f.I(); q > 0 && f.ok; q--) {
                        if (f.I() != 1) continue;
                        int m = f.I(); size_t at = f.o;
                        f.Skip(m, 14);
                        if (pass && f.ok)
                            NlPolygons(p + at, m, verts, blk[b].nv, p + tex, nt, nullptr, false, s, b, blk[b].c);
                    }
            }
        if (pass) break;
        for (int l = 0; l < nb * 4 + 1 && f.ok; l++)  // the other objects
            for (int q = f.I(); q > 0 && f.ok; q--) {
                f.Skip(24, 1); f.Skip(f.I(), 1); f.Skip(f.I(), 16); f.Skip(f.I(), 14);
            }
        nt = f.I(); tex = f.o;
        f.Skip(nt, 47);
        if (f.o != n) f.ok = false;
    }
}
// lights one vertex list; adds to *all how many vertices it has, to *lit how many it lit
static void NlList(const float* v, DWORD* c, int n, const float o[3], int* all, int* lit) {
    static const float kNone[3] = {};
    if (n <= 0 || n > kNlMaxVerts || !v || !c) return;
    *all += n;
    if (g_nlTrack && g_nlTrack->glow > 1.0f) nl::Brighten(c, n, g_nlTrack->glow, kNone);
    if (int m = nl::Near(v, n, o, g_nlLamps, g_nlLampN, g_nlNear)) *lit += nl::Light(v, c, n, o, g_nlNear, m, g_nlK);
}
// an object list (a count, then a pointer to entries of 0x34 bytes)
static void NlObjects(const BYTE* list, int* all, int* lit) {
    int n = *(const int*)list; const BYTE* e = *(BYTE* const*)(list + 4);
    if (n <= 0 || n > 100000 || !e) return;
    for (int i = 0; i < n; i++, e += 0x34) {
        int type = *(const int*)e, nv = *(const int*)(e + 0x20);
        DWORD* c = *(DWORD* const*)(e + 0x28);
        if (type == 4) NlList(*(const float* const*)(e + 0x24), c, nv, (const float*)(e + 0xC), all, lit);
        else if (type == 3 && g_nlTrack && c && nv > 0 && nv <= kNlMaxVerts && NL_HAS(g_nlTrack->lit, (short)nv)) {
            float k = g_nlFoundK * g_nlTrack->litk;                     // animated: its own lights, warm white
            const float add[3] = { 125.0f * k, 140.0f * k, 140.0f * k };
            nl::Brighten(c, nv, 1.0f, add);
        }
    }
}
extern "C" void __cdecl NightLamps() {
    if (!Rd<int>(0x7A3A60)) return;                 // a day race
    char name[24]; TrackName(name, sizeof(name));
    int nb = Rd<int>(0x5DD83C) + 1;
    if (nb <= 0 || nb > 1000) return;
    float scale = NlScale();
    g_nlLampN = 0;
    int skipped = 0;
    for (int b = 0; b < nb; b++) {                  // the game's light sources
        const BYTE* B = (const BYTE*)(0x571370 + b * 0x5C0);
        int n = *(const int*)(B + 0x5B8); const int* L = *(const int* const*)(B + 0x5BC);
        if (n <= 0 || n > 1000 || !L) continue;
        for (int i = 0; i < n; i++, L += 4) {
            int type = L[3] & 15;
            if (!(kNlTypes >> type & 1) || g_nlLampN == kNlMaxLamps) { skipped++; continue; }
            const BYTE* e = (const BYTE*)(0x55E3DC + 12 * type);    // its glow
            DWORD col = *(const DWORD*)e; float size = *(const float*)(e + 8);
            nl::Lamp& l = g_nlLamps[g_nlLampN++];
            l.x = L[0] / 65536.0f; l.y = L[1] / 65536.0f; l.z = L[2] / 65536.0f;
            l.r = ((col >> 16) & 0xFF) / 255.0f; l.g = ((col >> 8) & 0xFF) / 255.0f; l.b = (col & 0xFF) / 255.0f;
            l.reach = g_nlReach * scale * (size > 0.5f && size < 20.0f ? size / 4.5f : 1.0f);
        }
    }
    int street = g_nlLampN;
    for (int i = 0; i < g_nlFoundN && g_nlTrack && g_nlLampN < kNlMaxLamps; i++) {    // those found in the .frd
        static const float kBlue[3] = { 0.2f, 0.4f, 1.0f }, kYellow[3] = { 0.95f, 0.72f, 0.25f };
        const NlTrack& T = *g_nlTrack;
        int kind = g_nlFound[i].kind;
        const float* col = kind == kNlWindow ? T.wcol : kind == kNlRoad ? T.rcol : kind == kNlBlue ? kBlue
                         : kind == kNlYellow ? kYellow : T.fcol;
        float k = kNlKind[kind].k * g_nlFoundK / g_nlK;                // (Light multiplies by nightLamps)
        nl::Lamp& l = g_nlLamps[g_nlLampN++];
        l.x = g_nlFound[i].at[0]; l.y = g_nlFound[i].at[1]; l.z = g_nlFound[i].at[2];
        l.r = col[0] * k; l.g = col[1] * k; l.b = col[2] * k;
        l.reach = (kind == kNlWindow ? T.wreach : kNlKind[kind].reach) * scale * g_nlReach / 22.0f;
    }
    int vAll = 0, vLit = 0, oAll = 0, oLit = 0;
    static const float kZero[3] = {};
    static const int kLists[3] = { 0x564, 0x56C, 0x574 };         // (the game leaves +0x57C as it is)
    for (int b = 0; b < nb; b++) {
        const BYTE* B = (const BYTE*)(0x571370 + b * 0x5C0);
        NlList(*(const float* const*)(B + 0x70), *(DWORD* const*)(B + 0x74), *(const int*)(B + 0x58), kZero,
               &vAll, &vLit);
        for (int list : kLists) NlObjects(B + list, &oAll, &oLit);
    }
    NlObjects((const BYTE*)0x5DD070, &oAll, &oLit);
    LogfAsync("night lamps: %s: %d lamps (%d others light nothing), %d more found in its .frd, %d of %d track "
              "vertices and %d of %d object vertices lit\n", name, street, skipped, g_nlLampN - street, vLit, vAll,
              oLit, oAll);
}
extern "C" { DWORD g_fnNightTint = 0x419750; }
extern "C" void NightLampsStub();
asm(
    ".globl _NightLampsStub\n"
    "_NightLampsStub:\n"
    "    call *_g_fnNightTint\n"            // the game darkens the track's colours for the night (or not)
    "    pushal\n"
    "    call _NightLamps\n"
    "    popal\n"
    "    ret\n");
void InstallNightLamps() {
    if (!g_knownExe) return;
    float k = g_cfg.nightLamps, reach = g_cfg.nightLampReach, more = g_cfg.nightScenery;
    if (!(k > 0.0f)) { Logf("night lamps: off\n"); return; }
    g_nlK = k > 4.0f ? 4.0f : k;
    g_nlReach = !(reach > 0.0f) ? 22.0f : reach < 2.0f ? 2.0f : reach > 100.0f ? 100.0f : reach;
    g_nlFoundK = !(more > 0.0f) ? 0.0f : more > 4.0f ? 4.0f : more;
    bool ok = PatchRel32(0x41A108, 0xE8, (void*)&NightLampsStub) && NL_FRD_HOOK;
    Logf("night lamps: x%.2f, reach %.1f m, scenery x%.2f: %s\n", g_nlK, g_nlReach, g_nlFoundK,
         ok ? "on" : "patch FAILED");
}
