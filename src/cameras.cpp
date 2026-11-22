#include "common.h"
#include "cameras.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "interp.h"
#include "speedfx.h"
#include "nightlamps.h"

// ---------------------------------------------------------------------------
// The low and roof cameras. The game's views (low, high, roof, bumper,
// cockpit, heli) are camera types from its camera setup; the two chase
// cameras 1 m up, the low and the roof one (types 4 and 6: cam+0x110 = 0,
// 1.0, -4.0 / -3.25 in the car's axes, set at 0x422F67 / 0x422FEF), are both
// updated by 0x4243A0, reached each step through
// the per-step camera update 0x426450's table at 0x4264A8. They're placed
// against the car with its rotation, so every bump and twitch of the car
// goes straight into the picture. After that update the car's rotation is
// followed with a lag instead: the difference, in the car's own axes (x
// pitch, y yaw, z roll), dies away with a time constant per axis and is held
// within a few degrees, and the camera's place and rotation are turned round
// the car by what's left of it (W = lagging^T x car's), so it swings round
// the car in turns and rides out bumps; its height trails a little. The
// game's own result is put back before its next update (it may build on the
// last one), so the game never sees the change. Looking back, a switch or a
// jump starts it afresh. Done on the simulation's values, so frames are
// blended from them.
// ---------------------------------------------------------------------------
struct LooseCam { bool valid; DWORD cam, car; long step; Quat q; float y;
                  bool raw; float rawP[3], rawM[9], outP[3], outM[9]; };
static LooseCam g_loose[2];
static inline bool LooseType(int t) { return t == 4 || t == 6; }
static Quat QMul(const Quat& a, const Quat& b) {
    return { a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z, a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
             a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x, a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w };
}
static void QLog(Quat q, float r[3]) {           // rotation vector (axis x angle)
    if (q.w < 0) { q.w = -q.w; q.x = -q.x; q.y = -q.y; q.z = -q.z; }
    float s = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z);
    float k = s < 1e-7f ? 2.0f : 2.0f * atan2f(s, q.w) / s;
    r[0] = q.x * k; r[1] = q.y * k; r[2] = q.z * k;
}
static Quat QExp(const float r[3]) {
    float a = sqrtf(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
    if (a < 1e-7f) return { 1, r[0] * 0.5f, r[1] * 0.5f, r[2] * 0.5f };
    float s = sinf(a * 0.5f) / a;
    return { cosf(a * 0.5f), r[0] * s, r[1] * s, r[2] * s };
}
// before the camera's update: the game's own last result back
extern "C" void __cdecl LowCamBefore(DWORD cam) {
    LooseCam& s = g_loose[Rd<int>(cam + 0xC) & 1];
    if (!s.raw || s.cam != cam) return;
    s.raw = false;
    float* m = (float*)(cam + hp::camRot); float* p = (float*)(cam + hp::camPos);
    // each unless something changed it since (a chase camera's rotation may be
    // worked out again from its place when it's drawn)
    if (!memcmp(m, s.outM, 36)) memcpy(m, s.rawM, 36);
    if (!memcmp(p, s.outP, 12)) memcpy(p, s.rawP, 12);
}
extern "C" void __cdecl LowCamAfter(DWORD cam) {
    int v = Rd<int>(cam + 0xC) & 1;               // its view (the look back flag's index, as the update uses it)
    LooseCam& s = g_loose[v];
    s.raw = false;
    if (!LooseType(Rd<int>(cam))) return;
    DWORD car = Rd<DWORD>(cam + hp::camCar);
    long step = Rd<long>(hp::pSimStepIdx);
    float* m = (float*)(cam + hp::camRot);
    float* p = (float*)(cam + hp::camPos);
    if (!car || !CarListed(car) || Rd<int>(0x79C4A0 + v * 4)) { s.valid = false; return; }
    const float* cm = (const float*)(car + hp::carRot);
    const float* cp = (const float*)(car + hp::carPos);
    if (!IsRotation(cm) || !IsRotation(m)) { s.valid = false; return; }
    Quat qt = MatToQuat(cm);
    float e[3] = { 0, 0, 0 };
    bool fresh = !s.valid || s.cam != cam || s.car != car || step < s.step || step - s.step > 4;
    if (!fresh) {
        float d = qt.w*s.q.w + qt.x*s.q.x + qt.y*s.q.y + qt.z*s.q.z;
        Quat c = { s.q.w, -s.q.x, -s.q.y, -s.q.z };
        QLog(QMul(qt, c), e);                       // qt = exp(e) * q
        fresh = fabsf(d) < 0.95f;                   // over ~36 degrees apart: a cut
    }
    if (fresh) { s.valid = true; s.cam = cam; s.car = car; s.step = step; s.q = qt; s.y = p[1]; return; }
    float k = g_cfg.lowCamLoose > 3 ? 3 : g_cfg.lowCamLoose;
    long n = step - s.step; if (n < 1) n = 1;       // (a replay's fast-forward updates twice per index)
    float dt = n / 64.0f;
    // time constants in seconds, and how far the car's frame may trail (radians), at lowCamLoose=1
    static const float kTau[3] = { 0.16f, 0.14f, 0.24f }, kMax[3] = { 0.12f, 0.18f, 0.10f };
    for (int i = 0; i < 3; i++) {
        e[i] *= expf(-dt / (kTau[i] * k));
        float lim = kMax[i] * k;
        if (e[i] > lim) e[i] = lim; else if (e[i] < -lim) e[i] = -lim;
        e[i] = -e[i];
    }
    Quat q = QMul(QExp(e), qt);                     // the car's rotation, turned back by what's left of the lag
    float l = sqrtf(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (!(l > 1e-6f)) { s.valid = false; return; }
    q.w /= l; q.x /= l; q.y /= l; q.z /= l;
    float ms[9]; QuatToMat(q, ms);
    float W[9];                                     // world turn from the car's frame to the lagging one: ms^T cm
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) W[i*3+j] = ms[i]*cm[j] + ms[3+i]*cm[3+j] + ms[6+i]*cm[6+j];
    memcpy(s.rawM, m, 36); memcpy(s.rawP, p, 12);
    float d[3] = { p[0] - cp[0], p[1] - cp[1], p[2] - cp[2] }, np[3], nm[9];
    for (int i = 0; i < 3; i++) np[i] = cp[i] + W[i*3]*d[0] + W[i*3+1]*d[1] + W[i*3+2]*d[2];
    for (int r = 0; r < 3; r++)                     // the camera's rows (its axes) turned the same way: m W^T
        for (int j = 0; j < 3; j++) nm[r*3+j] = m[r*3]*W[j*3] + m[r*3+1]*W[j*3+1] + m[r*3+2]*W[j*3+2];
    float yo = (s.y - np[1]) * expf(-dt / (0.06f * k)), ylim = 0.10f * k;   // the height trails the car's a little
    if (yo > ylim) yo = ylim; else if (yo < -ylim) yo = -ylim;
    np[1] += yo;
    memcpy(m, nm, 36); memcpy(p, np, 12);
    memcpy(s.outM, nm, 36); memcpy(s.outP, np, 12);
    s.raw = true;
    s.q = q; s.y = np[1]; s.step = step;
}
// WsHorPlus: how much wider the low camera's main view is at this moment
// (a factor on the tangents of both half angles; 1 = the game's)
double LowCamFovFactor(int half) {
    // (not the mirror; not rain and snow's own 120 degree pass, half = 60)
    if (!(g_cfg.lowCamSpeedFov > 0) || Rd<int>(0x7A3D14) != 0 || half <= 0 || half >= 55) return 1.0;
    int v = Rd<int>(0x7A3D18) & 1;
    DWORD cam = EffectiveCam(v);
    if (!cam || !Readable(cam, 4) || !LooseType(Rd<int>(cam))) return 1.0;
    float add = FxAmount(2, v) * (g_cfg.lowCamSpeedFov > 40 ? 40 : g_cfg.lowCamSpeedFov);
    const double kDeg = 3.14159265358979323846 / 180.0;
    double h2 = half + add * 0.5;
    if (h2 > 85) h2 = 85;
    return tan(h2 * kDeg) / tan(half * kDeg);
}
extern "C" { DWORD g_fnLowCam = 0x4243A0; }
extern "C" void LowCamStub();
asm(
    // the low cameras' update (0x4264A8, eax = ecx = the camera): the game's
    // own last result back, the update, then the lag
    ".globl _LowCamStub\n"
    "_LowCamStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    pushl %ecx\n"
    "    call _LowCamBefore\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    popfl\n"
    "    call *_g_fnLowCam\n"
    "    pushfl\n"
    "    pushal\n"
    "    pushl %ecx\n"
    "    call _LowCamAfter\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    popfl\n"
    "    ret\n");
void InstallLowCam() {
    if (!g_knownExe) return;
    if (g_cfg.lowCamLoose > 0)
        Logf("low and roof cameras: loose x%.2f: %s\n", g_cfg.lowCamLoose,
             PatchRel32(0x4264A8, 0xE8, (void*)&LowCamStub) ? "on" : "patch FAILED");
    else Logf("low and roof cameras: the game's (rigid)\n");
    if (g_cfg.lowCamSpeedFov > 0)
        Logf("low and roof cameras: view up to %.1f degrees wider with speed%s\n", g_cfg.lowCamSpeedFov,
             g_cfg.widescreen && g_cfg.wsFixFOV ? "" : " - off, it needs widescreen and wsFixFOV");
}
