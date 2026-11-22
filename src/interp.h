// What the other parts use from interp.cpp.
#pragma once
#include "common.h"

extern std::atomic<unsigned long> g_clockSnaps;
// The cameras are static objects inside the exe's image (data and .bss,
// 0x536000..0xA19000), always mapped: a range check is enough there, and
// IsBadReadPtr (slow) is only needed for heap objects (cars).
static inline bool Readable(DWORD a, DWORD n) {
    if (a >= 0x536000 && a + n <= 0xA19000) return true;
    return !IsBadReadPtr((void*)a, n);
}
extern std::atomic<unsigned long> g_steps, g_resets;
extern HANDLE g_limTimer;
extern std::atomic<unsigned long> g_renders, g_extraRenders, g_blended, g_rejected, g_cuts;
extern volatile double g_alphaSum;
extern volatile unsigned long g_alphaN, g_alphaClamped;
extern volatile bool g_clampHave;
extern SYSTEMTIME g_clampFirst;
extern volatile double g_clampAheadMs, g_clampBehindMs;
extern std::atomic<unsigned long> g_menuFrames;
extern std::atomic<long> g_menuGapMaxUs;
extern "C" void MenuWaitStub();
void InstallInterpolation();
