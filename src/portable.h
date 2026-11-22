// What the other parts use from portable.cpp.
#pragma once
#include "common.h"

// The game matches "Thrash Driver" against its table of renderer DLLs at
// [0x55FD68] (case-sensitive, as a prefix) and quits with "files are corrupted"
// if nothing matches; it then loads the table's name, not the value.
static const char* const kThrashDlls[] = { "nulla.dll", "softtria.dll", "softlina.dll", "voodooa.dll",
                                           "voodoo2a.dll", "ragea.dll", "d3da.dll", "powervra.dll" };
void InstallPortableRegistry();
