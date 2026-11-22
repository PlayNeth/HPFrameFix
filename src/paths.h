// What the other parts use from paths.cpp.
#pragma once
#include "common.h"
#include "game.h"
#include "widescreen.h"

static const char* const kFullInstall[] = {
    ".\\GameData\\", ".\\GameData\\Tracks\\", ".\\GameData\\Tracks\\Tutor\\",
    ".\\GameData\\CarModel\\", ".\\GameData\\Render\\pc\\", ".\\GameData\\DashHud\\",
    ".\\GameData\\Audio\\pc\\", ".\\GameData\\Audio\\SFX\\",
    ".\\GameData\\Audio\\Speech\\English\\", ".\\GameData\\Audio\\Speech\\German\\",
    ".\\GameData\\Audio\\Speech\\French\\", ".\\GameData\\Audio\\Speech\\Spanish\\",
    ".\\GameData\\Audio\\Speech\\Italian\\",
    ".\\FeData\\art\\", ".\\FeData\\text\\", ".\\FeData\\text\\", ".\\FeData\\save\\",
    ".\\FeData\\stats\\", ".\\FeData\\config\\", ".\\FeData\\audio\\",
    ".\\FeData\\Art\\Slides\\", ".\\FeData\\Art\\Track\\", ".\\FeData\\Art\\Showcase\\",
    ".\\FeData\\movies\\", ".\\FeData\\stats\\prh\\",
};
static_assert(2 + sizeof(kFullInstall) / sizeof(kFullInstall[0]) <= (size_t)hp::installLines, "table size");
void InstallInstallPaths();
