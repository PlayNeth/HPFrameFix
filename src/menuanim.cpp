#include "common.h"
#include "menuanim.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "timing.h"
#include "interp.h"
#include "nightlamps.h"

// ---------------------------------------------------------------------------
// Menus: the frame cap lifted, and the animations the game steps once a frame
// ---------------------------------------------------------------------------
void InstallMenuUnlock() {
    if (!g_knownExe || !g_cfg.unlockMenus) { Logf("menus: original 64 fps cap\n"); return; }
    bool ok = PatchRel32(hp::menuWaitAt, 0xE9, (void*)&MenuWaitStub);
    Logf("menus: %s\n", ok ? "unlocked" : "patch FAILED");
}

// ---- menu animations that the game steps once per frame --------------------
extern "C" { int g_logoN = 1, g_glowN = 1; }
extern "C" void LogoStepStub();
extern "C" void GlowPhaseStub();
extern "C" void GlowCountStub();

// steps an animation takes this frame: one per two ticks (64 a second)
struct TickSteps {
    DWORD last = 0;
    bool  have = false;
    int Take() {
        DWORD t = *(volatile DWORD*)0x79C1A4 >> 1;
        DWORD n = t - last;
        if (!have || n > 8) n = 1;       // first frame, or back in the menus after a while
        have = true; last = t;
        return (int)n;
    }
};
extern "C" void __cdecl LogoSteps() { static TickSteps s; g_logoN = s.Take(); }
// The menus' text fades (0x445AC0: [0x661088] up 10 a frame to 255, or down 15;
// 0x445F50: [0x661064] up 20 a frame) were made for the menus' 64 fps. Here
// each gets the 64ths of a second since its last frame instead, fractions
// carried over, so a fade takes the same time at any frame rate.
extern "C" int __cdecl FadeSteps(int id) {
    static LONGLONG last[3]; static double acc[3];
    if (id < 0 || id > 2) return 1;
    LONGLONG now = Qpc();
    double dt = last[id] ? (double)(now - last[id]) / (double)g_qpf.QuadPart : 1.0;
    last[id] = now;
    if (dt > 0.25 || dt < 0) { acc[id] = 0; return 1; }   // (the first frame of a fade)
    acc[id] += dt * 64.0;
    int n = (int)acc[id];
    acc[id] -= n;
    return n;
}
extern "C" void __cdecl GlowSteps() { static TickSteps s; g_glowN = s.Take(); }

__asm__(
    // the text fades: in place of "lea edi,[esi+0x14] / mov [0x661064],edi" (0x445F83),
    // "lea ebx,[ecx+0xA] / mov [0x661088],ebx" (0x445CC3) and "lea edi,[esi-0xF] /
    // mov [0x661088],edi" (0x445C99): the same per 64th of a second
    ".globl _FadeInStub\n"
    "_FadeInStub:\n"
    "    pushl %eax\n"
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl $0\n"
    "    call _FadeSteps\n"
    "    addl $4, %esp\n"
    "    imull $0x14, %eax, %edi\n"
    "    addl %esi, %edi\n"
    "    popl %edx\n"
    "    popl %ecx\n"
    "    popl %eax\n"
    "    movl %edi, 0x661064\n"
    "    ret\n"
    ".globl _TextFadeInStub\n"
    "_TextFadeInStub:\n"
    "    pushl %eax\n"
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl $1\n"
    "    call _FadeSteps\n"
    "    addl $4, %esp\n"
    "    imull $0xA, %eax, %ebx\n"
    "    popl %edx\n"
    "    popl %ecx\n"
    "    popl %eax\n"
    "    addl %ecx, %ebx\n"
    "    movl %ebx, 0x661088\n"
    "    ret\n"
    ".globl _TextFadeOutStub\n"
    "_TextFadeOutStub:\n"
    "    pushl %eax\n"
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl $2\n"
    "    call _FadeSteps\n"
    "    addl $4, %esp\n"
    "    imull $0xF, %eax, %eax\n"
    "    movl %esi, %edi\n"
    "    subl %eax, %edi\n"
    "    popl %edx\n"
    "    popl %ecx\n"
    "    popl %eax\n"
    "    movl %edi, 0x661088\n"
    "    ret\n"
    // in place of the step at 0x4A469E (eax = logo * 4): ecx = x + n*dx, ebx = y + n*dy, edx = dx
    ".globl _LogoStepStub\n"
    "_LogoStepStub:\n"
    "    testl %eax, %eax\n"            // first logo: a new frame
    "    jnz 1f\n"
    "    call _LogoSteps\n"
    "    xorl %eax, %eax\n"
    "1:  movl 0x7A1AE8(%eax), %edx\n"
    "    movl _g_logoN, %ecx\n"
    "    movl %ecx, %ebx\n"
    "    imull %edx, %ecx\n"
    "    addl 0x55E95C(%eax), %ecx\n"
    "    imull 0x7A1B10(%eax), %ebx\n"
    "    addl 0x7A1AC0(%eax), %ebx\n"
    "    ret\n"
    // in place of "mov dx, [esi+0x46] / add edx, 2" at 0x4A4C35, the first thing
    // 0x4A4C20 does after a call: the frame's step count, then phase + 2n
    ".globl _GlowPhaseStub\n"
    "_GlowPhaseStub:\n"
    "    pushal\n"
    "    call _GlowSteps\n"
    "    popal\n"
    "    movw 0x46(%esi), %dx\n"
    "    addl _g_glowN, %edx\n"
    "    addl _g_glowN, %edx\n"
    "    ret\n"
    // in place of the countdown at 0x4A4D1B: 0 -> 300, else minus n, not below 0
    ".globl _GlowCountStub\n"
    "_GlowCountStub:\n"
    "    movl 0x55EB28, %eax\n"
    "    testl %eax, %eax\n"
    "    jz 2f\n"
    "    subl _g_glowN, %eax\n"
    "    jns 1f\n"
    "    xorl %eax, %eax\n"
    "1:  movl %eax, 0x55EB28\n"
    "    ret\n"
    "2:  movl $300, 0x55EB28\n"
    "    ret\n"
);

extern "C" void FadeInStub(); extern "C" void TextFadeInStub(); extern "C" void TextFadeOutStub();
void InstallMenuAnimations() {
    if (!g_knownExe || !g_cfg.menuAnimations) { Logf("menu animations: one step per frame, as in the game\n"); return; }
    static const BYTE skipLogo[2]  = { 0xEB, 0x13 };     // jmp 0x4A46B8, past the rest of the old step
    static const BYTE nop2[2]      = { 0x90, 0x90 };
    static const BYTE skipCount[2] = { 0xEB, 0x17 };     // jmp 0x4A4D39, past the rest of the old countdown
    bool ok = PatchRel32(0x4A469E, 0xE8, (void*)&LogoStepStub)  && PatchBytes(0x4A46A3, skipLogo, 2) &&
              PatchRel32(0x4A4C35, 0xE8, (void*)&GlowPhaseStub) && PatchBytes(0x4A4C3A, nop2, 2) &&
              PatchRel32(0x4A4D1B, 0xE8, (void*)&GlowCountStub) && PatchBytes(0x4A4D20, skipCount, 2);
    static const BYTE nop4[4] = { 0x90, 0x90, 0x90, 0x90 };
    ok = ok && PatchRel32(0x445F83, 0xE8, (void*)&FadeInStub) && PatchBytes(0x445F88, nop4, 4) &&
         PatchRel32(0x445CC3, 0xE8, (void*)&TextFadeInStub) && PatchBytes(0x445CC8, nop4, 4) &&
         PatchRel32(0x445C99, 0xE8, (void*)&TextFadeOutStub) && PatchBytes(0x445C9E, nop4, 4);
    Logf("menu animations: %s\n", ok ? "background logos, background fade, spark, glow and text fades step 64 times a second"
                                     : "patch FAILED");
}
