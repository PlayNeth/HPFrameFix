// What the other parts use from menus.cpp.
#pragma once
#include "common.h"
#include "interp.h"
#include "fog.h"
#include "hud.h"

int WINAPI MnSetState(int s, int v);
extern "C" void __cdecl PzLimitsCheck();
extern "C" { extern float g_pzMeasF; }
void InstallWidescreen();
