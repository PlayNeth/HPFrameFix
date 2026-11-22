#include "common.h"
#include "interp.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "timing.h"
#include "nightlamps.h"
#include "menus.h"

// ---------------------------------------------------------------------------
// Transform snapshots
// ---------------------------------------------------------------------------
struct Xform { float p[3]; float m[9]; };
struct Entry { DWORD obj; int kind; Xform x; };    // kind: 0 car, 1 camera
static const int kMaxEntries = 96;                 // 16 cars + 48 table cameras + 4 active/override
struct Snapshot { int n = 0; long step = 0; DWORD activeCam[2] = {0, 0};   // effective (override or active)
                 DWORD camSlot[4] = {0, 0, 0, 0};                         // active[2], override[2] as stored
                 DWORD look[2] = {0, 0};                                  // look back flag the step's camera update saw
                 Entry e[kMaxEntries]; };

static const int kRing = 4;
static Snapshot g_ring[kRing];   // g_ring[g_head] = latest
static int  g_head = 0;
static int  g_ringN = 0;         // valid snapshots (0..kRing)
static long g_lastStepIdx = 0;
// Smoothed sim clock: step index s is shown at time g_clockT0 + s * p64.
// g_clockT0 follows the steps' arrival times with a slow average, so frame-to-
// frame lateness (vsync, input batching) never moves the timeline by a whole step.
static LONGLONG g_clockT0 = 0;
static bool g_clockValid = false;
std::atomic<unsigned long> g_clockSnaps{0};

static inline DWORD PosOff(int kind) { return kind == 1 ? hp::camPos : hp::carPos; }
static inline DWORD RotOff(int kind) { return kind == 1 ? hp::camRot : hp::carRot; }
static inline bool ReadXform(DWORD obj, int kind, Xform* x) {
    DWORD po = PosOff(kind), mo = RotOff(kind);
    DWORD lo = po < mo ? po : mo, hi = (po + 12 > mo + 36) ? po + 12 : mo + 36;
    if (!Readable(obj + lo, hi - lo)) return false;
    memcpy(x->p, (void*)(obj + po), 12);
    memcpy(x->m, (void*)(obj + mo), 36);
    return true;
}
static inline void WriteXform(DWORD obj, int kind, const Xform& x) {
    memcpy((void*)(obj + PosOff(kind)), x.p, 12);
    memcpy((void*)(obj + RotOff(kind)), x.m, 36);
}

DWORD EffectiveCam(int v) {
    DWORD o = Rd<DWORD>(hp::pCamOverride + v * 4);
    return o ? o : Rd<DWORD>(hp::pCamActive + v * 4);
}

static void Collect(Snapshot& s) {
    s.n = 0;
    s.activeCam[0] = EffectiveCam(0); s.activeCam[1] = EffectiveCam(1);
    for (int v = 0; v < 2; v++) { s.camSlot[v] = Rd<DWORD>(hp::pCamActive + v * 4);
                                  s.camSlot[2 + v] = Rd<DWORD>(hp::pCamOverride + v * 4);
                                  s.look[v] = Rd<DWORD>(hp::pLookBack + v * 4); }
    long nc = Rd<long>(hp::pCarCount);
    if (nc < 0) nc = 0;
    if (nc > 16) nc = 16;
    for (long i = 0; i < nc && s.n < kMaxEntries; i++) {
        DWORD car = Rd<DWORD>(hp::pCarList + i * 4);
        if (!car) continue;
        Entry& e = s.e[s.n]; e.obj = car; e.kind = 0;
        if (ReadXform(car, 0, &e.x)) s.n++;
    }
    DWORD seen[hp::camTableSlots + 4]; int ns = 0;
    auto addCam = [&](DWORD cam) {
        if (!cam || s.n >= kMaxEntries) return;
        for (int k = 0; k < ns; k++) if (seen[k] == cam) return;
        if (ns < hp::camTableSlots + 4) seen[ns++] = cam;
        Entry& e = s.e[s.n]; e.obj = cam; e.kind = 1;
        if (ReadXform(cam, 1, &e.x)) s.n++;
    };
    for (int i = 0; i < hp::camTableSlots; i++) addCam(Rd<DWORD>(hp::pCamTable + i * 4));
    for (int v = 0; v < 2; v++) { addCam(Rd<DWORD>(hp::pCamActive + v * 4));
                                  addCam(Rd<DWORD>(hp::pCamOverride + v * 4)); }
}

std::atomic<unsigned long> g_steps{0}, g_resets{0};

static LONGLONG StepPeriodQpc() { return TickPeriodQpc() * 2; }   // 64 Hz

static void TraceStep(const Snapshot& s);
extern "C" void __cdecl OnSimStep() {
    LONGLONG now = Qpc();
    long idx = Rd<long>(hp::pSimStepIdx);           // index of the step that just ran
    if (g_ringN && idx == g_lastStepIdx) {           // replay fast-forward: 2 steps per index
        Collect(g_ring[g_head]);
        g_steps++;
        return;
    }
    if (g_ringN && (idx < g_lastStepIdx || idx - g_lastStepIdx > 16)) {
        g_ringN = 0; g_clockValid = false; g_resets++;   // restart / jump: no blending across it
    }
    g_lastStepIdx = idx;
    g_head = (g_head + 1) % kRing;
    Snapshot& s = g_ring[g_head];
    Collect(s);
    s.step = idx;
    if (g_ringN < kRing) g_ringN++;
    if (kTestBuild) TraceStep(s);

    LONGLONG p = StepPeriodQpc();
    LONGLONG t0 = now - (LONGLONG)idx * p;           // arrival-based origin of this step
    if (!g_clockValid) { g_clockT0 = t0; g_clockValid = true; }
    else {
        LONGLONG d = t0 - g_clockT0;
        if (d > 4 * p || d < -4 * p) { g_clockT0 = t0; g_clockSnaps++; }   // lost sync: snap
        else g_clockT0 += d / 32;                                         // slow follow
    }
    g_steps++;
}

// ---------------------------------------------------------------------------
// Interpolation math
// ---------------------------------------------------------------------------

Quat MatToQuat(const float* m) {   // m row-major 3x3
    Quat q; float tr = m[0] + m[4] + m[8];
    if (tr > 0) { float s = sqrtf(tr + 1.0f) * 2; q.w = 0.25f * s; q.x = (m[7] - m[5]) / s;
                  q.y = (m[2] - m[6]) / s; q.z = (m[3] - m[1]) / s; }
    else if (m[0] > m[4] && m[0] > m[8]) { float s = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2;
                  q.w = (m[7] - m[5]) / s; q.x = 0.25f * s; q.y = (m[1] + m[3]) / s; q.z = (m[2] + m[6]) / s; }
    else if (m[4] > m[8]) { float s = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2;
                  q.w = (m[2] - m[6]) / s; q.x = (m[1] + m[3]) / s; q.y = 0.25f * s; q.z = (m[5] + m[7]) / s; }
    else { float s = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2;
                  q.w = (m[3] - m[1]) / s; q.x = (m[2] + m[6]) / s; q.y = (m[5] + m[7]) / s; q.z = 0.25f * s; }
    return q;
}
void QuatToMat(const Quat& q, float* m) {
    float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z, xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z,
          wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    m[0] = 1 - 2*(yy+zz); m[1] = 2*(xy-wz);     m[2] = 2*(xz+wy);
    m[3] = 2*(xy+wz);     m[4] = 1 - 2*(xx+zz); m[5] = 2*(yz-wx);
    m[6] = 2*(xz-wy);     m[7] = 2*(yz+wx);     m[8] = 1 - 2*(xx+yy);
}
bool IsRotation(const float* m) {
    for (int r = 0; r < 3; r++) {
        float l = m[r*3]*m[r*3] + m[r*3+1]*m[r*3+1] + m[r*3+2]*m[r*3+2];
        if (fabsf(l - 1.0f) > 0.02f) return false;
    }
    float det = m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6]) + m[2]*(m[3]*m[7]-m[4]*m[6]);
    return fabsf(det - 1.0f) < 0.05f;
}
// returns false if the pair should not be blended
static bool Blend(const Xform& a, const Xform& b, float t, Xform* out) {
    float dx = b.p[0]-a.p[0], dy = b.p[1]-a.p[1], dz = b.p[2]-a.p[2];
    if (dx*dx + dy*dy + dz*dz > g_cfg.maxStepDistance * g_cfg.maxStepDistance) return false;
    for (int i = 0; i < 3; i++) out->p[i] = a.p[i] + (b.p[i] - a.p[i]) * t;
    if (!memcmp(a.m, b.m, 36)) { memcpy(out->m, b.m, 36); return true; }
    if (!IsRotation(a.m) || !IsRotation(b.m)) {          // not a pure rotation: plain lerp
        for (int i = 0; i < 9; i++) out->m[i] = a.m[i] + (b.m[i] - a.m[i]) * t;
        return true;
    }
    Quat qa = MatToQuat(a.m), qb = MatToQuat(b.m);
    float d = qa.w*qb.w + qa.x*qb.x + qa.y*qb.y + qa.z*qb.z;
    if (d < 0) { qb.w = -qb.w; qb.x = -qb.x; qb.y = -qb.y; qb.z = -qb.z; d = -d; }
    if (d > 1) d = 1;
    float ang = 2.0f * acosf(d) * 57.29578f;
    if (ang > g_cfg.maxStepAngle) return false;
    Quat q;
    if (d > 0.9995f) {
        q.w = qa.w + (qb.w-qa.w)*t; q.x = qa.x + (qb.x-qa.x)*t;
        q.y = qa.y + (qb.y-qa.y)*t; q.z = qa.z + (qb.z-qa.z)*t;
    } else {
        float th = acosf(d), s = sinf(th), wa = sinf((1-t)*th)/s, wb = sinf(t*th)/s;
        q.w = qa.w*wa + qb.w*wb; q.x = qa.x*wa + qb.x*wb; q.y = qa.y*wa + qb.y*wb; q.z = qa.z*wa + qb.z*wb;
    }
    float l = sqrtf(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (l < 1e-6f) return false;
    q.w /= l; q.x /= l; q.y /= l; q.z /= l;
    QuatToMat(q, out->m);
    return true;
}

static const Entry* FindEntry(const Snapshot& s, DWORD obj, int kind) {
    for (int j = 0; j < s.n; j++) if (s.e[j].obj == obj && s.e[j].kind == kind) return &s.e[j];
    return nullptr;
}

// a camera's place in its car's own axes (a world vector v is v . row i of the car's matrix along axis i)
static const float kCamJump = 3.0f;               // m in one step against the car: a cut (smooth motion is under 0.5)
static bool RelPos(const Xform& cam, const Xform& car, float rel[3]) {
    if (!IsRotation(car.m)) return false;
    float d[3] = { cam.p[0]-car.p[0], cam.p[1]-car.p[1], cam.p[2]-car.p[2] };
    for (int i = 0; i < 3; i++) rel[i] = d[0]*car.m[i*3] + d[1]*car.m[i*3+1] + d[2]*car.m[i*3+2];
    return true;
}
static float RotAngle(const float* a, const float* b) {            // degrees between two rotations (-1: not rotations)
    if (!IsRotation(a) || !IsRotation(b)) return -1.0f;
    Quat qa = MatToQuat(a), qb = MatToQuat(b);
    float d = fabsf(qa.w*qb.w + qa.x*qb.x + qa.y*qb.y + qa.z*qb.z);
    return 2.0f * acosf(d > 1 ? 1 : d) * 57.29578f;
}

// ---------------------------------------------------------------------------
// Render wrapper
// ---------------------------------------------------------------------------
static Snapshot g_saved;          // live values while an interpolated frame is drawn
static Xform    g_written[kMaxEntries];
static bool     g_wrote[kMaxEntries];
static bool g_applied = false;
static bool g_slotsSwapped = false;
static DWORD g_liveSlot[4], g_drawSlot[4];
static bool g_lookSet[2] = { false, false };     // the look back flag was set for this frame (live value, value drawn with)
static DWORD g_lookLive[2], g_lookDraw[2];

// ---------------------------------------------------------------------------
// Look back trace (test builds). Every sim step and every frame drawn leaves a
// record for view 0: the look back flag, and where the camera is and how far
// it's turned against the car it follows (cam+4), in the car's own axes. For a
// step that's the game's own values right after the step; for a frame it's
// what was on screen (read after the render, before anything is put back),
// with the rotation the mod wrote next to the one the render left - they
// differ when the render works the camera's rotation out itself. When the
// flag changes, the records around it are written to the log (8 times at
// most), so a press and a release can be read step by step.
// ---------------------------------------------------------------------------
struct TraceRec { char kind, how; bool ok; long a, b; float t; DWORD flag, live, cam; float rel[3], ang, angW; };
static const int kTrace = 64, kTraceAfter = 28, kTraceLines = 44;
static TraceRec g_tr[kTrace];
static int g_trAt = 0, g_trLeft = -1, g_trDumps = 0;
static bool g_trFlag = false;
static long g_trFA = 0, g_trFB = 0; static float g_trFT = 0; static char g_trHow = 'l';
static bool CamVsCar(const Xform& cam, const Xform& car, float rel[3], float* ang) {
    if (!RelPos(cam, car, rel)) return false;
    *ang = RotAngle(car.m, cam.m);
    return true;
}
static void TraceAdd(const TraceRec& r) {
    g_tr[g_trAt % kTrace] = r; g_trAt++;
    if (g_trLeft <= 0 || --g_trLeft) return;
    int n = g_trAt < kTraceLines ? g_trAt : kTraceLines;
    LogfAsync("  trace %d: look back %s\n", g_trDumps + 1, g_trFlag ? "pressed" : "released");
    for (int k = g_trAt - n; k < g_trAt; k++) {
        const TraceRec& q = g_tr[k % kTrace];
        if (q.kind == 'S')
            LogfAsync("  trace step %ld: look back %lu, camera %08lX at (%.2f, %.2f, %.2f) from its car, %.0f deg against it%s\n",
                      q.a, (unsigned long)q.flag, (unsigned long)q.cam, q.rel[0], q.rel[1], q.rel[2], q.ang, q.ok ? "" : " (?)");
        else
            LogfAsync("  trace   frame %ld-%ld t=%.2f %s: look back %lu (live %lu), camera at (%.2f, %.2f, %.2f), %.0f deg (written %.0f)%s\n",
                      q.a, q.b, q.t, q.how == 'b' ? "blended" : q.how == 'n' ? "nearest" : q.how == 'l' ? "live" : "untouched",
                      (unsigned long)q.flag, (unsigned long)q.live, q.rel[0], q.rel[1], q.rel[2], q.ang, q.angW, q.ok ? "" : " (?)");
    }
    g_trAt = 0; g_trLeft = -1; g_trDumps++;
}
static void TraceStep(const Snapshot& s) {
    TraceRec r = {}; r.kind = 'S'; r.a = s.step; r.flag = r.live = s.look[0]; r.cam = s.activeCam[0]; r.ang = r.angW = -1;
    const Entry* ce = r.cam ? FindEntry(s, r.cam, 1) : nullptr;
    DWORD car = ce ? Rd<DWORD>(r.cam + hp::camCar) : 0;
    const Entry* ke = car ? FindEntry(s, car, 0) : nullptr;
    r.ok = ce && ke && CamVsCar(ce->x, ke->x, r.rel, &r.ang);
    bool flag = s.look[0] != 0;
    if (flag != g_trFlag) {
        g_trFlag = flag;
        if (g_trLeft < 0 && g_trDumps < 8) g_trLeft = kTraceAfter + 1;
    }
    TraceAdd(r);
}
static void TraceFrame() {                         // top of PostRender: what this frame was drawn with
    TraceRec r = {}; r.kind = 'F'; r.how = g_applied ? g_trHow : 'l'; r.a = g_trFA; r.b = g_trFB; r.t = g_trFT;
    r.flag = Rd<DWORD>(hp::pLookBack); r.live = g_lookSet[0] ? g_lookLive[0] : r.flag;
    r.cam = EffectiveCam(0); r.ang = r.angW = -1;
    Xform cx, kx;
    DWORD car = r.cam && Readable(r.cam + hp::camCar, 4) ? Rd<DWORD>(r.cam + hp::camCar) : 0;
    if (car && ReadXform(r.cam, 1, &cx) && ReadXform(car, 0, &kx)) {
        r.ok = CamVsCar(cx, kx, r.rel, &r.ang);
        if (g_applied)
            for (int i = 0; i < g_saved.n; i++)
                if (g_wrote[i] && g_saved.e[i].kind == 1 && g_saved.e[i].obj == r.cam) { r.angW = RotAngle(kx.m, g_written[i].m); break; }
    }
    TraceAdd(r);
}
static inline DWORD CamSlotAddr(int i) { return i < 2 ? hp::pCamActive + i * 4 : hp::pCamOverride + (i - 2) * 4; }
HANDLE g_limTimer = nullptr;
static LONGLONG g_limNext = 0;
std::atomic<unsigned long> g_renders{0}, g_extraRenders{0}, g_blended{0}, g_rejected{0}, g_cuts{0};
// written by the game thread, read and reset by the stats thread (approximate per-window stats)
volatile double g_alphaSum = 0; volatile unsigned long g_alphaN = 0, g_alphaClamped = 0;
volatile bool g_clampHave = false; SYSTEMTIME g_clampFirst;
volatile double g_clampAheadMs = 0, g_clampBehindMs = 0;
static void NoteClamp(double steps) {             // >0: frame ahead of newest step, <0: behind history
    double ms = steps * 1000.0 / 64.0;
    if (!g_clampHave) { GetLocalTime(&g_clampFirst); g_clampHave = true; }
    if (ms > g_clampAheadMs) g_clampAheadMs = ms;
    if (-ms > g_clampBehindMs) g_clampBehindMs = -ms;
}
extern "C" { int g_extraEnabled = 0; DWORD g_fnSimStep = hp::fnSimStep, g_fnRender = hp::fnRender,
                                     g_fnMenuIdle = 0x4C9860; }

// Frames reach the screen on the display's schedule, not when the CPU happens
// to start them, so the time used for blending follows a smoothed frame clock.
static LONGLONG g_ftLast = 0, g_ftInterval = 0;
static LONGLONG FrameTime() {
    LONGLONG now = Qpc();
    if (!g_ftLast) { g_ftLast = now; return now; }
    LONGLONG d = now - g_ftLast;
    if (!g_ftInterval) g_ftInterval = d;
    LONGLONG pred = g_ftLast + g_ftInterval;
    LONGLONG err = now - pred;
    LONGLONG ft;
    if (g_ftInterval > 0 && err < g_ftInterval / 2 && err > -g_ftInterval / 2) {
        ft = pred + err / 8;                                  // on schedule: follow gently
        g_ftInterval += (d - g_ftInterval) / 16;
    } else {
        ft = now;                                             // hitch / rate change: resync
        if (d > 0 && d < g_qpf.QuadPart / 10) g_ftInterval = d;
    }
    g_ftLast = ft;
    return ft;
}

static void LimitTo(int fps, LONGLONG& next) {
    if (fps <= 0) return;
    LONGLONG period = g_qpf.QuadPart / fps, now = Qpc();
    if (next && now < next) { WaitUntilOn(g_limTimer, next); now = Qpc(); }
    if (!next || now - next > period) next = now + period; else next += period;
}

static LONGLONG g_menuNext = 0;
std::atomic<unsigned long> g_menuFrames{0};
std::atomic<long> g_menuGapMaxUs{0};       // longest time between two menu frames
// Menu frame ceiling: auto = the highest rate <= 128 that divides the refresh rate
// (re-read every 2 s, the game may change display mode).
// The display's refresh rate, re-read every 2 s (the game may change display mode).
// EnumDisplaySettings can take milliseconds (under Wine it asks the X server),
// so it's never called on the game thread after the first time: a background
// thread refreshes the value every 2 s.
static std::atomic<int> g_displayHz{0};
static int QueryDisplayHz() {
    DEVMODEA dm = {}; dm.dmSize = sizeof(dm);
    int hz = 0;
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) hz = (int)dm.dmDisplayFrequency;
    if (hz < 30 || hz > 1000) hz = 60;                     // "default"/unknown
    return hz;
}
static DWORD WINAPI DisplayThread(LPVOID) {
    for (;;) { g_displayHz = QueryDisplayHz(); Sleep(2000); }
}
static int DisplayHz() {
    int hz = g_displayHz.load();
    if (!hz) {                                             // first use, before the thread ran
        hz = QueryDisplayHz(); g_displayHz = hz;
        static bool started = false;
        if (!started) { started = true; if (HANDLE th = StartModThread(DisplayThread)) CloseHandle(th); }
    }
    return hz;
}
// targetFPS=-1: a limit that never fights vsync. A limiter at exactly the
// refresh rate on top of vsync beats against it (its clock and the display's
// drift apart: frames start late, miss a refresh, and blending stutters). So
// the limit starts at 1.5x the refresh rate, which vsync never reaches, and
// only if frames then run faster than the refresh rate (no vsync: software
// renderer, or vsync off) does it drop to exactly the refresh rate.
static int  g_autoMode = 0;          // 0 = not known yet / vsync, 1 = no vsync: limit to the refresh rate
static LONGLONG g_autoWin = 0; static unsigned long g_autoFrames = 0;
static int RaceFPS() {
    if (g_cfg.targetFPS >= 0) return g_cfg.targetFPS;
    int hz = DisplayHz();
    return g_autoMode ? hz : hz * 3 / 2;
}
static void AutoLimitCheck() {        // once per race frame, auto mode only
    if (g_cfg.targetFPS >= 0 || g_autoMode) return;
    LONGLONG now = Qpc();
    if (!g_autoWin) { g_autoWin = now; g_autoFrames = 0; return; }
    g_autoFrames++;
    LONGLONG d = now - g_autoWin;
    if (d < 2 * g_qpf.QuadPart) return;
    double fps = g_autoFrames * (double)g_qpf.QuadPart / (double)d;
    int hz = DisplayHz();
    if (fps > hz * 1.1) {
        g_autoMode = 1;
        LogfAsync("limiter: %.0f fps on a %d Hz display - no vsync, frames limited to %d fps\n", fps, hz, hz);
    }
    g_autoWin = now; g_autoFrames = 0;
}
static void Limiter() { AutoLimitCheck(); LimitTo(RaceFPS(), g_limNext); }
static int MenuCeiling() {
    if (g_cfg.menuMaxFPS >= 0) return g_cfg.menuMaxFPS;
    static int cached = 0, lastHz = 0;
    int hz = DisplayHz();
    if (hz != lastHz) {
        lastHz = hz;
        // highest rate <= 128 that divides the refresh rate (even pacing), not
        // below 50; otherwise the first rate <= 128 (e.g. 165 Hz -> 55, 144 -> 72)
        int c = 0, first = 0;
        for (int n = 1; n <= 64; n++) {
            int r = hz / n;
            if (r > 128) continue;
            if (!first) first = r;
            if (r < 50) break;
            if (hz % n == 0) { c = r; break; }
        }
        if (!c) c = first ? first : 60;
        if (c != cached) { cached = c; LogfAsync("menus: frame ceiling %d fps (display %d Hz)\n", c, hz); }
    }
    return cached;
}
// The game's menu wait called the menu idle work (0x4C9860) over and over
// while it waited, and that work refills the front-end music/speech stream one
// chunk per call (0x4C9380). Called once per frame, a stream drained by a slow
// frame (switching menus) refilled too slowly and crackled. So it runs a few
// times every frame (cheap when the stream is full), and the menu frame limit
// waits in ~1 ms slices with the idle work in between, as the game did.
extern "C" void CallMenuIdle();
static void MenuLimitTo(int fps, LONGLONG& next) {
    for (int i = 0; i < 3; i++) CallMenuIdle();       // + the stub's call = 4 per frame
    if (fps <= 0) return;
    LONGLONG period = g_qpf.QuadPart / fps, now = Qpc();
    if (next && now < next) {
        LONGLONG twoMs = g_qpf.QuadPart / 500;
        for (;;) {
            if (next - now > twoMs) Sleep(1);           // coarse slice, then idle work
            else { WaitUntilOn(g_limTimer, next); break; }
            CallMenuIdle();
            now = Qpc();
            if (now >= next) break;
        }
        now = Qpc();
    }
    if (!next || now - next > period) next = now + period; else next += period;
}
extern "C" void __cdecl MenuFrame() {
    int fps = g_cfg.menuTargetFPS < 0 ? RaceFPS() : g_cfg.menuTargetFPS;
    int cap = MenuCeiling();
    if (cap > 0 && (fps <= 0 || fps > cap)) fps = cap;
    MenuLimitTo(fps, g_menuNext);
    g_menuFrames++;
    static LONGLONG lastFrame = 0;                // game thread only
    LONGLONG now = Qpc();
    if (lastFrame) {
        LONGLONG us = (now - lastFrame) * 1000000 / g_qpf.QuadPart;
        if (us < 5000000 && us > g_menuGapMaxUs.load()) g_menuGapMaxUs = (long)us;   // (longer: the menus weren't running)
    }
    lastFrame = now;
}

// A frame that isn't blended is drawn from the live values. For a camera whose
// rotation the render works on in place, those aren't the simulation's any
// more once a frame has been drawn: the cockpit camera's head movement
// (0x421730, cam+0x104 set) multiplies the matrix at cam+0x2C by a small
// rotation at every render, where the game drew once per step. A second frame
// on the same step turned it twice - the picture shook, most with the software
// renderer, where many frames come after the newest step. So such a frame
// first gets the newest step's own values back for the cameras in use.
static void CamsToNewest() {
    if (g_ringN < 1) return;
    const Snapshot& nb = g_ring[g_head];
    // (only while that snapshot is the step the game is on: at the start of the
    // next race the ring still holds the last race's, until its first step)
    long cur = Rd<long>(hp::pSimStepIdx);           // (the step's own index until the game counts on, then one more)
    if (nb.step != cur && nb.step + 1 != cur) return;
    for (int v = 0; v < 2; v++) {
        DWORD cam = EffectiveCam(v);
        if (!cam || nb.activeCam[v] != cam) continue;        // (switched since the step: left alone)
        if (v == 1 && cam == nb.activeCam[0]) continue;
        if (const Entry* e = FindEntry(nb, cam, 1)) WriteXform(cam, 1, e->x);
    }
}
extern "C" void __cdecl PreRender() {
    Limiter();
    g_renders++;
    PzLimitsCheck();
    g_applied = false;
    if (!g_cfg.interpolate || g_ringN < 2 || !g_clockValid) { CamsToNewest(); return; }

    // Position on the smoothed sim timeline, one step (interpDelay) behind.
    LONGLONG p = StepPeriodQpc();
    double r = (double)(FrameTime() - g_clockT0) / (double)p - g_cfg.interpDelay;
    // Find consecutive snapshots A.step <= r < B.step (newest first).
    const Snapshot* A = nullptr; const Snapshot* B = nullptr;
    for (int k = 0; k + 1 < g_ringN; k++) {
        const Snapshot& nb = g_ring[(g_head - k + kRing) % kRing];
        const Snapshot& na = g_ring[(g_head - k - 1 + kRing) % kRing];
        if (k == 0 && r >= (double)nb.step) {
            g_alphaClamped++; g_alphaSum += 1; g_alphaN++; NoteClamp(r - (double)nb.step); CamsToNewest(); return;
        }
        if (r >= (double)na.step) { A = &na; B = &nb; break; }
    }
    if (!A) {                                        // older than the history: oldest pair
        int k = g_ringN - 2;
        B = &g_ring[(g_head - k + kRing) % kRing];
        A = &g_ring[(g_head - k - 1 + kRing) % kRing];
        g_alphaClamped++;
        NoteClamp(r - (double)A->step);
        r = (double)A->step;
    }
    long span = B->step - A->step;
    if (span <= 0 || span > 8) { CamsToNewest(); return; }
    float t = (float)((r - (double)A->step) / (double)span);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    g_alphaSum += t; g_alphaN++;
    g_trFA = A->step; g_trFB = B->step; g_trFT = t; g_trHow = 'x';

    // Every object is drawn at the same moment r. An object that can't be
    // blended across this pair (a camera cut, looking back, a reset) takes the
    // nearer of the two snapshots instead of its live value: the live values
    // are the newest step, up to two steps ahead of r, and showing them for the
    // cut frame and then going back to r made a visible back-and-forth jump.
    // A camera switch after B (the game already uses another camera) is drawn
    // with B's camera until r reaches the switch: the camera slots are set to
    // B's for this frame and put back afterwards.
    // The look back flag is set once per pass of the main loop, but the frame
    // shows a moment one to two steps earlier: the render's own uses of the flag
    // (cockpit picture, mirror, per-view caches) would switch that much before
    // the camera drawn does. So for this frame the flag is the one the snapshot
    // nearer to r was made with - the same choice as for a camera that can't be
    // blended - and it's put back afterwards.
    for (int v = 0; v < 2; v++) {
        DWORD live = Rd<DWORD>(hp::pLookBack + v * 4), want = (t < 0.5f ? A : B)->look[v];
        g_lookSet[v] = want != live;
        if (g_lookSet[v]) { g_lookLive[v] = live; g_lookDraw[v] = want; *(volatile DWORD*)(hp::pLookBack + v * 4) = want; }
    }
    bool cut = false;
    g_slotsSwapped = false;
    for (int i = 0; i < 4; i++) {
        g_liveSlot[i] = Rd<DWORD>(CamSlotAddr(i));
        g_drawSlot[i] = g_liveSlot[i];
        if (B->camSlot[i] != g_liveSlot[i] && (B->camSlot[i] || i >= 2)) {
            g_drawSlot[i] = B->camSlot[i];
            *(volatile DWORD*)CamSlotAddr(i) = B->camSlot[i];
            g_slotsSwapped = true;
        }
    }
    DWORD liveCam[2] = { EffectiveCam(0), EffectiveCam(1) };
    for (int v = 0; v < 2; v++)
        if (A->activeCam[v] != B->activeCam[v] || liveCam[v] != B->activeCam[v]) cut = true;

    // Current live values (normally == the newest step) are saved and restored after drawing.
    Collect(g_saved);
    for (int i = 0; i < g_saved.n; i++) {
        g_wrote[i] = false;
        const Entry& cur = g_saved.e[i];
        bool active = cur.kind == 1 && (cur.obj == liveCam[0] || cur.obj == liveCam[1]);
        if (active && cur.obj != B->activeCam[0] && cur.obj != B->activeCam[1]) continue;   // new: live
        const Entry* pb = FindEntry(*B, cur.obj, cur.kind);
        if (!pb) continue;
        const Entry* pa = FindEntry(*A, cur.obj, cur.kind);
        // switched to between A and B: its A snapshot is from before it was in use
        bool switched = active && cur.obj != A->activeCam[0] && cur.obj != A->activeCam[1];
        Xform x;
        // A camera that jumps against the car it follows is a cut, however far
        // it moved in the world. Looking back puts a chase camera at the other
        // end of the car, 10.3 m away: with the car's own travel that's just
        // over maxStepDistance on the press (a cut) and just under it on the
        // release - which was blended, and swept the camera through the car
        // over two frames, turning round on the way.
        bool relCut = false;
        if (active && pa && !switched && Readable(cur.obj + hp::camCar, 4)) {
            DWORD car = Rd<DWORD>(cur.obj + hp::camCar);
            const Entry* ka = car ? FindEntry(*A, car, 0) : nullptr;
            const Entry* kb = car ? FindEntry(*B, car, 0) : nullptr;
            float ra[3], rb[3];
            if (ka && kb && RelPos(pa->x, ka->x, ra) && RelPos(pb->x, kb->x, rb)) {
                float dx = rb[0]-ra[0], dy = rb[1]-ra[1], dz = rb[2]-ra[2];
                relCut = dx*dx + dy*dy + dz*dz > kCamJump * kCamJump;
            }
        }
        if (pa && !switched && !relCut && Blend(pa->x, pb->x, t, &x)) { g_blended++; if (active && cur.obj == liveCam[0]) g_trHow = 'b'; }
        else {
            if (active && cur.obj == liveCam[0]) g_trHow = 'n';
            const Snapshot* X = (pa && !switched && t < 0.5f) ? A : B;
            x = X == A ? pa->x : pb->x;
            g_rejected++;
            if (active) {
                // The camera can't be blended (looking back moves it to the
                // other end of the car one step and turns it the next), so
                // it's taken from the nearer snapshot - but moved along with
                // the car it follows, which is drawn at r: otherwise the car
                // jumps back and forth under it on these frames.
                const Entry* folw = nullptr; float best = 1e30f;
                for (int j = 0; j < X->n; j++) {
                    const Entry& c = X->e[j];
                    if (c.kind != 0) continue;
                    float dx = c.x.p[0]-x.p[0], dy = c.x.p[1]-x.p[1], dz = c.x.p[2]-x.p[2];
                    float d = dx*dx + dy*dy + dz*dz;
                    if (d < best) { best = d; folw = &c; }
                }
                if (folw && best < 60.0f * 60.0f)
                    for (int j = 0; j < i; j++)
                        if (g_wrote[j] && g_saved.e[j].kind == 0 && g_saved.e[j].obj == folw->obj) {
                            for (int k = 0; k < 3; k++) x.p[k] += g_written[j].p[k] - folw->x.p[k];
                            break;
                        }
                cut = true;
            }
        }
        WriteXform(cur.obj, cur.kind, x);
        g_written[i] = x; g_wrote[i] = true;
    }
    if (cut) g_cuts++;
    g_applied = true;
}

extern "C" void __cdecl PostRender() {
    if (kTestBuild) TraceFrame();
    for (int v = 0; v < 2; v++)                  // the live look back flag back, unless the render changed it
        if (g_lookSet[v]) {
            if (Rd<DWORD>(hp::pLookBack + v * 4) == g_lookDraw[v]) *(volatile DWORD*)(hp::pLookBack + v * 4) = g_lookLive[v];
            g_lookSet[v] = false;
        }
    if (g_slotsSwapped) {      // the game's camera slots back, unless the render changed one
        for (int i = 0; i < 4; i++)
            if (Rd<DWORD>(CamSlotAddr(i)) == g_drawSlot[i]) *(volatile DWORD*)CamSlotAddr(i) = g_liveSlot[i];
        g_slotsSwapped = false;
    }
    if (!g_applied) return;
    // Put the simulation values back - unless the render itself changed them
    // (e.g. a camera switch re-initialises the new camera). Position and
    // rotation are judged separately: the render works out the rotation of a
    // camera that looks at its car itself (0x422290, from the position drawn),
    // and that used to leave such a camera's position at the frame's blended
    // value too, a step or more behind the simulation's, until the next step.
    for (int i = 0; i < g_saved.n; i++) {
        if (!g_wrote[i]) continue;
        const Entry& e = g_saved.e[i];
        Xform now;
        if (!ReadXform(e.obj, e.kind, &now)) continue;
        if (!memcmp(now.p, g_written[i].p, 12)) memcpy((void*)(e.obj + PosOff(e.kind)), e.x.p, 12);
        if (!memcmp(now.m, g_written[i].m, 36)) memcpy((void*)(e.obj + RotOff(e.kind)), e.x.m, 36);
    }
    g_applied = false;
}

extern "C" void __cdecl NoteExtraRender() { g_extraRenders++; }

// Stubs (the game is Watcom code: every stub preserves every register and the flags)
extern "C" void StepStub();
extern "C" void RenderStub();
extern "C" void ExtraRenderStub();
asm(".text\n"
    // call site of the sim step: run it (eax = step index, untouched), then snapshot
    ".globl _StepStub\n"
    "_StepStub:\n"
    "    call *_g_fnSimStep\n"
    "    pushfl\n"
    "    pushal\n"
    "    call _OnSimStep\n"
    "    popal\n"
    "    popfl\n"
    "    ret\n"
    // call site of the render: eax = argument
    ".globl _RenderStub\n"
    "_RenderStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    call _PreRender\n"
    "    popal\n"
    "    popfl\n"
    "    call *_g_fnRender\n"
    "    pushfl\n"
    "    pushal\n"
    "    call _PostRender\n"
    "    popal\n"
    "    popfl\n"
    "    ret\n"
    // replaces "cmp [0x7D3674],0 / jne 0x4C548E" (jumped to). edi = the game's
    // "first frame pending" local: left to the game.
    ".globl _ExtraRenderStub\n"
    "_ExtraRenderStub:\n"
    "    cmpl $0, 0x7D3674\n"
    "    jne 1f\n"
    "    pushl $0x4C5477\n"           // new step: original render decision
    "    ret\n"
    "1:  cmpl $0, _g_extraEnabled\n"
    "    je 2f\n"
    "    testl %edi, %edi\n"
    "    jne 2f\n"
    "    pushfl\n"
    "    pushal\n"
    "    call _NoteExtraRender\n"
    "    popal\n"
    "    popfl\n"
    "    movl 0x7D36A0, %eax\n"
    "    call _RenderStub\n"
    "2:  pushl $0x4C548E\n"
    "    ret\n"
    // replaces the menu's "wait for 2 ticks" loop (jumped to, inside 0x4D9140)
    ".globl _MenuWaitStub\n"
    "_MenuWaitStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    call *_g_fnMenuIdle\n"       // the work the wait loop used to do
    "    call _MenuFrame\n"           // optional limiter
    "    popal\n"
    "    popfl\n"
    "    movl 0x56415C, %eax\n"       // registers as the original loop leaves them
    "    addl $2, %eax\n"
    "    movl 0x79C1A4, %edx\n"
    "    pushl $0x4D9172\n"           // mov [0x56415C], edx
    "    ret\n"
    // the menu idle work (Watcom: may clobber eax-edx), callable from C
    ".globl _CallMenuIdle\n"
    "_CallMenuIdle:\n"
    "    pushal\n"
    "    call *_g_fnMenuIdle\n"
    "    popal\n"
    "    ret\n");

void InstallInterpolation() {
    if (!g_knownExe || !g_cfg.interpolate) { Logf("interpolation: off\n"); return; }
    bool ok = true;
    for (DWORD at : hp::callSimStep) ok &= PatchRel32(at, 0xE8, (void*)&StepStub);
    ok &= PatchRel32(hp::callRender, 0xE8, (void*)&RenderStub);
    BYTE j[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
    DWORD rel = (DWORD)&ExtraRenderStub - (hp::skipRenderAt + 5);
    memcpy(j + 1, &rel, 4);
    ok &= PatchBytes(hp::skipRenderAt, j, sizeof(j));
    g_extraEnabled = ok ? 1 : 0;
    Logf("interpolation: %s\n", ok ? "installed" : "patch FAILED");
}
