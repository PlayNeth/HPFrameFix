// What the other parts use from trackscale.cpp.
#pragma once
#include "common.h"

extern float g_tsScale[10];
int TrackNum();
void TrackName(char* name, size_t size);
extern bool g_tsOn;
extern "C" void TsFrdStub();
void InstallTrackScale();
