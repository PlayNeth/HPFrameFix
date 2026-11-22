// What the other parts use from sound.cpp.
#pragma once
#include "common.h"
#include "log.h"

extern std::atomic<unsigned long> g_sndCalls;
extern std::atomic<long> g_sndMaxGapUs, g_sndMaxRunUs, g_sndLateGaps;
extern volatile LONG* g_eacLead;
extern volatile LONG* g_eacRate;
extern std::atomic<long> g_sndUnderruns;
extern void** g_eacMixSlot;
extern void** g_eacBuf;
int CaptureSeconds();
void CaptureFlush();
void InstallSoundTiming();
