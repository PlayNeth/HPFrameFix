// What every part of HPFrameFix starts from: the system headers and the version.
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define HPFF_VERSION "0.1.8"
// Test builds (a version with "test" in it) record the first two minutes of the
// game's mix to HPFrameFix_mix.wav unless the ini says otherwise, and write a
// step-by-step trace around every look back press and release to the log.
static constexpr bool HasTest(const char* v) {
    for (; *v; v++) if (v[0] == 't' && v[1] == 'e' && v[2] == 's' && v[3] == 't') return true;
    return false;
}
static constexpr bool kTestBuild = HasTest(HPFF_VERSION);
