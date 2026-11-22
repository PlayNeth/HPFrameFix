// What the other parts use from nightlamps.cpp.
#pragma once
#include "common.h"

enum { kNlWindow, kNlFitting, kNlRoad, kNlBlue, kNlYellow, kNlPole, kNlHead, kNlKinds };
static const struct { float join, reach, k; } kNlKind[kNlKinds] = {     // metres at the game's track size; strength
    { 5, 0, 1 }, { 6, 14, 1 }, { 18, 20, 1 }, { 8, 16, 1 }, { 6, 14, 1 }, { 6, 28, 3 }, { 6, 6, 1.6f },
};                                                                      // (a window's reach: the track's)
void InstallNightLamps();
