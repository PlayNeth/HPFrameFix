// What the other parts use from widescreen.cpp.
#pragma once
#include "common.h"
#include "paths.h"

extern "C" void WsProjStub();
extern "C" void WsZoomStub();
extern "C" void HorizonRowStub();
extern "C" void HorizonStub0();
extern "C" void HorizonStub1();
extern "C" void HorizonStub2();
extern "C" void XfWrapCockpit();
extern "C" void XfNeedlesStub();
extern "C" void XfWrapLoadScreen();
extern "C" void ModeFilterStub();
extern "C" { extern int g_wsMirror, g_mirrorLayout; }
extern bool g_mirrorNarrowed;
typedef void (WINAPI *TDraw4_t)(void*, void*, void*, void*);
typedef void (WINAPI *TDraw3_t)(void*, void*, void*);
typedef void (WINAPI *TDrawN_t)(int, void*);
extern bool g_xfCockpit;
static const int kVtx = 32;
static_assert(kVtx == 32, "TVERTEX");
extern bool g_softRenderer;
int ModeRank(const int* m);
