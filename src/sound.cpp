#include "common.h"
#include "sound.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "timing.h"

// ---------------------------------------------------------------------------
// Sound service timing (diagnostics): the sound thread is woken on every 128 Hz
// tick to top up the DirectSound buffer. A late or slow top-up = a crackle.
// ---------------------------------------------------------------------------
extern "C" void SoundServeStub();
static LONGLONG g_sndLast = 0, g_sndStart = 0;
std::atomic<unsigned long> g_sndCalls{0};
std::atomic<long> g_sndMaxGapUs{0}, g_sndMaxRunUs{0}, g_sndLateGaps{0};
volatile LONG* g_eacLead = nullptr;     // eacsnd.dll's lead in samples (+0xA8CC) and rate (+0xA900), set by PatchEacsnd
volatile LONG* g_eacRate = nullptr;
static LONG g_eacLastLead = 0;
std::atomic<long> g_sndUnderruns{0};
// soundCapture (diagnostics): eacsnd.dll gets every block of sound from the
// game's mixer through a callback (the first of the 8 it's given, stored at
// +0xA904: stdcall (buffer, frames), called from its fill loop 0x2110). MixHook
// stands in for it, while a capture is on, and keeps a copy of what the mixer
// wrote.
typedef void (__stdcall *MixFn_t)(void*, int);
void** g_eacMixSlot = nullptr;             // eacsnd.dll +0xA904, set by PatchEacsnd
void** g_eacBuf = nullptr;                 // eacsnd.dll +0xA8A8: its output buffer
static MixFn_t g_mixOrig = nullptr;
// The copy goes into
// memory on the sound thread and from there to HPFrameFix_mix.wav on the log
// writer thread, within a second, so the sound thread never waits on the disk.
static BYTE* g_cap = nullptr;                     // capture memory (VirtualAlloc)
static DWORD g_capSize = 0;                       // its size in bytes
static std::atomic<DWORD> g_capLen{0};            // bytes filled so far
static DWORD g_capRate = 0; static int g_capCh = 0, g_capBits = 0;
static bool g_capOff = false;                     // full, failed, or the format changed
int CaptureSeconds() { return g_cfg.soundCapture < 0 ? (kTestBuild ? 120 : 0) : g_cfg.soundCapture > 600 ? 600 : g_cfg.soundCapture; }
static void CaptureBlock(const void* src, int frames, DWORD rate, int ch, int bits) {   // sound thread
    if (g_capOff) return;
    if (!g_cap) {
        int sec = CaptureSeconds();
        if (sec <= 0 || !rate) { g_capOff = true; return; }
        g_capSize = (DWORD)sec * rate * ch * (bits / 8);
        g_cap = (BYTE*)VirtualAlloc(nullptr, g_capSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_cap) { g_capOff = true; return; }
        g_capRate = rate; g_capCh = ch; g_capBits = bits;
        LogfAsync("sound capture: the first %d s of the game's mix go to HPFrameFix_mix.wav (%lu Hz, %d channel(s), %d bit)\n",
                  sec, (unsigned long)rate, ch, bits);
    }
    if (rate != g_capRate || ch != g_capCh || bits != g_capBits) { g_capOff = true; return; }
    DWORD n = (DWORD)frames * ch * (bits / 8), at = g_capLen.load();
    if (n > g_capSize - at) { n = g_capSize - at; g_capOff = true; }
    memcpy(g_cap + at, src, n);
    g_capLen = at + n;
}
void CaptureFlush() {                      // log writer thread only
    static FILE* f = nullptr; static DWORD done = 0; static bool failed = false;
    DWORD len = g_capLen.load();
    if (failed || !g_cap || len == done) return;
    if (!f) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%sHPFrameFix_mix.wav", g_asiDir);
        f = fopen(path, "wb");
        if (!f) { failed = true; return; }
        BYTE h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0 };
        WORD ch = (WORD)g_capCh, bits = (WORD)g_capBits, align = (WORD)(g_capCh * g_capBits / 8);
        DWORD rate = g_capRate, bps = rate * align;
        memcpy(h + 22, &ch, 2); memcpy(h + 24, &rate, 4); memcpy(h + 28, &bps, 4);
        memcpy(h + 32, &align, 2); memcpy(h + 34, &bits, 2); memcpy(h + 36, "data", 4);
        fwrite(h, 1, 44, f);
    }
    fseek(f, 44 + (long)done, SEEK_SET);
    fwrite(g_cap + done, 1, len - done, f);
    done = len;
    DWORD riff = 36 + len;
    fseek(f, 4, SEEK_SET);  fwrite(&riff, 4, 1, f);
    fseek(f, 40, SEEK_SET); fwrite(&len, 4, 1, f);
    fflush(f);
}
static void __stdcall MixHook(void* dst, int frames) {
    g_mixOrig(dst, frames);
    static void* fmtOf = nullptr; static int ch = 0, bits = 0;   // sound thread only
    static DWORD rate = 0;
    void* buf = g_eacBuf ? *g_eacBuf : nullptr;
    if (!buf || frames <= 0 || frames > 65536) return;
    if (buf != fmtOf) {                           // a new buffer: its format (IDirectSoundBuffer::GetFormat)
        BYTE wf[18] = {};
        typedef HRESULT (__stdcall *GetFmt_t)(void*, void*, DWORD, DWORD*);
        fmtOf = buf; ch = 0;
        if (((GetFmt_t)(*(void***)buf)[0x14 / 4])(buf, wf, sizeof wf, nullptr) >= 0) {
            ch = *(WORD*)(wf + 2); bits = *(WORD*)(wf + 14); rate = *(DWORD*)(wf + 4);
            if (ch < 1 || ch > 2 || (bits != 8 && bits != 16)) ch = 0;
        }
    }
    if (ch) CaptureBlock(dst, frames, rate, ch, bits);
}
extern "C" void __cdecl SoundServeBefore() {
    if (g_eacMixSlot && *g_eacMixSlot && *g_eacMixSlot != (void*)&MixHook) {   // (set, or set again, by the game)
        g_mixOrig = (MixFn_t)*g_eacMixSlot;
        *g_eacMixSlot = (void*)&MixHook;
    }
    LONGLONG now = Qpc();
    if (g_sndLast) {
        long gap = (long)((now - g_sndLast) * 1000000 / g_qpf.QuadPart);
        if (gap > g_sndMaxGapUs.load()) g_sndMaxGapUs = gap;
        if (gap > 15000) g_sndLateGaps++;
    }
    g_sndLast = now; g_sndStart = now;
}
extern "C" void __cdecl SoundServeAfter() {
    long run = (long)((Qpc() - g_sndStart) * 1000000 / g_qpf.QuadPart);
    if (run > g_sndMaxRunUs.load()) g_sndMaxRunUs = run;
    g_sndCalls++;
    if (g_eacLead) {                             // the DLL only grows its lead when the play cursor overtook the data: an underrun
        LONG l = *g_eacLead;
        if (g_eacLastLead && l > g_eacLastLead) g_sndUnderruns++;
        g_eacLastLead = l;
    }
}
void InstallSoundTiming() {
    if (!g_knownExe) return;
    DWORD imm = (DWORD)&SoundServeStub;
    BYTE b[5] = { 0xB8 }; memcpy(b + 1, &imm, 4);
    Logf("sound timing: %s\n", PatchBytes(0x4E232C, b, 5) ? "logged" : "patch FAILED");
}

asm(".text\n"
    // the sound thread's service call (0x4E2234: lock, iSNDdirectserve, unlock),
    // timed. Called by the thread loop 0x48EC40; registers kept as it does.
    ".globl _SoundServeStub\n"
    "_SoundServeStub:\n"
    "    pushal\n"
    "    call _SoundServeBefore\n"
    "    popal\n"
    "    movl $0x4E2234, %eax\n"
    "    call *%eax\n"
    "    pushal\n"
    "    call _SoundServeAfter\n"
    "    popal\n"
    "    ret\n");
