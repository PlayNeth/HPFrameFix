// What the other parts use from threads.cpp.
#pragma once
#include "common.h"
#include "log.h"

extern volatile DWORD_PTR g_gameCore;
void AffinityWatch();
bool HookCreateThreadIn(HMODULE mod, const char* what);
void InstallGameThreadsOneCore();
