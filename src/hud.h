// What the other parts use from hud.cpp.
#pragma once
#include "common.h"
#include "widescreen.h"
#include "menus.h"

extern "C" { extern int g_hudW; }
extern bool g_hudIn;
extern bool g_pzMeasIn;
extern "C" { extern float g_pzMeasF; }
extern "C" void __cdecl HudUpdate();
bool InstallHud();
void InstallSoftStandingsBox();
