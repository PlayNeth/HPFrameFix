// What the other parts use from fog.cpp.
#pragma once
#include "common.h"
#include "widescreen.h"

typedef int (WINAPI *TSetState_t)(int, int);
extern TSetState_t g_mnSetState;
void FogPause(bool pause);
void InstallD3dFog();
