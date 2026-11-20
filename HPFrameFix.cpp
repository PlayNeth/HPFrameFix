// HPFrameFix.asi
// Frame-rate unlock with interpolation and portable mode for Need for Speed III:
// Hot Pursuit (PC). Same engine as High Stakes; ported from HSFrameFix.
//
// Supported exe: nfs3.exe, SHA-1 87872783b8d09a27ae5a1b2a7de4f69b7fbb4a39 (PE
// timestamp 0x35EEDACE, image base 0x400000). The Banshee patch (nfs3ptch.exe)
// only replaces voodoo2a.dll, so it doesn't change the exe. Every patch site is
// byte-checked first; on any other exe the ASI does nothing (exeCheck=0 patches
// anyway; every differing byte range is logged, expected vs found).
//
// How the game keeps time (main loop at 0x4C4E40):
//  - A 128 Hz multimedia-timer chain (callback 0x48ED30, TimerInit(128) at
//    0x48EFF0, period in [0x79C170]) wakes a handler thread; the game's handler
//    0x42DC60 counts 128/64/32 Hz ([0x5EFDF4]/[0x5EFDF8]/[0x5EFDFC]).
//  - The simulation (0x4C48D0, step index in eax) steps at 64 Hz with a fixed
//    timestep. The loop only renders (0x4BC680) after a new sim step, so the
//    frame rate is capped at 64.
//
// What this ASI does:
//  - preciseTicks: drives the 128 Hz chain from a QPC-based scheduler so tick
//    times are exact (the original uses 1 ms GetTickCount one-shots).
//  - interpolate: after every sim step it snapshots the transforms of all cars
//    (pos car+0x98, rotation car+0xC0, list [0x5EFAC8], count [0x5EFD9C]) and
//    all camera objects (pos cam+0x14, rotation cam+0x2C, table 0x552EC8).
//    Before each render it blends them to "now - one 64 Hz step" (rotations by
//    quaternion slerp), renders, then restores the exact simulation values, so
//    the simulation never sees interpolated data. The car draw (0x42D160) reads
//    car+0x98 and copies car+0xC0 at draw time, so the blended values are used.
//  - The main loop's "nothing new, skip render" branch (0x4C546E) renders an
//    interpolated frame instead, so the frame rate follows the display (vsync)
//    or the optional limiter instead of the 64 Hz sim.
//  - Camera cuts (switching camera, look back, jumps too large to blend) draw
//    the frame at the newest step. NFS III has no look-mode flag like High
//    Stakes: looking back turns the camera 180 degrees within one step, which
//    the maxStepAngle check already treats as a cut.
//  - Menus: every front-end screen presents through 0x4D9140: flush, page flip,
//    then it spins until the 128 Hz tick counter [0x79C1A4] has advanced by 2
//    (= 64 fps cap), calling the idle work 0x4C9860 meanwhile. unlockMenus
//    replaces that wait with one idle call per frame (plus the optional
//    limiter). menuMaxFPS keeps menus at or below 128 fps (auto: a divisor of
//    the refresh rate), since menu animations are counted in 128 Hz ticks.
//  - menuAnimations: the logos drifting behind the menus (0x4A4410, ten of
//    them, each drawn with a trail of twelve copies) take one step per frame:
//    at 0x4A4697 x [0x55E95C+i*4] += [0x7A1AE8+i*4] and y [0x7A1AC0+i*4] +=
//    [0x7A1B10+i*4]. The step is taken once per two ticks instead (64 a
//    second, the speed under the game's own cap) whatever the frame rate.
//    The same for the menu frame's background (0x4A4C20): the phase of the
//    cross-fade between its pictures (word [obj+0x46] += 2 at 0x4A4C39) and
//    the countdown [0x55EB28] (300 to 0, minus 1 at 0x4A4D24) that times the
//    spark running along the arc and the glow around the ball ("glw0").
//  - Loading: the loading bar (0x495230) redraws and flips on every call.
//    speedUpLoading only redraws it every loadScreenRenderMs; the skipped calls
//    still run its callback poll (0x4E7630).
//  - portableRegistry: the game's registry values (renderer choice under
//    HKLM\SOFTWARE\Electronic Arts\Need For Speed III, network DLL locations
//    under HKLM\SOFTWARE\EA-NPS\DLL) are answered by the mod: the renderer and
//    the display adapter from HPFrameFix.ini (renderer=, d3dDevice=), anything
//    else from the Windows registry if it has it. The game runs with none of
//    them set. (Up to 0.1.0 they lived in a file of their own,
//    HPFrameFix_registry.ini; one that's still there is still read.) The game
//    only reads the registry, so nothing is written back.
//  - portableInstallPaths: install.win (game folder) lists the data folders;
//    a partial install points some at the CD (E:\FeData\movies\ ...). After the
//    game reads it (0x4A6E10, lines in [0x7A2634], 128 bytes each), every
//    "X:\folder" line whose folder exists in the game folder becomes
//    ".\folder". install.win itself is not changed.
//  - installWinFallback: without install.win the reader returns 0 and the game
//    quits with "files are corrupted". The fallback then fills the line table
//    with a full install (every folder relative to the game folder), sets the
//    language index [0x7A3A50] as the reader would, and returns 1.
//  - fixMovieFreeze: the movie streamer (cmn\cdstream.c) stops its thread by
//    spinning until the thread clears its "running" flag, its last act before
//    returning, then TerminateThread. With several cores the kill can land while
//    the thread is exiting and holds the loader lock, which is then never
//    released: no thread can start or end any more and the game freezes. The
//    kill call (0x4E4B30) first waits up to 2 s for the thread to end; the
//    original kill then finds it gone. KillThread's other caller (shutdown,
//    0x4B6274) is left alone.
//  - gameThreadsOneCore: the game's sound code shares data between its threads
//    with no locking that holds up on several cores (it crackles and pops). As
//    in SilentPatch's SingleProcAffinity for NFS3, the main thread and every
//    thread nfs3.exe / eacsnd.dll create (CreateThread import) run on the first
//    core; the system's threads (DirectSound's mixer) and this mod's keep all.
//    widenAffinity: a process that is on one core when the mod loads gets all
//    cores back, and so does one that is pinned later (a wrapper's single-core
//    option, checked once a second by AffinityWatch): the game's threads then
//    move to the core the other one picked, the rest are free again.
//  - fixSound: eacsnd.dll (sound driver, loaded at 0x4E1DF1) wears down how far
//    ahead of playback it writes until it underruns and pops. As the DLL is
//    loaded that's patched out and the lead set to soundLatencyMs (PatchEacsnd).
//  - noCDCheck: at startup (0x4B6320) a "local" install must have the CD in a
//    drive (X:NFS3.EXE on a CD-ROM drive, else "insert the CD" and quit). The
//    check's call at 0x4B6394 becomes "mov eax,1".
//  - renderer: the game reads the "Thrash Driver" registry value (0x4B6400;
//    software if missing), matches it against its renderer table [0x55FD68]
//    (0x4B5720: case-sensitive prefix, no match = "files are corrupted") and
//    loads that table entry. renderer= answers that query, with or without
//    portableRegistry; values are given in the table's spelling.
//  - driversFolder: LoadLibraryA of a renderer DLL (the game's 8 names)
//    without a path loads <game folder>\<driversFolder>\<dll>
//    if it's there (dependencies such as glide2x.dll are looked up there first
//    too), else the game folder as before.
//  - noDiskSpaceCheck: the free-space test (0x4516A0) overflows on big disks
//    and always answers "enough" instead.
//  - widescreen: see the Widescreen section further down (view, HUD, cockpit,
//    loading screen), the menus' section (menus at the chosen resolution,
//    wsMenuFilter, menuTexelInset, alternatePixelCenter) and the in-race
//    menus' section (wsPauseMenu). wsWeather keeps rain and snow in their own
//    width of view; softStandingsBox and wsMenusSoft are for the software
//    renderer.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define HPFF_VERSION "0.1.7-plus"
// Test builds (a version with "test" in it) record the first two minutes of the
// game's mix to HPFrameFix_mix.wav unless the ini says otherwise, and write a
// step-by-step trace around every look back press and release to the log.
static constexpr bool HasTest(const char* v) {
    for (; *v; v++) if (v[0] == 't' && v[1] == 'e' && v[2] == 's' && v[3] == 't') return true;
    return false;
}
static constexpr bool kTestBuild = HasTest(HPFF_VERSION);

// ---------------------------------------------------------------------------
// Game addresses (nfs3.exe)
// ---------------------------------------------------------------------------
namespace hp {
    // timer
    const DWORD fnTimerCallback = 0x48ED30;   // TIMECALLBACK (stdcall, ret 0x14)
    const DWORD pTimerPeriod    = 0x79C170;   // ms per tick, 16.16 fixed point
    // main loop
    const DWORD fnSimStep       = 0x4C48D0;   // eax = step index
    const DWORD callSimStep[4]  = { 0x4C5358, 0x4C536C, 0x4C5376, 0x4C53E1 };
    const DWORD fnRender        = 0x4BC680;   // eax = render argument
    const DWORD callRender      = 0x4C5489;
    const DWORD skipRenderAt    = 0x4C546E;   // cmp [0x7D3674],0 / jne 0x4C548E (9 bytes)
    const DWORD skipRenderCont  = 0x4C5477;   // normal render decision (test edi,edi)
    const DWORD skipRenderOut   = 0x4C548E;   // after render
    const DWORD pSimStepIdx     = 0x7D36B0;   // index of the next step (incremented after it runs)
    const DWORD pNoRender       = 0x7D3674;   // 1 = no new step this pass
    const DWORD pRenderArg      = 0x7D36A0;
    // objects
    const DWORD pCarCount       = 0x5EFD9C;
    const DWORD pCarList        = 0x5EFAC8;   // Car*[16]
    const DWORD carPos          = 0x98;       // float[3]
    const DWORD carRot          = 0xC0;       // float[9]
    const DWORD pCamTable       = 0x552EC8;   // Camera*[24 types][2 views]
    const int   camTableSlots   = 48;
    const DWORD pCamActive      = 0x5E10B0;   // Camera*[2]
    const DWORD pCamOverride    = 0x5E10B8;   // Camera*[2]
    const DWORD camPos          = 0x14;       // float[3]
    const DWORD camRot          = 0x2C;       // float[9]
    const DWORD camCar          = 0x04;       // Car* the camera follows
    const DWORD pLookBack       = 0x79C4A0;   // int[2], per view: look back held. Set once per main loop pass
                                              // (0x4C5139 / 0x4C5153), read by the camera update and by the render
    // front end
    const DWORD menuWaitAt      = 0x4D9151;   // mov eax,[0x79C1A4] - start of the 2-tick wait
    const DWORD menuWaitDone    = 0x4D9172;   // mov [0x56415C], edx - after the wait
    // loading bar
    const DWORD fnLoadBar       = 0x495230;   // void LoadBar(float percent), ret 4
    // install.win
    const DWORD callReadInstall = 0x4A3D4A;   // call 0x4A6E10
    const DWORD fnReadInstall   = 0x4A6E10;
    const DWORD pInstallLines   = 0x7A2634;   // char[40][128]
    const int   installLineLen  = 128, installLines = 40;
    const DWORD pLanguageIndex  = 0x7A3A50;   // index into the 6 names at [0x55FD24]
    // startup check 0x4B6320: "call 0x4B62A0; test eax,eax; jne done", then a
    // search of every CD-ROM drive for X:NFS3.EXE ("insert the CD" + quit if none)
    const DWORD cdCheckCall     = 0x4B6394;
    // movies: cmn\cdstream.c delstreamstruct (0x4E4A30) spins until the stream
    // thread clears its running flag [s+0x28], then kills it: 0x4E4B30
    // call 0x4DF870 (KillThread: TerminateThread + CloseHandle, eax = &handle)
    const DWORD streamKillCall  = 0x4E4B30;
    const DWORD fnKillThread    = 0x4DF870;

    struct Sig { DWORD addr; const char* bytes; int len; };
    const Sig kSigs[] = {   // generated from nfs3.exe
        { 0x48ED30, "\x53\x56\x57\x55\x89\xE5\xA1\x70\xC1\x79\x00\x8B", 12 },  // timer callback
        { 0x48F028, "\xA3\x70\xC1\x79\x00", 5 },  // timer period store
        { 0x42DCCB, "\xB8\x60\xDC\x42\x00", 5 },  // register tick handler
        { 0x4C4E4B, "\xA1\xF8\xFD\x5E\x00", 5 },  // 64 Hz counter in main loop
        { 0x4C48D0, "\x53\x51\x52\x56\x57\x55\x89\xE5", 8 },  // sim step
        { 0x4C5353, "\xA1\xB0\x36\x7D\x00\xE8\x73\xF5\xFF\xFF", 10 },  // sim step call 1
        { 0x4C5367, "\xA1\xB0\x36\x7D\x00\xE8\x5F\xF5\xFF\xFF\xA1\xB0\x36\x7D\x00\xE8\x55"
                    "\xF5\xFF\xFF", 20 },  // sim step calls 2+3
        { 0x4C53DC, "\xA1\xB0\x36\x7D\x00\xE8\xEA\xF4\xFF\xFF", 10 },  // sim step call 4
        { 0x4C52C0, "\x8B\x1D\xAC\x36\x7D\x00", 6 },  // sim target
        { 0x4C52E1, "\xF6\x05\xB0\x36\x7D\x00\x01", 7 },  // step index
        { 0x4BC680, "\x53\x51\x52\x56\x55\x89\xE5\x89", 8 },  // render
        { 0x4C546E, "\x83\x3D\x74\x36\x7D\x00\x00\x75\x17\x85\xFF\x74\x09\xE8\x10\xF0\xFF"
                    "\xFF\x31\xFF\xEB\x0A\xA1\xA0\x36\x7D\x00\xE8\xF2\x71\xFF\xFF", 32 },  // skip-render branch, render call
        { 0x4C548E, "\x83\x3D\xB0\xD3\x6F\x00\x04", 7 },  // after render
        { 0x41BA69, "\x3B\x15\x9C\xFD\x5E\x00\x7D\x23\x8B\x04\x95\xC8\xFA\x5E\x00\x8A", 16 },  // car count / car list
        { 0x42D195, "\x8B\x80\x98\x00\x00\x00", 6 },  // car+0x98 position
        { 0x42D231, "\xB9\x09\x00\x00\x00\x8D\xB8\x64\x06\x00\x00\x8D\xB0\xC0\x00\x00\x00", 17 },  // car+0xC0 rotation
        { 0x423946, "\x8B\x94\xC8\xC8\x2E\x55\x00\x89\x90\xB0\x10\x5E\x00", 13 },  // camera table, active camera
        { 0x41190A, "\x8B\x90\xB8\x10\x5E\x00", 6 },  // camera override
        { 0x422541, "\x8D\x70\x14", 3 },  // cam+0x14 position
        { 0x4225EF, "\x8D\x70\x2C", 3 },  // cam+0x2C rotation
        { 0x4C5139, "\xA3\xA0\xC4\x79\x00", 5 },  // look back flag: mov [0x79C4A0], eax
        { 0x4D9140, "\x51\x52\x55\x89\xE5\xFF\x15\x50\xF9\x9E\x00\xFF\x15\x40\xF9\x9E\x00"
                    "\xA1\xA4\xC1\x79\x00\x89\xC2\xA1\x5C\x41\x56\x00\x83\xC0\x02\x39\xD0"
                    "\x7E\x0E\xA1\xA4\xC1\x79\x00\x89\xC2\xE8\xF0\x06\xFF\xFF\xEB\xE6\x89"
                    "\x15\x5C\x41\x56\x00\x5D\x5A\x59\xC3", 60 },  // menu present + 2-tick wait
        { 0x4C9860, "\x55\x89\xE5\x31\xC0\xE8\xC6\xDD\x01\x00\xE8\x11\xFB\xFF\xFF\x5D\xC3", 17 },  // menu idle
        { 0x495230, "\x53\x51\x52\x56\x57\x55\x89\xE5\x83\xEC\x0C\x31\xC0\xE8", 14 },  // loading bar
        { 0x4955F8, "\xC2\x04\x00", 3 },  // loading bar ret 4
        { 0x4A3D4A, "\xE8\xC1\x30\x00\x00", 5 },  // install.win reader call
        { 0x4A6E10, "\x53\x51\x52\x56\x57\x55\x89\xE5", 8 },  // install.win reader
        { 0x4A6ED1, "\xBF\x34\x26\x7A\x00", 5 },  // install.win line table
        { 0x4B6394, "\xE8\x07\xFF\xFF\xFF", 5 },  // CD check: call 0x4B62A0
        { 0x4E4B30, "\xE8\x3B\xAD\xFF\xFF", 5 },  // stream thread kill: call 0x4DF870
        { 0x4E232C, "\xB8\x34\x22\x4E\x00", 5 },  // sound service fn handed to the sound thread
        { 0x4CD7FE, "\x68\x40\xCE\x4C\x00\x6A\x10", 7 },  // push WM_CLOSE handler, push 0x10
        { 0x4CD83A, "\x68\x90\xCE\x4C\x00\x68\x12\x01\x00\x00", 10 },  // WM_SYSCOMMAND handler
        { 0x4B9FB9, "\x8B\x15\x1C\xBC\x6F\x00\xC1\xE2\x04", 9 },  // LOD table index (cardetail*16)
        { 0x4BBAE0, "\xBE\x50\x7A\x4B\x00", 5 },  // detail budget table 0x4B7A50
        { 0x55FDF4, "\x00\x00\xF0\x41\x00\x00\x70\x42\x00\x00\xF0\x42\x00\x00\x96\x43", 16 },  // LOD 30/60/120/300
        { 0x4B7A50, "\x0A\x00\x00\x00\x08\x00\x00\x00\x06\x00\x00\x00", 12 },  // budget 10/8/6
        { 0x4BEDF8, "\x0F\x85\x65\x00\x00\x00", 6 },    // mode list: w*3 != h*4 -> skip
        { 0x4BEDDD, "\x83\xFA\x02\x0F\x8C\x7D\x00\x00\x00", 9 },  // mode list: first test
        { 0x482CA3, "\xE8\x98\x65\x00\x00", 5 },        // HUD: call cockpit picture 0x489240
        { 0x4A3468, "\xE8\x33\x1B\xFF\xFF", 5 },        // race load: call loading screen 0x494FA0
        { 0x4B5D51, "\x89\x35\x64\x3A\x7A\x00", 6 },  // d3da setup: fog mode [0x7A3A64] = 0
        { 0x4894EC, "\xE8\xBF\xEB\xFF\xFF", 5 },        // cockpit: call needles 0x4880B0
        { 0x4BEF50, "\x53\x51\x52\x56\x57\x55", 6 },  // SetVideoMode: push ebx..ebp
        { 0x4BE8C4, "\xE8\x07\xFB\xFF\xFF", 5 },        // view rects 0x4BE5E0: its last call (0x4BE3D0)
        { 0x4DBE0B, "\xE8\x10\x2F\xFE\xFF", 5 },        // ViewSetup, mirror: call 0x4BED20 (projection for a rect)
        { 0x480910, "\x56\x57\x55\x89\xE5", 5 },        // HUD box placer: push esi/edi/ebp; mov ebp, esp
        { 0x4847B5, "\xF6\x05\x58\x3A\x7A\x00\x02\x0F\x85\x7F\x01\x00\x00", 13 },  // HUD, standings panel: software renderer -> skip fill and frame
        { 0x4847E5, "\x89\xF0\x8B\x15\xD8\x1A\x79\x00", 8 },  // ... its frame: mov eax, esi; mov edx, [0x791AD8]
        { 0x4BC7AB, "\xE8\x20\xF6\xF8\xFF", 5 },        // RenderFrame: call the in-race menu's draw 0x44BDD0
        { 0x44A5DC, "\xFF\x53\x2C\x85\xC0", 5 },        // menu draw loop: call the item's draw [ebx+0x2C]; test eax, eax
        { 0x44BE03, "\xE8\x58\x8D\xFE\xFF", 5 },        // in-race menu: call the pointer's draw 0x434B60
        { 0x459EBD, "\x8D\xBC", 2 }, { 0x459EC0, "\x74\xBC\x6F\x00", 4 },   // HUD positions: lea edi, [ecx+eax+0x6FBC74]
        { 0x452055, "\xDB\x05\xC8\xDA\x7C\x00", 6 },                     // in-race menus, string width to 640ths: fild [0x7CDAC8]
        { 0x451D37, "\xE8\x44\xF7\x07\x00", 5 },                         // in-race menus, the other measure: call 0x4D1480
        { 0x4D06F0, "\x51\x56\x57\x55\x89\xE5\x83\xEC\x5C", 9 },         // a letter's quad: push ecx/esi/edi/ebp; mov ebp, esp; sub esp, 0x5C
        { 0x4D1340, "\x52\x55\x89\xE5\xF6\x05\x58\x3A\x7A\x00\x02", 11 },  // text flush: push edx/ebp; mov ebp, esp; test [0x7A3A58], 2
        { 0x4D724E, "\x6A\x00\x6A\x07\xFF\x15\x6C\xF9\x9E\x00", 10 },  // big menu pictures: setstate(7, 0) = texture filter off
        { 0x4A3422, "\xE8\x29\xBB\x01\x00", 5 },        // race: call SetVideoMode
        { 0x4A35E8, "\xE8\x63\xB9\x01\x00", 5 },        // front end: call SetVideoMode
        { 0x4B60FD, "\xE8\x4E\x8E\x00\x00", 5 },        // startup: call SetVideoMode
        { 0x44B1E5, "\xE8\x66\x3D\x07\x00", 5 },        // menus after a reset: call SetVideoMode (mode 1)
        { 0x4B610C, "\xE8\x6F\x8D\x00\x00", 5 },        // startup: saved width -> mode (0x4BEE80)
        { 0x472ECD, "\xE8\xAE\xBF\x04\x00", 5 },        // default settings: width -> mode
        { 0x47327E, "\xE8\xFD\xBB\x04\x00", 5 },        // settings loaded: saved width -> mode
        { 0x4482F0, "\x51\x52\x55\x89\xE5\xE8", 6 },  // GetSelectedMode
        { 0x48AB47, "\xD9\x05\xB8\xD6\x55\x00", 6 },  // HUD: fld [screen width float]
        { 0x48ABFA, "\xD9\x05\xB8\xD6\x55\x00", 6 },
        { 0x484B7A, "\xA1\xC8\xDA\x7C\x00", 5 },        // HUD: mov eax, [0x7CDAC8]
        { 0x49CEB9, "\x81\xFB\x00\x01\x00\x00", 6 },    // texture size: cmp ebx, 256
        { 0x41DB7B, "\xBB\x80\x00\x00\x00", 5 },        // track textures: limit 128
        { 0x4B7D54, "\xBB\x00\x01\x00\x00", 5 },        // player car textures: limit 256
        { 0x4B7D5B, "\xBB\x80\x00\x00\x00", 5 },        // other cars' textures: limit 128
        { 0x4DBD2B, "\x8B\x55\xE8\x89\xF0\x89\x75\xF8\xE8\xB8\x16\x02\x00", 13 },  // frustum call
        { 0x4DBD3F, "\xDB\x45\xF8", 3 },                 // fild [ebp-8]: horizontal half FOV
        { 0x4DBD48, "\xDB\x45\xF8", 3 },                 // fild [ebp-8]: vertical half FOV
        { 0x47A90E, "\xE8\x6D\xED\xFF\xFF", 5 },        // horizon: call 0x479680
        { 0x47A92D, "\xE8\x9E\xF0\xFF\xFF", 5 },        // horizon: call 0x4799D0
        { 0x47A93D, "\xE8\x4E\xF2\xFF\xFF", 5 },        // horizon: call 0x479B90
        { 0x4796FF, "\xE8\x6C\xFC\xFF\xFF", 5 },        // horizon rows: call 0x479370 (9 points each)
        { 0x47971D, "\xE8\x4E\xFC\xFF\xFF", 5 },
        { 0x479A51, "\xE8\x1A\xF9\xFF\xFF", 5 },
        { 0x479A6F, "\xE8\xFC\xF8\xFF\xFF", 5 },
        { 0x479D33, "\xE8\x38\xF6\xFF\xFF", 5 },
        { 0x479D50, "\xE8\x1B\xF6\xFF\xFF", 5 },
        { 0x479D6D, "\xE8\xFE\xF5\xFF\xFF", 5 },
        { 0x4DBE8F, "\xD9\x05\xAC\x00\x56\x00\xD8\x48\x54", 9 },  // ViewSetup, a zoomed camera: x focal = half width * zoom
        { 0x4DBEA4, "\xD9\x1D\x9C\x00\x56\x00", 6 },
        { 0x4A469E, "\x8B\x90\xE8\x1A\x7A\x00\x8B\x88\x5C\xE9\x55\x00\x8B\x98\xC0\x1A\x7A\x00", 18 },  // menu logos: step
        { 0x4A4C35, "\x66\x8B\x56\x46\x83\xC2\x02", 7 },                // menu background: fade phase += 2
        { 0x445F83, "\x8D\x7E\x14\x89\x3D\x64\x10\x66\x00", 9 },  // text fade in: +20 a frame
        { 0x445CC3, "\x8D\x59\x0A\x89\x1D\x88\x10\x66\x00", 9 },  // text fade in: +10 a frame
        { 0x445C99, "\x8D\x7E\xF1\x89\x3D\x88\x10\x66\x00", 9 },  // text fade out: -15 a frame
        { 0x4A4D1B, "\xA1\x28\xEB\x55\x00\x85\xC0\x74\x0B\x8D\x50\xFF", 12 },  // menu spark: countdown
        { 0x4516A0, "\x51\x52\x55", 3 },                 // free disk space test: push ecx / push edx / push ebp
        { 0x4DF87E, "\x6A\x00\x53\x2E\xFF\x15\x08\x46\x53\x00", 10 },  // TerminateThread(h, 0)
        { 0x4A25A3, "\xD9\x05\xB4\xC8\x53\x00", 6 },  // car physics: fld [0x53C8B4] (air drag factor)
        { 0x53C8B4, "\x00\x00\x00\x3D", 4 },        // 0.03125
    };
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
struct Config {
    int   interpolate      = 1;
    int   preciseTicks     = 1;
    int   targetFPS        = -1;    // -1 = auto (vsync, else the refresh rate), 0 = no limiter, N = N fps
    float maxStepDistance  = 10.0f; // world units per sim step; larger jumps are not blended
    float maxStepAngle     = 90.0f; // degrees per sim step
    float interpDelay      = 1.0f;  // how far behind the newest step frames are shown (steps)
    int   unlockMenus      = 1;
    int   menuTargetFPS    = -1;    // -1 = same as targetFPS, 0 = off
    int   menuMaxFPS       = -1;    // -1 = auto (<= 128, divides the refresh rate), 0 = none
    int   menuAnimations   = 1;     // per-frame menu animations keep their 64 fps speed
    int   speedUpLoading   = 1;
    float carLodScale      = 2.0f;  // car model LOD switch distances x this (1 = the game's)
    int   carDrawDistance  = 0;     // m; cars further away aren't drawn (0 = the game's 300)
    int   fullDetailCars   = 1;     // no car is dropped to a lower detail model/texture set
    float airDrag          = 0.0f;  // air drag on cars in the air, x the game's (1 = the game's, 0 = none)
    int   maxTextureSize   = 512;   // largest texture loaded (the game's 256; more with Direct3D only)
    int   closeExits       = 1;     // closing the window (Alt+F4, window manager) ends the game
    int   widescreen       = 1;     // master switch for the ws* keys
    int   wsFixFOV         = 1;     // Hor+ 3D view
    int   wsUnlockResolutions = 1;  // list non-4:3 display modes (and those the game thought too big)
    int   ws32bit          = 1;     // list 32-bit modes too (when the renderer offers them), preferred
    int   wsCockpit        = 1;     // cockpit picture: 1 = zoomed to the width, centred (no stretch), 0 = stretched
    int   wsLoading        = 1;     // loading screen: 1 = 4:3 in the middle, 0 = stretched
    int   alternatePixelCenter = 1; // Direct3D: every vertex half a pixel up and left (pixel centres between whole coordinates)
    int   menuTexelInset   = 1;     // Direct3D, in-race menus above 640x480: pictures sampled half a texel in from their edges
    int   softStandingsBox = 1;     // software renderer: the standings list gets its frame (the game leaves it out)
    int   wsHud            = 1;     // race HUD: 1 = 4:3 proportions (panels stay at the edges), 0 = stretched
    int   wsMenus          = 1;     // menus at the race resolution: 1 = 4:3 in the middle, 2 = stretched, 0 = the game's 640x480
    int   wsPauseMenu      = 1;     // in-race menus: 1 = frame over the whole screen, items in 4:3 proportions; 2 = all of it 4:3 in the middle; 0 = stretched
    int   wsMenuFilter     = 1;     // Direct3D: enlarged menus' big pictures filtered too (the game draws them unfiltered)
    int   wsMenusSoft      = 0;     // experimental: the software renderer's menus at the chosen resolution too
    int   wsMenuResolution = 1;     // 1 = the resolution chosen in the settings, 2 = the desktop's
    int   wsMovies         = 1;     // movies in the menus scaled with them
    int   wsRaceWidth      = 0;     // the race resolution last used (the game itself saves only the width)
    int   wsRaceHeight     = 0;
    int   d3dFog           = 1;     // fog on the Direct3D renderer, like on Glide
    int   loadScreenRenderMs = 100;
    int   portableRegistry = 1;
    int   portableInstallPaths = 1;
    int   installWinFallback = 1;   // built-in install.win when the file is missing
    int   noCDCheck        = 1;     // skip the startup search for the game CD
    int   noDiskSpaceCheck = 1;     // skip the "less than 1MB free" test (wrong on big disks)
    int   wsWeather        = 1;     // rain and snow keep the game's own width of view on a wide screen
    int   fixMovieFreeze   = 1;
    int   gameThreadsOneCore = 1;   // the game's threads share one core (sound races)
    int   widenAffinity    = 1;     // a process pinned to one core gets all cores back; only the game's threads stay on one
    int   fixSound         = 1;     // eacsnd.dll: a lead that doesn't wear down, soundLatencyMs
    int   soundCapture     = -1;    // seconds of the game's mix written to HPFrameFix_mix.wav (-1: 120 in test builds, else 0)
    int   soundLatencyMs   = 40;    // how far ahead of the play cursor it writes (the game's 20, then shrunk)     // let the movie stream thread exit before it's killed
    char  language[16]     = "english";   // for the built-in install.win
    int   d3dDevice        = -1;    // display adapter for Direct3D (0 = primary); -1 = not set here
    char  renderer[64]     = "d3d"; // d3d, software, voodoo, voodoo2, a DLL name, or "" = the game's setting
    char  driversFolder[MAX_PATH] = "";   // renderer DLLs; "" = game folder
    int   exeCheck         = 1;     // 1 = turn the mod off if nfs3.exe differs, 0 = patch anyway
    int   logToFile        = 0;
    int   logEverySeconds  = 10;
};
static Config g_cfg;
static char g_asiDir[MAX_PATH] = {};
static char g_gameDir[MAX_PATH] = {};

static void InitDirs() {
    HMODULE self = nullptr;
    g_asiDir[0] = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&InitDirs), &self) &&
        GetModuleFileNameA(self, g_asiDir, MAX_PATH)) {
        char* s = strrchr(g_asiDir, '\\');
        if (s) s[1] = 0; else g_asiDir[0] = 0;
    }
    g_gameDir[0] = 0;
    if (GetModuleFileNameA(nullptr, g_gameDir, MAX_PATH)) {
        char* s = strrchr(g_gameDir, '\\');
        if (s) s[1] = 0; else g_gameDir[0] = 0;
    }
}

static const char kIniText[] =
    "; HPFrameFix.ini (v" HPFF_VERSION ")\n"
    ";\n"
    "; Lines starting with ; are notes. For on/off settings 1 = on, 0 = off.\n"
    "; Delete this file to get a fresh one with the defaults.\n"
    "\n"
    "\n"
    "[Frame rate]\n"
    "\n"
    "; Draw frames at the display's refresh rate, blending car and camera motion between\n"
    "; the game's fixed 64 steps a second. 0 = the game's 64 fps cap\n"
    "interpolate=1\n"
    "\n"
    "; Run the game's 128 Hz timer from a precise clock. Needed for smooth blending\n"
    "preciseTicks=1\n"
    "\n"
    "; Frame limiter for races.\n"
    ";   -1 = automatic: left to vsync; without vsync, the display's refresh rate\n"
    ";    0 = no limit (the software renderer then starves the sound and the simulation)\n"
    ";    N = N frames a second\n"
    "targetFPS=-1\n"
    "\n"
    "; How many 1/64 s steps behind the newest step frames are drawn. 1.0 is smooth with\n"
    "; the least delay; try 1.5 if you still see hitches\n"
    "interpDelay=1.0\n"
    "\n"
    "; Movement bigger than this in one step isn't blended (resets, camera cuts):\n"
    "; a distance in the game's units, and an angle in degrees\n"
    "maxStepDistance=10\n"
    "maxStepAngle=90\n"
    "\n"
    "\n"
    "[Menus]\n"
    "\n"
    "; Drop the game's 64 fps cap in the menus\n"
    "unlockMenus=1\n"
    "\n"
    "; Frame limiter for the menus: -1 = same as targetFPS, 0 = no limit, N = N fps\n"
    "menuTargetFPS=-1\n"
    "\n"
    "; Highest frame rate in the menus. Their animations count time in 1/128 s.\n"
    ";   -1 = automatic: the highest rate up to 128 that divides the refresh rate\n"
    ";        (120 Hz -> 120, 144 Hz -> 72, 240 Hz -> 120)\n"
    ";    0 = no ceiling\n"
    ";    N = at most N fps\n"
    "menuMaxFPS=-1\n"
    "\n"
    "; Keep the menus' frame-by-frame animations (the drifting logos, the spark\n"
    "; and glow at the top left, the background fade, the text fading in and out)\n"
    "; at their normal speed at any frame rate\n"
    "menuAnimations=1\n"
    "\n"
    "\n"
    "[Loading]\n"
    "\n"
    "; Faster loading: the loading bar is redrawn at most every loadScreenRenderMs\n"
    "; milliseconds instead of at every step (0 = at every step)\n"
    "speedUpLoading=1\n"
    "loadScreenRenderMs=100\n"
    "\n"
    "\n"
    "[Graphics]\n"
    "\n"
    "; Renderer:\n"
    ";   d3d      = Direct3D (d3da.dll)\n"
    ";   software = software renderer (softtria.dll)\n"
    ";   voodoo   = 3dfx Voodoo 1 (voodooa.dll)\n"
    ";   voodoo2  = 3dfx Voodoo2 (voodoo2a.dll; Banshee/Voodoo3 with the Banshee patch's DLL)\n"
    ";   or one of the game's other renderer DLL names.\n"
    ";   Empty = the game's own setting (the Windows registry's; software if it has none)\n"
    "renderer=d3d\n"
    "\n"
    "; Direct3D: which display adapter to use. 0 = the primary display, 1 = the second...\n"
    "; -1 = not set here (the Windows registry's if it has one, else the primary display)\n"
    "d3dDevice=-1\n"
    "\n"
    "; Folder the renderer DLLs are loaded from: a folder in the game folder, or a full\n"
    "; path. A DLL that isn't there is loaded from the game folder. Empty = the game folder\n"
    "driversFolder=\n"
    "\n"
    "; Direct3D: fog, as the game has it on 3dfx cards. 0 = the game's Direct3D look\n"
    "d3dFog=1\n"
    "\n"
    "; Direct3D: every vertex is moved half a pixel up and left, so 2D pictures (HUD,\n"
    "; menus, text) land exactly on the pixels and come out sharp. 0 = as the renderer has it\n"
    "alternatePixelCenter=1\n"
    "\n"
    "; Largest texture the game loads: a power of two (the game's own is 256, and 128 for\n"
    "; tracks and other cars). Above 256 with the Direct3D renderer only; the others\n"
    "; can't draw larger ones. This doesn't sharpen the original textures; it's for HD\n"
    "; texture and car mods\n"
    "maxTextureSize=512\n"
    "\n"
    "; Cars switch to their simpler models at the game's distances times this\n"
    "; (1 = the game's: on high detail the first switch is at 30 m)\n"
    "carLodScale=2.0\n"
    "\n"
    "; Cars further away than this many metres aren't drawn. 0 = the game's 300\n"
    "carDrawDistance=0\n"
    "\n"
    "; The game drops the farthest cars to lower detail once a small budget is used up,\n"
    "; even on high car detail. 1 = no budget, every car keeps its detail\n"
    "fullDetailCars=1\n"
    "\n"
    "\n"
    "[Widescreen]\n"
    "\n"
    "; Master switch for everything in this section and the next one\n"
    "widescreen=1\n"
    "\n"
    "; Race view: keep the 4:3 view's height and show more at the sides. 0 = the game\n"
    "; stretches the 4:3 picture to the screen's width\n"
    "wsFixFOV=1\n"
    "\n"
    "; Rain and snow on a wide screen keep the game's own width of view (their height is\n"
    "; narrowed instead, so the flakes are a little larger). 0 = widened like the rest of\n"
    "; the view, which shows big white blocks at the screen's edges\n"
    "wsWeather=1\n"
    "\n"
    "; List every resolution in the options. The game hides those that aren't 4:3 and those\n"
    "; it thinks need more than 4 MB of video memory. Its list holds 20: the largest are kept\n"
    "wsUnlockResolutions=1\n"
    "\n"
    "; List 32-bit colour modes too, and prefer them to 16-bit, if the renderer offers them\n"
    "ws32bit=1\n"
    "\n"
    "; Race HUD: 1 = the panels keep their 4:3 shape and stay at the screen's edges,\n"
    "; 0 = stretched sideways\n"
    "wsHud=1\n"
    "\n"
    "; Cockpit view: 1 = the dashboard picture keeps its shape, zoomed to the screen's\n"
    "; width (its top and bottom are cut off), 0 = stretched to the screen\n"
    "wsCockpit=1\n"
    "\n"
    "; Loading screen: 1 = 4:3 in the middle of the screen, 0 = stretched\n"
    "wsLoading=1\n"
    "\n"
    "\n"
    "[Widescreen - menus]\n"
    "\n"
    "; Menus at the chosen resolution, without switching the screen to 640x480:\n"
    ";   1 = in 4:3 proportions in the middle of the screen\n"
    ";   2 = stretched to the screen\n"
    ";   0 = the game's 640x480\n"
    "wsMenus=1\n"
    "\n"
    "; Which resolution those menus use: 1 = the one chosen in the game's options (as in\n"
    "; Hot Pursuit 2), 2 = the desktop's\n"
    "wsMenuResolution=1\n"
    "\n"
    "; Direct3D, menus enlarged by wsMenus: 1 = backgrounds, titles and the sliding stripes\n"
    "; are smoothed like everything else. 0 = the game's own (jagged, uneven pixels when\n"
    "; enlarged)\n"
    "wsMenuFilter=1\n"
    "\n"
    "; Movies in the menus (intro, demo) are scaled along with them. 0 = top-left corner\n"
    "wsMovies=1\n"
    "\n"
    "; The pause menu and the other menus shown during a race:\n"
    ";   1 = the frame covers the whole screen; text and buttons keep their 4:3 shape\n"
    ";       (the bottom row's buttons stay at the sides)\n"
    ";   2 = the whole menu in 4:3 proportions in the middle\n"
    ";   0 = stretched to the screen\n"
    "wsPauseMenu=1\n"
    "\n"
    "; Direct3D, menus above 640x480: no thin lines beside buttons and pictures (each\n"
    "; picture is sampled half a texel in from its edges). For the menus during a race this\n"
    "; needs wsPauseMenu on\n"
    "menuTexelInset=1\n"
    "\n"
    "; The race resolution last used, kept here by HPFrameFix (the game itself saves only\n"
    "; the width and would pick a 4:3 mode at startup). 0 = none yet\n"
    "wsRaceWidth=0\n"
    "wsRaceHeight=0\n"
    "\n"
    "\n"
    "[Software renderer]\n"
    "\n"
    "; The standings list in a race gets its frame (the game draws it with no box at all)\n"
    "softStandingsBox=1\n"
    "\n"
    "; Experimental: the software renderer's menus at the chosen resolution too. They are\n"
    "; drawn at 640x480 as always and each finished frame is scaled up. Needs wsMenus on.\n"
    "; 0 = its menus stay 640x480\n"
    "wsMenusSoft=0\n"
    "\n"
    "\n"
    "[Gameplay]\n"
    "\n"
    "; Cars in the air are slowed down by a strong drag of the game's own, so every jump\n"
    "; costs a lot of speed. This is that drag times this number, for every car (yours,\n"
    "; the other racers, the police). 0 = none: a car keeps its speed through the air.\n"
    "; 1 = the game's\n"
    "airDrag=0\n"
    "\n"
    "\n"
    "[Sound]\n"
    "\n"
    "; The game's sound driver keeps cutting down how far ahead of playback it writes,\n"
    "; until the sound runs dry and pops. 1 = it stays at soundLatencyMs\n"
    "fixSound=1\n"
    "\n"
    "; How far ahead of playback the sound is written, in milliseconds (the game's 20).\n"
    "; Raise it if you hear dropouts; higher = sound a little later. Needs fixSound=1\n"
    "soundLatencyMs=40\n"
    "\n"
    "; The game's own threads (main, sound, streaming) all run on one CPU core; its sound\n"
    "; code was written for one CPU and crackles when they run side by side. The system's\n"
    "; threads keep the other cores. 0 = leave them alone\n"
    "gameThreadsOneCore=1\n"
    "\n"
    "; If the whole game was started on one core (taskset, a launcher's single-core\n"
    "; option), the system's sound threads share that core and the sound pops whenever the\n"
    "; game is busy. 1 = the game gets all cores back and only its own threads stay on one.\n"
    "; The same is done if something pins the game later, such as a DirectDraw\n"
    "; wrapper's single-core option. Needs gameThreadsOneCore=1\n"
    "widenAffinity=1\n"
    "\n"
    "\n"
    "[Fixes]\n"
    "\n"
    "; Closing the window (Alt+F4, the title bar, the window manager) ends the game.\n"
    "; 0 = the game ignores it and only quits from its menus\n"
    "closeExits=1\n"
    "\n"
    "; Movies: no freeze on a black screen after a movie on CPUs with more than one core\n"
    "fixMovieFreeze=1\n"
    "\n"
    "; No test for free disk space at startup. On today's big disks the game's test goes\n"
    "; wrong and says \"Your hard drive has less than 1MB free space\". 0 = the game's test\n"
    "noDiskSpaceCheck=1\n"
    "\n"
    "\n"
    "[Portable]\n"
    "\n"
    "; The game needs no registry entries: the renderer and the display adapter come from\n"
    "; this file (renderer, d3dDevice). 0 = the Windows registry as it is\n"
    "portableRegistry=1\n"
    "\n"
    "; Folders that install.win puts on a drive letter (E:\\FeData\\movies\\ from a partial\n"
    "; install) are read from the game folder instead, when they are there\n"
    "portableInstallPaths=1\n"
    "\n"
    "; If install.win is missing, use a built-in full install (every folder in the game\n"
    "; folder) instead of the game's \"files are corrupted\" error\n"
    "installWinFallback=1\n"
    "\n"
    "; Language of that built-in install: english, german, french, spanish, italian, swedish\n"
    "language=english\n"
    "\n"
    "; Don't ask for the game CD at startup\n"
    "noCDCheck=1\n"
    "\n"
    "\n"
    "[Advanced]\n"
    "\n"
    "; 1 = if nfs3.exe isn't the version this mod was made for, the mod turns itself off.\n"
    "; 0 = patch anyway (only for a modified exe you know is the same version)\n"
    "exeCheck=1\n"
    "\n"
    "; Write HPFrameFix.log next to this file (what was patched, and frame and sound\n"
    "; statistics every logEverySeconds seconds). Turn it on when reporting a problem\n"
    "logToFile=0\n"
    "logEverySeconds=10\n"
    "\n"
    "; Diagnostics: write the first N seconds of the game's sound to HPFrameFix_mix.wav\n"
    "; (0 = off)\n"
    "soundCapture=0\n";

static void LoadConfig() {
    InitDirs();
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sHPFrameFix.ini", g_asiDir);
    FILE* f = fopen(path, "r");
    if (!f) {
        f = fopen(path, "w");
        if (f) { fputs(kIniText, f); fclose(f); }
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ';' || line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        char key[64] = {}, val[MAX_PATH] = {};
        if (sscanf(line, " %63[^= \t] = %259[^;\r\n]", key, val) < 1) continue;
        for (size_t n = strlen(val); n && (val[n - 1] == ' ' || val[n - 1] == '\t'); ) val[--n] = 0;
        // (these two may be empty: the game folder, the game's own setting)
        if (!_stricmp(key, "driversFolder")) { lstrcpynA(g_cfg.driversFolder, val, sizeof(g_cfg.driversFolder)); continue; }
        if (!_stricmp(key, "renderer"))      { lstrcpynA(g_cfg.renderer, val, sizeof(g_cfg.renderer)); continue; }
        if (!val[0]) continue;
        if      (!_stricmp(key, "language"))             lstrcpynA(g_cfg.language, val, sizeof(g_cfg.language));
        else if (!_stricmp(key, "installWinFallback"))   g_cfg.installWinFallback = atoi(val);
        else if (!_stricmp(key, "gameThreadsOneCore"))   g_cfg.gameThreadsOneCore = atoi(val);
        else if (!_stricmp(key, "widenAffinity"))        g_cfg.widenAffinity = atoi(val);
        else if (!_stricmp(key, "fixSound"))             g_cfg.fixSound = atoi(val);
        else if (!_stricmp(key, "soundLatencyMs"))       g_cfg.soundLatencyMs = atoi(val);
        else if (!_stricmp(key, "soundCapture"))         g_cfg.soundCapture = atoi(val);
        else if (!_stricmp(key, "fixMovieFreeze"))       g_cfg.fixMovieFreeze = atoi(val);
        else if (!_stricmp(key, "noCDCheck"))            g_cfg.noCDCheck = atoi(val);
        else if (!_stricmp(key, "d3dDevice"))            g_cfg.d3dDevice = atoi(val);
        else if (!_stricmp(key, "noDiskSpaceCheck"))     g_cfg.noDiskSpaceCheck = atoi(val);
        else if (!_stricmp(key, "wsWeather"))            g_cfg.wsWeather = atoi(val);
        else if (!_stricmp(key, "interpolate"))          g_cfg.interpolate = atoi(val);
        else if (!_stricmp(key, "preciseTicks"))         g_cfg.preciseTicks = atoi(val);
        else if (!_stricmp(key, "targetFPS"))            g_cfg.targetFPS = atoi(val);
        else if (!_stricmp(key, "maxStepDistance"))      g_cfg.maxStepDistance = (float)atof(val);
        else if (!_stricmp(key, "maxStepAngle"))         g_cfg.maxStepAngle = (float)atof(val);
        else if (!_stricmp(key, "interpDelay"))          g_cfg.interpDelay = (float)atof(val);
        else if (!_stricmp(key, "unlockMenus"))          g_cfg.unlockMenus = atoi(val);
        else if (!_stricmp(key, "menuTargetFPS"))        g_cfg.menuTargetFPS = atoi(val);
        else if (!_stricmp(key, "menuMaxFPS"))           g_cfg.menuMaxFPS = atoi(val);
        else if (!_stricmp(key, "menuAnimations"))       g_cfg.menuAnimations = atoi(val);
        else if (!_stricmp(key, "speedUpLoading"))       g_cfg.speedUpLoading = atoi(val);
        else if (!_stricmp(key, "carLodScale"))          g_cfg.carLodScale = (float)atof(val);
        else if (!_stricmp(key, "carDrawDistance"))      g_cfg.carDrawDistance = atoi(val);
        else if (!_stricmp(key, "fullDetailCars"))       g_cfg.fullDetailCars = atoi(val);
        else if (!_stricmp(key, "airDrag"))              g_cfg.airDrag = (float)atof(val);
        else if (!_stricmp(key, "maxTextureSize"))       g_cfg.maxTextureSize = atoi(val);
        else if (!_stricmp(key, "closeExits"))           g_cfg.closeExits = atoi(val);
        else if (!_stricmp(key, "widescreen"))           g_cfg.widescreen = atoi(val);
        else if (!_stricmp(key, "wsFixFOV"))             g_cfg.wsFixFOV = atoi(val);
        else if (!_stricmp(key, "wsUnlockResolutions"))  g_cfg.wsUnlockResolutions = atoi(val);
        else if (!_stricmp(key, "ws32bit"))              g_cfg.ws32bit = atoi(val);
        else if (!_stricmp(key, "wsCockpit"))            g_cfg.wsCockpit = atoi(val);
        else if (!_stricmp(key, "wsLoading"))            g_cfg.wsLoading = atoi(val);
        else if (!_stricmp(key, "alternatePixelCenter")) g_cfg.alternatePixelCenter = atoi(val);
        else if (!_stricmp(key, "menuTexelInset"))       g_cfg.menuTexelInset = atoi(val);
        else if (!_stricmp(key, "softStandingsBox"))     g_cfg.softStandingsBox = atoi(val);
        else if (!_stricmp(key, "wsHud"))                g_cfg.wsHud = atoi(val);
        else if (!_stricmp(key, "wsMenus"))              g_cfg.wsMenus = atoi(val);
        else if (!_stricmp(key, "wsPauseMenu"))          g_cfg.wsPauseMenu = atoi(val);
        else if (!_stricmp(key, "wsMenuFilter"))         g_cfg.wsMenuFilter = atoi(val);
        else if (!_stricmp(key, "wsMenusSoft"))          g_cfg.wsMenusSoft = atoi(val);
        else if (!_stricmp(key, "wsMenuResolution"))     g_cfg.wsMenuResolution = atoi(val);
        else if (!_stricmp(key, "wsMovies"))             g_cfg.wsMovies = atoi(val);
        else if (!_stricmp(key, "wsRaceWidth"))          g_cfg.wsRaceWidth = atoi(val);
        else if (!_stricmp(key, "wsRaceHeight"))         g_cfg.wsRaceHeight = atoi(val);
        else if (!_stricmp(key, "d3dFog"))               g_cfg.d3dFog = atoi(val);
        else if (!_stricmp(key, "loadScreenRenderMs"))   g_cfg.loadScreenRenderMs = atoi(val);
        else if (!_stricmp(key, "portableRegistry"))     g_cfg.portableRegistry = atoi(val);
        else if (!_stricmp(key, "portableInstallPaths")) g_cfg.portableInstallPaths = atoi(val);
        else if (!_stricmp(key, "exeCheck"))             g_cfg.exeCheck = atoi(val);
        else if (!_stricmp(key, "logToFile"))            g_cfg.logToFile = atoi(val);
        else if (!_stricmp(key, "logEverySeconds"))      g_cfg.logEverySeconds = atoi(val);
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static FILE* g_log = nullptr;
static CRITICAL_SECTION g_logLock;
static bool g_logLockInit = false;

static void Logf(const char* fmt, ...) {
    if (!g_log) return;
    if (g_logLockInit) EnterCriticalSection(&g_logLock);
    va_list ap; va_start(ap, fmt);
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
    if (g_logLockInit) LeaveCriticalSection(&g_logLock);
}

// Log lines from the game thread go through a queue and are written by a
// background thread, so the game thread never waits on the disk.
static CRITICAL_SECTION g_qLock;
static HANDLE g_qEvent = nullptr;
static char g_q[256][192];
static int g_qHead = 0, g_qTail = 0;
static void LogfAsync(const char* fmt, ...) {
    if (!g_log || !g_qEvent) return;
    char buf[192];
    SYSTEMTIME st; GetLocalTime(&st);
    int n = snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond,
                     st.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    if (m >= (int)sizeof(buf) - n) buf[sizeof(buf) - 2] = '\n';   // cut short: still one line
    EnterCriticalSection(&g_qLock);
    int next = (g_qHead + 1) % 256;
    if (next != g_qTail) { memcpy(g_q[g_qHead], buf, sizeof(buf)); g_qHead = next; }
    LeaveCriticalSection(&g_qLock);
    SetEvent(g_qEvent);
}
static void DrainLogQueue() {
    for (;;) {
        char line[192];
        EnterCriticalSection(&g_qLock);
        bool any = g_qTail != g_qHead;
        if (any) { memcpy(line, g_q[g_qTail], sizeof(line)); g_qTail = (g_qTail + 1) % 256; }
        LeaveCriticalSection(&g_qLock);
        if (!any) break;
        if (!g_log) continue;
        if (g_logLockInit) EnterCriticalSection(&g_logLock);
        fputs(line, g_log); fflush(g_log);
        if (g_logLockInit) LeaveCriticalSection(&g_logLock);
    }
}
static void CaptureFlush();
static void AffinityWatch();
static DWORD WINAPI WriterThread(LPVOID) {
    for (;;) { WaitForSingleObject(g_qEvent, 1000); AffinityWatch(); DrainLogQueue(); CaptureFlush(); }   // (at least once a second)
}

static void OpenLog() {
    if (!g_cfg.logToFile) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%sHPFrameFix.log", g_asiDir);
    g_log = fopen(path, "w");
    Logf("HPFrameFix v" HPFF_VERSION " | interpolate=%d preciseTicks=%d targetFPS=%d "
         "maxStepDistance=%.1f maxStepAngle=%.0f interpDelay=%.2f unlockMenus=%d menuTargetFPS=%d "
         "menuMaxFPS=%d\n", g_cfg.interpolate, g_cfg.preciseTicks, g_cfg.targetFPS,
         g_cfg.maxStepDistance, g_cfg.maxStepAngle, g_cfg.interpDelay, g_cfg.unlockMenus,
         g_cfg.menuTargetFPS, g_cfg.menuMaxFPS);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool PatchBytes(DWORD at, const void* bytes, size_t len) {
    DWORD old;
    if (!VirtualProtect((void*)at, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)at, bytes, len);
    DWORD d; VirtualProtect((void*)at, len, old, &d);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, len);
    return true;
}
static bool PatchRel32(DWORD at, BYTE op, void* target) {   // E8 call / E9 jmp
    BYTE b[5]; b[0] = op;
    DWORD rel = (DWORD)target - (at + 5);
    memcpy(b + 1, &rel, 4);
    return PatchBytes(at, b, 5);
}
static bool PatchPointer(void** slot, void* nv, void** ov) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    if (ov) *ov = *slot;
    *slot = nv;
    DWORD d; VirtualProtect(slot, sizeof(void*), old, &d);
    return true;
}
template <typename Fn>
static void ForEachImportIn(HMODULE mod, const char* dll, Fn cb) {
    BYTE* base = (BYTE*)mod;
    auto dos = (PIMAGE_DOS_HEADER)base;
    auto nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return;
    for (auto imp = (PIMAGE_IMPORT_DESCRIPTOR)(base + rva); imp->Name; ++imp) {
        if (_stricmp((const char*)(base + imp->Name), dll)) continue;
        auto th = (PIMAGE_THUNK_DATA)(base + imp->FirstThunk);
        auto on = (PIMAGE_THUNK_DATA)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk
                                                                      : imp->FirstThunk));
        for (; th->u1.Function; ++th, ++on) {
            if (IMAGE_SNAP_BY_ORDINAL(on->u1.Ordinal)) continue;
            auto nm = (PIMAGE_IMPORT_BY_NAME)(base + on->u1.AddressOfData);
            cb((void**)&th->u1.Function, (const char*)nm->Name);
        }
    }
}
template <typename Fn>
static void ForEachImport(const char* dll, Fn cb) { ForEachImportIn(GetModuleHandleA(nullptr), dll, cb); }
// This mod's own threads run on every core the process has, even if they're
// created from a game thread pinned to one core (gameThreadsOneCore).
static HANDLE g_modThreads[8]; static volatile LONG g_modThreadN = 0;   // (handles of our own: callers close theirs)
static HANDLE StartModThread(LPTHREAD_START_ROUTINE fn) {
    HANDLE h = CreateThread(nullptr, 0, fn, nullptr, CREATE_SUSPENDED, nullptr);
    if (!h) return nullptr;
    DWORD_PTR proc = 0, sys = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) && proc) SetThreadAffinityMask(h, proc);
    HANDLE d = nullptr;
    if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        LONG i = InterlockedIncrement(&g_modThreadN) - 1;
        if (i < 8) g_modThreads[i] = d; else CloseHandle(d);
    }
    ResumeThread(h);
    return h;
}
template <typename T> static inline T Rd(DWORD a) { return *(volatile T*)a; }

static bool g_knownExe = false;
static bool CheckExe() {
    if (GetModuleHandleA(nullptr) != (HMODULE)0x400000) {
        Logf("exe check: unexpected image base - disabled\n"); return false;
    }
    int bad = 0, total = 0;
    for (const auto& s : hp::kSigs) {
        total++;
        if (IsBadReadPtr((void*)s.addr, s.len)) {
            Logf("exe check: 0x%06lX unreadable\n", (unsigned long)s.addr); bad++; continue;
        }
        if (!memcmp((void*)s.addr, s.bytes, s.len)) continue;
        bad++;
        char exp[3 * 24 + 1] = {}, got[3 * 24 + 1] = {};
        const BYTE* g = (const BYTE*)s.addr;
        size_t n = s.len < 24 ? s.len : 24;
        for (size_t i = 0; i < n; i++) {
            bool d = g[i] != (BYTE)s.bytes[i];
            snprintf(exp + i * 3, 4, "%02X%c", (BYTE)s.bytes[i], d ? '*' : ' ');
            snprintf(got + i * 3, 4, "%02X%c", g[i], d ? '*' : ' ');
        }
        Logf("exe check: 0x%06lX differs (* = changed byte)\n    expected %s\n    found    %s\n",
             (unsigned long)s.addr, exp, got);
    }
    if (!bad) { Logf("exe check: nfs3.exe matches (%d checks)\n", total); return true; }
    if (g_cfg.exeCheck) {
        Logf("exe check: %d of %d checks differ - not the analysed nfs3.exe, mod disabled "
             "(exeCheck=0 in the ini patches anyway)\n", bad, total);
        return false;
    }
    Logf("exe check: %d of %d checks differ - exeCheck=0, patching anyway\n", bad, total);
    return true;
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
static LARGE_INTEGER g_qpf;
static inline LONGLONG Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static inline double QpcMs(LONGLONG d) { return (double)d * 1000.0 / (double)g_qpf.QuadPart; }

static void WaitUntilOn(HANDLE timer, LONGLONG deadline) {
    for (;;) {
        LONGLONG now = Qpc(), rem = deadline - now;
        if (rem <= 0) return;
        double ms = QpcMs(rem);
        if (ms > 2.0 && timer) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)((ms - 1.5) * 10000.0);
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE); continue;
            }
        }
        if (ms > 1.0) Sleep(0); else YieldProcessor();
    }
}

static HANDLE CreateHiResTimer() {
    HANDLE t = CreateWaitableTimerExA(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                      TIMER_ALL_ACCESS);
    if (!t) t = CreateWaitableTimerA(nullptr, FALSE, nullptr);
    return t;
}

// ---------------------------------------------------------------------------
// Precise 128 Hz scheduler (replaces the game's timeSetEvent one-shot chain)
// ---------------------------------------------------------------------------
typedef MMRESULT (WINAPI *timeSetEvent_t)(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT);
typedef MMRESULT (WINAPI *timeKillEvent_t)(UINT);
static timeSetEvent_t  g_origSetEvent = nullptr;
static timeKillEvent_t g_origKillEvent = nullptr;
static const UINT kFakeId = 0x48504646;   // "HPFF"
static HANDLE g_tickWake = nullptr, g_tickThread = nullptr;
static std::atomic<bool> g_tickArmed{false};
static std::atomic<UINT> g_tickFirstDelay{0};
static std::atomic<unsigned long> g_tickFires{0}, g_tickResyncs{0};
static bool g_ticksActive = false;

static LONGLONG TickPeriodQpc() {
    DWORD fx = Rd<DWORD>(hp::pTimerPeriod);   // ms << 16
    if (fx < 0x10000 || fx > 1000u * 0x10000u) fx = (1000u << 16) / 128;
    return (LONGLONG)((double)fx / 65536.0 * (double)g_qpf.QuadPart / 1000.0 + 0.5);
}

static DWORD WINAPI TickThread(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    HANDLE timer = CreateHiResTimer();
    LONGLONG next = 0;
    auto cb = (void (WINAPI*)(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR))hp::fnTimerCallback;
    for (;;) {
        if (!g_tickArmed.load()) { WaitForSingleObject(g_tickWake, INFINITE); next = 0; continue; }
        LONGLONG period = TickPeriodQpc();
        LONGLONG now = Qpc();
        if (!next) next = now + (LONGLONG)g_tickFirstDelay.load() * g_qpf.QuadPart / 1000;
        if (now - next > period * 8) { next = now; g_tickResyncs++; }   // stalled: don't burst
        WaitUntilOn(timer, next);
        if (!g_tickArmed.exchange(false)) continue;   // killed while waiting
        g_tickFires++;
        cb(kFakeId, 0, 0, 0, 0);                      // re-arms itself via timeSetEvent
        next += period;
    }
}

static MMRESULT WINAPI Hook_timeSetEvent(UINT delay, UINT res, LPTIMECALLBACK cb, DWORD_PTR user, UINT flags) {
    if ((DWORD)cb == hp::fnTimerCallback && g_ticksActive) {
        g_tickFirstDelay = delay;
        g_tickArmed = true;
        SetEvent(g_tickWake);
        return kFakeId;
    }
    return g_origSetEvent(delay, res, cb, user, flags);
}
static MMRESULT WINAPI Hook_timeKillEvent(UINT id) {
    if (id == kFakeId) { g_tickArmed = false; return TIMERR_NOERROR; }
    return g_origKillEvent(id);
}

static void InstallTickScheduler() {
    if (!g_knownExe || !g_cfg.preciseTicks) { Logf("precise ticks: off\n"); return; }
    g_tickWake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!g_tickWake) { Logf("precise ticks: FAILED (no event) - game timer used\n"); return; }
    // Thread first: once the imports are hooked, the game's next timeSetEvent
    // depends on it.
    g_tickThread = StartModThread(TickThread);
    if (!g_tickThread) { Logf("precise ticks: FAILED (no thread) - game timer used\n"); return; }
    bool s = false, k = false;
    ForEachImport("winmm.dll", [&](void** slot, const char* name) {
        void* old = nullptr;
        if (!strcmp(name, "timeSetEvent") && PatchPointer(slot, (void*)&Hook_timeSetEvent, &old)) {
            g_origSetEvent = (timeSetEvent_t)old; s = true;
        } else if (!strcmp(name, "timeKillEvent") && PatchPointer(slot, (void*)&Hook_timeKillEvent, &old)) {
            g_origKillEvent = (timeKillEvent_t)old; k = true;
        }
    });
    g_ticksActive = s && k;
    Logf("precise ticks: %s\n", g_ticksActive ? "on" : "FAILED (imports not found) - game timer used");
}

// ---------------------------------------------------------------------------
// Transform snapshots
// ---------------------------------------------------------------------------
struct Xform { float p[3]; float m[9]; };
struct Entry { DWORD obj; int kind; Xform x; };    // kind: 0 car, 1 camera
static const int kMaxEntries = 96;                 // 16 cars + 48 table cameras + 4 active/override
struct Snapshot { int n = 0; long step = 0; DWORD activeCam[2] = {0, 0};   // effective (override or active)
                 DWORD camSlot[4] = {0, 0, 0, 0};                         // active[2], override[2] as stored
                 DWORD look[2] = {0, 0};                                  // look back flag the step's camera update saw
                 Entry e[kMaxEntries]; };

static const int kRing = 4;
static Snapshot g_ring[kRing];   // g_ring[g_head] = latest
static int  g_head = 0;
static int  g_ringN = 0;         // valid snapshots (0..kRing)
static long g_lastStepIdx = 0;
// Smoothed sim clock: step index s is shown at time g_clockT0 + s * p64.
// g_clockT0 follows the steps' arrival times with a slow average, so frame-to-
// frame lateness (vsync, input batching) never moves the timeline by a whole step.
static LONGLONG g_clockT0 = 0;
static bool g_clockValid = false;
static std::atomic<unsigned long> g_clockSnaps{0};

static inline DWORD PosOff(int kind) { return kind == 1 ? hp::camPos : hp::carPos; }
static inline DWORD RotOff(int kind) { return kind == 1 ? hp::camRot : hp::carRot; }

// The cameras are static objects inside the exe's image (data and .bss,
// 0x536000..0xA19000), always mapped: a range check is enough there, and
// IsBadReadPtr (slow) is only needed for heap objects (cars).
static inline bool Readable(DWORD a, DWORD n) {
    if (a >= 0x536000 && a + n <= 0xA19000) return true;
    return !IsBadReadPtr((void*)a, n);
}
static inline bool ReadXform(DWORD obj, int kind, Xform* x) {
    DWORD po = PosOff(kind), mo = RotOff(kind);
    DWORD lo = po < mo ? po : mo, hi = (po + 12 > mo + 36) ? po + 12 : mo + 36;
    if (!Readable(obj + lo, hi - lo)) return false;
    memcpy(x->p, (void*)(obj + po), 12);
    memcpy(x->m, (void*)(obj + mo), 36);
    return true;
}
static inline void WriteXform(DWORD obj, int kind, const Xform& x) {
    memcpy((void*)(obj + PosOff(kind)), x.p, 12);
    memcpy((void*)(obj + RotOff(kind)), x.m, 36);
}

static DWORD EffectiveCam(int v) {
    DWORD o = Rd<DWORD>(hp::pCamOverride + v * 4);
    return o ? o : Rd<DWORD>(hp::pCamActive + v * 4);
}

static void Collect(Snapshot& s) {
    s.n = 0;
    s.activeCam[0] = EffectiveCam(0); s.activeCam[1] = EffectiveCam(1);
    for (int v = 0; v < 2; v++) { s.camSlot[v] = Rd<DWORD>(hp::pCamActive + v * 4);
                                  s.camSlot[2 + v] = Rd<DWORD>(hp::pCamOverride + v * 4);
                                  s.look[v] = Rd<DWORD>(hp::pLookBack + v * 4); }
    long nc = Rd<long>(hp::pCarCount);
    if (nc < 0) nc = 0;
    if (nc > 16) nc = 16;
    for (long i = 0; i < nc && s.n < kMaxEntries; i++) {
        DWORD car = Rd<DWORD>(hp::pCarList + i * 4);
        if (!car) continue;
        Entry& e = s.e[s.n]; e.obj = car; e.kind = 0;
        if (ReadXform(car, 0, &e.x)) s.n++;
    }
    DWORD seen[hp::camTableSlots + 4]; int ns = 0;
    auto addCam = [&](DWORD cam) {
        if (!cam || s.n >= kMaxEntries) return;
        for (int k = 0; k < ns; k++) if (seen[k] == cam) return;
        if (ns < hp::camTableSlots + 4) seen[ns++] = cam;
        Entry& e = s.e[s.n]; e.obj = cam; e.kind = 1;
        if (ReadXform(cam, 1, &e.x)) s.n++;
    };
    for (int i = 0; i < hp::camTableSlots; i++) addCam(Rd<DWORD>(hp::pCamTable + i * 4));
    for (int v = 0; v < 2; v++) { addCam(Rd<DWORD>(hp::pCamActive + v * 4));
                                  addCam(Rd<DWORD>(hp::pCamOverride + v * 4)); }
}

static std::atomic<unsigned long> g_steps{0}, g_resets{0};

static LONGLONG StepPeriodQpc() { return TickPeriodQpc() * 2; }   // 64 Hz

static void TraceStep(const Snapshot& s);
extern "C" void __cdecl OnSimStep() {
    LONGLONG now = Qpc();
    long idx = Rd<long>(hp::pSimStepIdx);           // index of the step that just ran
    if (g_ringN && idx == g_lastStepIdx) {           // replay fast-forward: 2 steps per index
        Collect(g_ring[g_head]);
        g_steps++;
        return;
    }
    if (g_ringN && (idx < g_lastStepIdx || idx - g_lastStepIdx > 16)) {
        g_ringN = 0; g_clockValid = false; g_resets++;   // restart / jump: no blending across it
    }
    g_lastStepIdx = idx;
    g_head = (g_head + 1) % kRing;
    Snapshot& s = g_ring[g_head];
    Collect(s);
    s.step = idx;
    if (g_ringN < kRing) g_ringN++;
    if (kTestBuild) TraceStep(s);

    LONGLONG p = StepPeriodQpc();
    LONGLONG t0 = now - (LONGLONG)idx * p;           // arrival-based origin of this step
    if (!g_clockValid) { g_clockT0 = t0; g_clockValid = true; }
    else {
        LONGLONG d = t0 - g_clockT0;
        if (d > 4 * p || d < -4 * p) { g_clockT0 = t0; g_clockSnaps++; }   // lost sync: snap
        else g_clockT0 += d / 32;                                         // slow follow
    }
    g_steps++;
}

// ---------------------------------------------------------------------------
// Interpolation math
// ---------------------------------------------------------------------------
struct Quat { float w, x, y, z; };
static Quat MatToQuat(const float* m) {   // m row-major 3x3
    Quat q; float tr = m[0] + m[4] + m[8];
    if (tr > 0) { float s = sqrtf(tr + 1.0f) * 2; q.w = 0.25f * s; q.x = (m[7] - m[5]) / s;
                  q.y = (m[2] - m[6]) / s; q.z = (m[3] - m[1]) / s; }
    else if (m[0] > m[4] && m[0] > m[8]) { float s = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2;
                  q.w = (m[7] - m[5]) / s; q.x = 0.25f * s; q.y = (m[1] + m[3]) / s; q.z = (m[2] + m[6]) / s; }
    else if (m[4] > m[8]) { float s = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2;
                  q.w = (m[2] - m[6]) / s; q.x = (m[1] + m[3]) / s; q.y = 0.25f * s; q.z = (m[5] + m[7]) / s; }
    else { float s = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2;
                  q.w = (m[3] - m[1]) / s; q.x = (m[2] + m[6]) / s; q.y = (m[5] + m[7]) / s; q.z = 0.25f * s; }
    return q;
}
static void QuatToMat(const Quat& q, float* m) {
    float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z, xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z,
          wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    m[0] = 1 - 2*(yy+zz); m[1] = 2*(xy-wz);     m[2] = 2*(xz+wy);
    m[3] = 2*(xy+wz);     m[4] = 1 - 2*(xx+zz); m[5] = 2*(yz-wx);
    m[6] = 2*(xz-wy);     m[7] = 2*(yz+wx);     m[8] = 1 - 2*(xx+yy);
}
static bool IsRotation(const float* m) {
    for (int r = 0; r < 3; r++) {
        float l = m[r*3]*m[r*3] + m[r*3+1]*m[r*3+1] + m[r*3+2]*m[r*3+2];
        if (fabsf(l - 1.0f) > 0.02f) return false;
    }
    float det = m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6]) + m[2]*(m[3]*m[7]-m[4]*m[6]);
    return fabsf(det - 1.0f) < 0.05f;
}
// returns false if the pair should not be blended
static bool Blend(const Xform& a, const Xform& b, float t, Xform* out) {
    float dx = b.p[0]-a.p[0], dy = b.p[1]-a.p[1], dz = b.p[2]-a.p[2];
    if (dx*dx + dy*dy + dz*dz > g_cfg.maxStepDistance * g_cfg.maxStepDistance) return false;
    for (int i = 0; i < 3; i++) out->p[i] = a.p[i] + (b.p[i] - a.p[i]) * t;
    if (!memcmp(a.m, b.m, 36)) { memcpy(out->m, b.m, 36); return true; }
    if (!IsRotation(a.m) || !IsRotation(b.m)) {          // not a pure rotation: plain lerp
        for (int i = 0; i < 9; i++) out->m[i] = a.m[i] + (b.m[i] - a.m[i]) * t;
        return true;
    }
    Quat qa = MatToQuat(a.m), qb = MatToQuat(b.m);
    float d = qa.w*qb.w + qa.x*qb.x + qa.y*qb.y + qa.z*qb.z;
    if (d < 0) { qb.w = -qb.w; qb.x = -qb.x; qb.y = -qb.y; qb.z = -qb.z; d = -d; }
    if (d > 1) d = 1;
    float ang = 2.0f * acosf(d) * 57.29578f;
    if (ang > g_cfg.maxStepAngle) return false;
    Quat q;
    if (d > 0.9995f) {
        q.w = qa.w + (qb.w-qa.w)*t; q.x = qa.x + (qb.x-qa.x)*t;
        q.y = qa.y + (qb.y-qa.y)*t; q.z = qa.z + (qb.z-qa.z)*t;
    } else {
        float th = acosf(d), s = sinf(th), wa = sinf((1-t)*th)/s, wb = sinf(t*th)/s;
        q.w = qa.w*wa + qb.w*wb; q.x = qa.x*wa + qb.x*wb; q.y = qa.y*wa + qb.y*wb; q.z = qa.z*wa + qb.z*wb;
    }
    float l = sqrtf(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (l < 1e-6f) return false;
    q.w /= l; q.x /= l; q.y /= l; q.z /= l;
    QuatToMat(q, out->m);
    return true;
}

static const Entry* FindEntry(const Snapshot& s, DWORD obj, int kind) {
    for (int j = 0; j < s.n; j++) if (s.e[j].obj == obj && s.e[j].kind == kind) return &s.e[j];
    return nullptr;
}

// a camera's place in its car's own axes (a world vector v is v . row i of the car's matrix along axis i)
static const float kCamJump = 3.0f;               // m in one step against the car: a cut (smooth motion is under 0.5)
static bool RelPos(const Xform& cam, const Xform& car, float rel[3]) {
    if (!IsRotation(car.m)) return false;
    float d[3] = { cam.p[0]-car.p[0], cam.p[1]-car.p[1], cam.p[2]-car.p[2] };
    for (int i = 0; i < 3; i++) rel[i] = d[0]*car.m[i*3] + d[1]*car.m[i*3+1] + d[2]*car.m[i*3+2];
    return true;
}
static float RotAngle(const float* a, const float* b) {            // degrees between two rotations (-1: not rotations)
    if (!IsRotation(a) || !IsRotation(b)) return -1.0f;
    Quat qa = MatToQuat(a), qb = MatToQuat(b);
    float d = fabsf(qa.w*qb.w + qa.x*qb.x + qa.y*qb.y + qa.z*qb.z);
    return 2.0f * acosf(d > 1 ? 1 : d) * 57.29578f;
}

// ---------------------------------------------------------------------------
// Render wrapper
// ---------------------------------------------------------------------------
static Snapshot g_saved;          // live values while an interpolated frame is drawn
static Xform    g_written[kMaxEntries];
static bool     g_wrote[kMaxEntries];
static bool g_applied = false;
static bool g_slotsSwapped = false;
static DWORD g_liveSlot[4], g_drawSlot[4];
static bool g_lookSet[2] = { false, false };     // the look back flag was set for this frame (live value, value drawn with)
static DWORD g_lookLive[2], g_lookDraw[2];

// ---------------------------------------------------------------------------
// Look back trace (test builds). Every sim step and every frame drawn leaves a
// record for view 0: the look back flag, and where the camera is and how far
// it's turned against the car it follows (cam+4), in the car's own axes. For a
// step that's the game's own values right after the step; for a frame it's
// what was on screen (read after the render, before anything is put back),
// with the rotation the mod wrote next to the one the render left - they
// differ when the render works the camera's rotation out itself. When the
// flag changes, the records around it are written to the log (8 times at
// most), so a press and a release can be read step by step.
// ---------------------------------------------------------------------------
struct TraceRec { char kind, how; bool ok; long a, b; float t; DWORD flag, live, cam; float rel[3], ang, angW; };
static const int kTrace = 64, kTraceAfter = 28, kTraceLines = 44;
static TraceRec g_tr[kTrace];
static int g_trAt = 0, g_trLeft = -1, g_trDumps = 0;
static bool g_trFlag = false;
static long g_trFA = 0, g_trFB = 0; static float g_trFT = 0; static char g_trHow = 'l';
static bool CamVsCar(const Xform& cam, const Xform& car, float rel[3], float* ang) {
    if (!RelPos(cam, car, rel)) return false;
    *ang = RotAngle(car.m, cam.m);
    return true;
}
static void TraceAdd(const TraceRec& r) {
    g_tr[g_trAt % kTrace] = r; g_trAt++;
    if (g_trLeft <= 0 || --g_trLeft) return;
    int n = g_trAt < kTraceLines ? g_trAt : kTraceLines;
    LogfAsync("  trace %d: look back %s\n", g_trDumps + 1, g_trFlag ? "pressed" : "released");
    for (int k = g_trAt - n; k < g_trAt; k++) {
        const TraceRec& q = g_tr[k % kTrace];
        if (q.kind == 'S')
            LogfAsync("  trace step %ld: look back %lu, camera %08lX at (%.2f, %.2f, %.2f) from its car, %.0f deg against it%s\n",
                      q.a, (unsigned long)q.flag, (unsigned long)q.cam, q.rel[0], q.rel[1], q.rel[2], q.ang, q.ok ? "" : " (?)");
        else
            LogfAsync("  trace   frame %ld-%ld t=%.2f %s: look back %lu (live %lu), camera at (%.2f, %.2f, %.2f), %.0f deg (written %.0f)%s\n",
                      q.a, q.b, q.t, q.how == 'b' ? "blended" : q.how == 'n' ? "nearest" : q.how == 'l' ? "live" : "untouched",
                      (unsigned long)q.flag, (unsigned long)q.live, q.rel[0], q.rel[1], q.rel[2], q.ang, q.angW, q.ok ? "" : " (?)");
    }
    g_trAt = 0; g_trLeft = -1; g_trDumps++;
}
static void TraceStep(const Snapshot& s) {
    TraceRec r = {}; r.kind = 'S'; r.a = s.step; r.flag = r.live = s.look[0]; r.cam = s.activeCam[0]; r.ang = r.angW = -1;
    const Entry* ce = r.cam ? FindEntry(s, r.cam, 1) : nullptr;
    DWORD car = ce ? Rd<DWORD>(r.cam + hp::camCar) : 0;
    const Entry* ke = car ? FindEntry(s, car, 0) : nullptr;
    r.ok = ce && ke && CamVsCar(ce->x, ke->x, r.rel, &r.ang);
    bool flag = s.look[0] != 0;
    if (flag != g_trFlag) {
        g_trFlag = flag;
        if (g_trLeft < 0 && g_trDumps < 8) g_trLeft = kTraceAfter + 1;
    }
    TraceAdd(r);
}
static void TraceFrame() {                         // top of PostRender: what this frame was drawn with
    TraceRec r = {}; r.kind = 'F'; r.how = g_applied ? g_trHow : 'l'; r.a = g_trFA; r.b = g_trFB; r.t = g_trFT;
    r.flag = Rd<DWORD>(hp::pLookBack); r.live = g_lookSet[0] ? g_lookLive[0] : r.flag;
    r.cam = EffectiveCam(0); r.ang = r.angW = -1;
    Xform cx, kx;
    DWORD car = r.cam && Readable(r.cam + hp::camCar, 4) ? Rd<DWORD>(r.cam + hp::camCar) : 0;
    if (car && ReadXform(r.cam, 1, &cx) && ReadXform(car, 0, &kx)) {
        r.ok = CamVsCar(cx, kx, r.rel, &r.ang);
        if (g_applied)
            for (int i = 0; i < g_saved.n; i++)
                if (g_wrote[i] && g_saved.e[i].kind == 1 && g_saved.e[i].obj == r.cam) { r.angW = RotAngle(kx.m, g_written[i].m); break; }
    }
    TraceAdd(r);
}
static inline DWORD CamSlotAddr(int i) { return i < 2 ? hp::pCamActive + i * 4 : hp::pCamOverride + (i - 2) * 4; }
static HANDLE g_limTimer = nullptr;
static LONGLONG g_limNext = 0;
static std::atomic<unsigned long> g_renders{0}, g_extraRenders{0}, g_blended{0}, g_rejected{0}, g_cuts{0};
// written by the game thread, read and reset by the stats thread (approximate per-window stats)
static volatile double g_alphaSum = 0; static volatile unsigned long g_alphaN = 0, g_alphaClamped = 0;
static volatile bool g_clampHave = false; static SYSTEMTIME g_clampFirst;
static volatile double g_clampAheadMs = 0, g_clampBehindMs = 0;
static void NoteClamp(double steps) {             // >0: frame ahead of newest step, <0: behind history
    double ms = steps * 1000.0 / 64.0;
    if (!g_clampHave) { GetLocalTime(&g_clampFirst); g_clampHave = true; }
    if (ms > g_clampAheadMs) g_clampAheadMs = ms;
    if (-ms > g_clampBehindMs) g_clampBehindMs = -ms;
}
extern "C" { int g_extraEnabled = 0; DWORD g_fnSimStep = hp::fnSimStep, g_fnRender = hp::fnRender,
                                     g_fnMenuIdle = 0x4C9860; }

// Frames reach the screen on the display's schedule, not when the CPU happens
// to start them, so the time used for blending follows a smoothed frame clock.
static LONGLONG g_ftLast = 0, g_ftInterval = 0;
static LONGLONG FrameTime() {
    LONGLONG now = Qpc();
    if (!g_ftLast) { g_ftLast = now; return now; }
    LONGLONG d = now - g_ftLast;
    if (!g_ftInterval) g_ftInterval = d;
    LONGLONG pred = g_ftLast + g_ftInterval;
    LONGLONG err = now - pred;
    LONGLONG ft;
    if (g_ftInterval > 0 && err < g_ftInterval / 2 && err > -g_ftInterval / 2) {
        ft = pred + err / 8;                                  // on schedule: follow gently
        g_ftInterval += (d - g_ftInterval) / 16;
    } else {
        ft = now;                                             // hitch / rate change: resync
        if (d > 0 && d < g_qpf.QuadPart / 10) g_ftInterval = d;
    }
    g_ftLast = ft;
    return ft;
}

static void LimitTo(int fps, LONGLONG& next) {
    if (fps <= 0) return;
    LONGLONG period = g_qpf.QuadPart / fps, now = Qpc();
    if (next && now < next) { WaitUntilOn(g_limTimer, next); now = Qpc(); }
    if (!next || now - next > period) next = now + period; else next += period;
}

static LONGLONG g_menuNext = 0;
static std::atomic<unsigned long> g_menuFrames{0};
static std::atomic<long> g_menuGapMaxUs{0};       // longest time between two menu frames
// Menu frame ceiling: auto = the highest rate <= 128 that divides the refresh rate
// (re-read every 2 s, the game may change display mode).
// The display's refresh rate, re-read every 2 s (the game may change display mode).
// EnumDisplaySettings can take milliseconds (under Wine it asks the X server),
// so it's never called on the game thread after the first time: a background
// thread refreshes the value every 2 s.
static std::atomic<int> g_displayHz{0};
static int QueryDisplayHz() {
    DEVMODEA dm = {}; dm.dmSize = sizeof(dm);
    int hz = 0;
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) hz = (int)dm.dmDisplayFrequency;
    if (hz < 30 || hz > 1000) hz = 60;                     // "default"/unknown
    return hz;
}
static DWORD WINAPI DisplayThread(LPVOID) {
    for (;;) { g_displayHz = QueryDisplayHz(); Sleep(2000); }
}
static int DisplayHz() {
    int hz = g_displayHz.load();
    if (!hz) {                                             // first use, before the thread ran
        hz = QueryDisplayHz(); g_displayHz = hz;
        static bool started = false;
        if (!started) { started = true; if (HANDLE th = StartModThread(DisplayThread)) CloseHandle(th); }
    }
    return hz;
}
// targetFPS=-1: a limit that never fights vsync. A limiter at exactly the
// refresh rate on top of vsync beats against it (its clock and the display's
// drift apart: frames start late, miss a refresh, and blending stutters). So
// the limit starts at 1.5x the refresh rate, which vsync never reaches, and
// only if frames then run faster than the refresh rate (no vsync: software
// renderer, or vsync off) does it drop to exactly the refresh rate.
static int  g_autoMode = 0;          // 0 = not known yet / vsync, 1 = no vsync: limit to the refresh rate
static LONGLONG g_autoWin = 0; static unsigned long g_autoFrames = 0;
static int RaceFPS() {
    if (g_cfg.targetFPS >= 0) return g_cfg.targetFPS;
    int hz = DisplayHz();
    return g_autoMode ? hz : hz * 3 / 2;
}
static void AutoLimitCheck() {        // once per race frame, auto mode only
    if (g_cfg.targetFPS >= 0 || g_autoMode) return;
    LONGLONG now = Qpc();
    if (!g_autoWin) { g_autoWin = now; g_autoFrames = 0; return; }
    g_autoFrames++;
    LONGLONG d = now - g_autoWin;
    if (d < 2 * g_qpf.QuadPart) return;
    double fps = g_autoFrames * (double)g_qpf.QuadPart / (double)d;
    int hz = DisplayHz();
    if (fps > hz * 1.1) {
        g_autoMode = 1;
        LogfAsync("limiter: %.0f fps on a %d Hz display - no vsync, frames limited to %d fps\n", fps, hz, hz);
    }
    g_autoWin = now; g_autoFrames = 0;
}
static void Limiter() { AutoLimitCheck(); LimitTo(RaceFPS(), g_limNext); }
static int MenuCeiling() {
    if (g_cfg.menuMaxFPS >= 0) return g_cfg.menuMaxFPS;
    static int cached = 0, lastHz = 0;
    int hz = DisplayHz();
    if (hz != lastHz) {
        lastHz = hz;
        // highest rate <= 128 that divides the refresh rate (even pacing), not
        // below 50; otherwise the first rate <= 128 (e.g. 165 Hz -> 55, 144 -> 72)
        int c = 0, first = 0;
        for (int n = 1; n <= 64; n++) {
            int r = hz / n;
            if (r > 128) continue;
            if (!first) first = r;
            if (r < 50) break;
            if (hz % n == 0) { c = r; break; }
        }
        if (!c) c = first ? first : 60;
        if (c != cached) { cached = c; LogfAsync("menus: frame ceiling %d fps (display %d Hz)\n", c, hz); }
    }
    return cached;
}
// The game's menu wait called the menu idle work (0x4C9860) over and over
// while it waited, and that work refills the front-end music/speech stream one
// chunk per call (0x4C9380). Called once per frame, a stream drained by a slow
// frame (switching menus) refilled too slowly and crackled. So it runs a few
// times every frame (cheap when the stream is full), and the menu frame limit
// waits in ~1 ms slices with the idle work in between, as the game did.
extern "C" void CallMenuIdle();
static void MenuLimitTo(int fps, LONGLONG& next) {
    for (int i = 0; i < 3; i++) CallMenuIdle();       // + the stub's call = 4 per frame
    if (fps <= 0) return;
    LONGLONG period = g_qpf.QuadPart / fps, now = Qpc();
    if (next && now < next) {
        LONGLONG twoMs = g_qpf.QuadPart / 500;
        for (;;) {
            if (next - now > twoMs) Sleep(1);           // coarse slice, then idle work
            else { WaitUntilOn(g_limTimer, next); break; }
            CallMenuIdle();
            now = Qpc();
            if (now >= next) break;
        }
        now = Qpc();
    }
    if (!next || now - next > period) next = now + period; else next += period;
}
extern "C" void __cdecl MenuFrame() {
    int fps = g_cfg.menuTargetFPS < 0 ? RaceFPS() : g_cfg.menuTargetFPS;
    int cap = MenuCeiling();
    if (cap > 0 && (fps <= 0 || fps > cap)) fps = cap;
    MenuLimitTo(fps, g_menuNext);
    g_menuFrames++;
    static LONGLONG lastFrame = 0;                // game thread only
    LONGLONG now = Qpc();
    if (lastFrame) {
        LONGLONG us = (now - lastFrame) * 1000000 / g_qpf.QuadPart;
        if (us < 5000000 && us > g_menuGapMaxUs.load()) g_menuGapMaxUs = (long)us;   // (longer: the menus weren't running)
    }
    lastFrame = now;
}

// A frame that isn't blended is drawn from the live values. For a camera whose
// rotation the render works on in place, those aren't the simulation's any
// more once a frame has been drawn: the cockpit camera's head movement
// (0x421730, cam+0x104 set) multiplies the matrix at cam+0x2C by a small
// rotation at every render, where the game drew once per step. A second frame
// on the same step turned it twice - the picture shook, most with the software
// renderer, where many frames come after the newest step. So such a frame
// first gets the newest step's own values back for the cameras in use.
static void CamsToNewest() {
    if (g_ringN < 1) return;
    const Snapshot& nb = g_ring[g_head];
    // (only while that snapshot is the step the game is on: at the start of the
    // next race the ring still holds the last race's, until its first step)
    long cur = Rd<long>(hp::pSimStepIdx);           // (the step's own index until the game counts on, then one more)
    if (nb.step != cur && nb.step + 1 != cur) return;
    for (int v = 0; v < 2; v++) {
        DWORD cam = EffectiveCam(v);
        if (!cam || nb.activeCam[v] != cam) continue;        // (switched since the step: left alone)
        if (v == 1 && cam == nb.activeCam[0]) continue;
        if (const Entry* e = FindEntry(nb, cam, 1)) WriteXform(cam, 1, e->x);
    }
}
extern "C" void __cdecl PzLimitsCheck();
extern "C" void __cdecl PreRender() {
    Limiter();
    g_renders++;
    PzLimitsCheck();
    g_applied = false;
    if (!g_cfg.interpolate || g_ringN < 2 || !g_clockValid) { CamsToNewest(); return; }

    // Position on the smoothed sim timeline, one step (interpDelay) behind.
    LONGLONG p = StepPeriodQpc();
    double r = (double)(FrameTime() - g_clockT0) / (double)p - g_cfg.interpDelay;
    // Find consecutive snapshots A.step <= r < B.step (newest first).
    const Snapshot* A = nullptr; const Snapshot* B = nullptr;
    for (int k = 0; k + 1 < g_ringN; k++) {
        const Snapshot& nb = g_ring[(g_head - k + kRing) % kRing];
        const Snapshot& na = g_ring[(g_head - k - 1 + kRing) % kRing];
        if (k == 0 && r >= (double)nb.step) {
            g_alphaClamped++; g_alphaSum += 1; g_alphaN++; NoteClamp(r - (double)nb.step); CamsToNewest(); return;
        }
        if (r >= (double)na.step) { A = &na; B = &nb; break; }
    }
    if (!A) {                                        // older than the history: oldest pair
        int k = g_ringN - 2;
        B = &g_ring[(g_head - k + kRing) % kRing];
        A = &g_ring[(g_head - k - 1 + kRing) % kRing];
        g_alphaClamped++;
        NoteClamp(r - (double)A->step);
        r = (double)A->step;
    }
    long span = B->step - A->step;
    if (span <= 0 || span > 8) { CamsToNewest(); return; }
    float t = (float)((r - (double)A->step) / (double)span);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    g_alphaSum += t; g_alphaN++;
    g_trFA = A->step; g_trFB = B->step; g_trFT = t; g_trHow = 'x';

    // Every object is drawn at the same moment r. An object that can't be
    // blended across this pair (a camera cut, looking back, a reset) takes the
    // nearer of the two snapshots instead of its live value: the live values
    // are the newest step, up to two steps ahead of r, and showing them for the
    // cut frame and then going back to r made a visible back-and-forth jump.
    // A camera switch after B (the game already uses another camera) is drawn
    // with B's camera until r reaches the switch: the camera slots are set to
    // B's for this frame and put back afterwards.
    // The look back flag is set once per pass of the main loop, but the frame
    // shows a moment one to two steps earlier: the render's own uses of the flag
    // (cockpit picture, mirror, per-view caches) would switch that much before
    // the camera drawn does. So for this frame the flag is the one the snapshot
    // nearer to r was made with - the same choice as for a camera that can't be
    // blended - and it's put back afterwards.
    for (int v = 0; v < 2; v++) {
        DWORD live = Rd<DWORD>(hp::pLookBack + v * 4), want = (t < 0.5f ? A : B)->look[v];
        g_lookSet[v] = want != live;
        if (g_lookSet[v]) { g_lookLive[v] = live; g_lookDraw[v] = want; *(volatile DWORD*)(hp::pLookBack + v * 4) = want; }
    }
    bool cut = false;
    g_slotsSwapped = false;
    for (int i = 0; i < 4; i++) {
        g_liveSlot[i] = Rd<DWORD>(CamSlotAddr(i));
        g_drawSlot[i] = g_liveSlot[i];
        if (B->camSlot[i] != g_liveSlot[i] && (B->camSlot[i] || i >= 2)) {
            g_drawSlot[i] = B->camSlot[i];
            *(volatile DWORD*)CamSlotAddr(i) = B->camSlot[i];
            g_slotsSwapped = true;
        }
    }
    DWORD liveCam[2] = { EffectiveCam(0), EffectiveCam(1) };
    for (int v = 0; v < 2; v++)
        if (A->activeCam[v] != B->activeCam[v] || liveCam[v] != B->activeCam[v]) cut = true;

    // Current live values (normally == the newest step) are saved and restored after drawing.
    Collect(g_saved);
    for (int i = 0; i < g_saved.n; i++) {
        g_wrote[i] = false;
        const Entry& cur = g_saved.e[i];
        bool active = cur.kind == 1 && (cur.obj == liveCam[0] || cur.obj == liveCam[1]);
        if (active && cur.obj != B->activeCam[0] && cur.obj != B->activeCam[1]) continue;   // new: live
        const Entry* pb = FindEntry(*B, cur.obj, cur.kind);
        if (!pb) continue;
        const Entry* pa = FindEntry(*A, cur.obj, cur.kind);
        // switched to between A and B: its A snapshot is from before it was in use
        bool switched = active && cur.obj != A->activeCam[0] && cur.obj != A->activeCam[1];
        Xform x;
        // A camera that jumps against the car it follows is a cut, however far
        // it moved in the world. Looking back puts a chase camera at the other
        // end of the car, 10.3 m away: with the car's own travel that's just
        // over maxStepDistance on the press (a cut) and just under it on the
        // release - which was blended, and swept the camera through the car
        // over two frames, turning round on the way.
        bool relCut = false;
        if (active && pa && !switched && Readable(cur.obj + hp::camCar, 4)) {
            DWORD car = Rd<DWORD>(cur.obj + hp::camCar);
            const Entry* ka = car ? FindEntry(*A, car, 0) : nullptr;
            const Entry* kb = car ? FindEntry(*B, car, 0) : nullptr;
            float ra[3], rb[3];
            if (ka && kb && RelPos(pa->x, ka->x, ra) && RelPos(pb->x, kb->x, rb)) {
                float dx = rb[0]-ra[0], dy = rb[1]-ra[1], dz = rb[2]-ra[2];
                relCut = dx*dx + dy*dy + dz*dz > kCamJump * kCamJump;
            }
        }
        if (pa && !switched && !relCut && Blend(pa->x, pb->x, t, &x)) { g_blended++; if (active && cur.obj == liveCam[0]) g_trHow = 'b'; }
        else {
            if (active && cur.obj == liveCam[0]) g_trHow = 'n';
            const Snapshot* X = (pa && !switched && t < 0.5f) ? A : B;
            x = X == A ? pa->x : pb->x;
            g_rejected++;
            if (active) {
                // The camera can't be blended (looking back moves it to the
                // other end of the car one step and turns it the next), so
                // it's taken from the nearer snapshot - but moved along with
                // the car it follows, which is drawn at r: otherwise the car
                // jumps back and forth under it on these frames.
                const Entry* folw = nullptr; float best = 1e30f;
                for (int j = 0; j < X->n; j++) {
                    const Entry& c = X->e[j];
                    if (c.kind != 0) continue;
                    float dx = c.x.p[0]-x.p[0], dy = c.x.p[1]-x.p[1], dz = c.x.p[2]-x.p[2];
                    float d = dx*dx + dy*dy + dz*dz;
                    if (d < best) { best = d; folw = &c; }
                }
                if (folw && best < 60.0f * 60.0f)
                    for (int j = 0; j < i; j++)
                        if (g_wrote[j] && g_saved.e[j].kind == 0 && g_saved.e[j].obj == folw->obj) {
                            for (int k = 0; k < 3; k++) x.p[k] += g_written[j].p[k] - folw->x.p[k];
                            break;
                        }
                cut = true;
            }
        }
        WriteXform(cur.obj, cur.kind, x);
        g_written[i] = x; g_wrote[i] = true;
    }
    if (cut) g_cuts++;
    g_applied = true;
}

extern "C" void __cdecl PostRender() {
    if (kTestBuild) TraceFrame();
    for (int v = 0; v < 2; v++)                  // the live look back flag back, unless the render changed it
        if (g_lookSet[v]) {
            if (Rd<DWORD>(hp::pLookBack + v * 4) == g_lookDraw[v]) *(volatile DWORD*)(hp::pLookBack + v * 4) = g_lookLive[v];
            g_lookSet[v] = false;
        }
    if (g_slotsSwapped) {      // the game's camera slots back, unless the render changed one
        for (int i = 0; i < 4; i++)
            if (Rd<DWORD>(CamSlotAddr(i)) == g_drawSlot[i]) *(volatile DWORD*)CamSlotAddr(i) = g_liveSlot[i];
        g_slotsSwapped = false;
    }
    if (!g_applied) return;
    // Put the simulation values back - unless the render itself changed them
    // (e.g. a camera switch re-initialises the new camera). Position and
    // rotation are judged separately: the render works out the rotation of a
    // camera that looks at its car itself (0x422290, from the position drawn),
    // and that used to leave such a camera's position at the frame's blended
    // value too, a step or more behind the simulation's, until the next step.
    for (int i = 0; i < g_saved.n; i++) {
        if (!g_wrote[i]) continue;
        const Entry& e = g_saved.e[i];
        Xform now;
        if (!ReadXform(e.obj, e.kind, &now)) continue;
        if (!memcmp(now.p, g_written[i].p, 12)) memcpy((void*)(e.obj + PosOff(e.kind)), e.x.p, 12);
        if (!memcmp(now.m, g_written[i].m, 36)) memcpy((void*)(e.obj + RotOff(e.kind)), e.x.m, 36);
    }
    g_applied = false;
}

extern "C" void __cdecl NoteExtraRender() { g_extraRenders++; }

// ---------------------------------------------------------------------------
// Loading bar throttle
// ---------------------------------------------------------------------------
extern "C" { int g_loadDraw = 1; DWORD g_fnPoll = 0x4E7630; int g_wsLoading = 0; DWORD g_xfTarget = 0; }
static LONGLONG g_loadLastCall = 0, g_loadLastDraw = 0, g_loadStart = 0;
static unsigned long g_loadCalls = 0, g_loadDraws = 0;
static bool g_loadActive = false;
static CRITICAL_SECTION g_loadLock;

extern "C" void __cdecl LoadBarDecide(float pct) {
    LONGLONG now = Qpc();
    EnterCriticalSection(&g_loadLock);
    if (!g_loadActive || now - g_loadLastCall > 2 * g_qpf.QuadPart) {   // new load
        g_loadActive = true; g_loadStart = now; g_loadCalls = 0; g_loadDraws = 0; g_loadLastDraw = 0;
    }
    g_loadLastCall = now;
    g_loadCalls++;
    LONGLONG minGap = (LONGLONG)g_cfg.loadScreenRenderMs * g_qpf.QuadPart / 1000;
    bool draw = g_cfg.loadScreenRenderMs <= 0 || !g_loadLastDraw || pct >= 100.0f ||
                now - g_loadLastDraw >= minGap;
    if (draw) { g_loadLastDraw = now; g_loadDraws++; }
    if (!g_cfg.speedUpLoading) draw = true;      // stub there only for wsLoading
    g_loadDraw = draw ? 1 : 0;
    LeaveCriticalSection(&g_loadLock);
}
static void LoadStatsTick() {                 // called by the stats thread
    EnterCriticalSection(&g_loadLock);
    if (g_loadActive && Qpc() - g_loadLastCall > 2 * g_qpf.QuadPart) {
        Logf("load: %lu loading-bar updates, %lu drawn, first->last %.0f ms\n", g_loadCalls,
             g_loadDraws, QpcMs(g_loadLastCall - g_loadStart));
        g_loadActive = false;
    }
    LeaveCriticalSection(&g_loadLock);
}

// ---------------------------------------------------------------------------
// install.win: drive-letter folders -> game folder
// ---------------------------------------------------------------------------
static void FixInstallPaths() {
    int fixed = 0;
    for (int i = 0; i < hp::installLines; i++) {
        char* l = (char*)(hp::pInstallLines + i * hp::installLineLen);
        if (!(((l[0] | 0x20) >= 'a' && (l[0] | 0x20) <= 'z') && l[1] == ':' &&
              (l[2] == '\\' || l[2] == '/') && l[3])) continue;
        char local[MAX_PATH];
        if (snprintf(local, sizeof(local), "%s%s", g_gameDir, l + 3) >= (int)sizeof(local)) continue;
        DWORD a = GetFileAttributesA(local);
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
            Logf("install.win: %s kept (no %s in the game folder)\n", l, l + 3);
            continue;
        }
        char was[hp::installLineLen];
        memcpy(was, l, sizeof(was)); was[sizeof(was) - 1] = 0;
        l[0] = '.'; l[1] = '\\';
        memmove(l + 2, l + 3, strnlen(l + 3, hp::installLineLen - 3) + 1);   // "X:\a" -> ".\a"
        Logf("install.win: %s -> %s\n", was, l);
        fixed++;
    }
    if (fixed) Logf("install.win: %d folder(s) read from the game folder\n", fixed);
}

// A full install (the floppy's install.win with the CD folders local)
static const char* const kLangNames[6] = { "english", "german", "french", "spanish", "italian", "swedish" };
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

// After the reader (0x4A6E10). ok = its result: 1 = read, 0 = no install.win.
// (A file with an unknown language never gets here: the reader quits itself.)
extern "C" int __cdecl AfterInstallRead(int ok) {
    if (!ok && g_cfg.installWinFallback) {
        int lang = -1;
        for (int i = 0; i < 6; i++) if (!_stricmp(g_cfg.language, kLangNames[i])) lang = i;
        if (lang < 0) { Logf("install.win: language=%s unknown - english used\n", g_cfg.language); lang = 0; }
        char* t = (char*)hp::pInstallLines;
        memset(t, 0, hp::installLines * hp::installLineLen);
        lstrcpynA(t, kLangNames[lang], hp::installLineLen);
        lstrcpynA(t + hp::installLineLen, "local", hp::installLineLen);
        int n = 2;
        for (const char* l : kFullInstall) lstrcpynA(t + n++ * hp::installLineLen, l, hp::installLineLen);
        *(volatile DWORD*)hp::pLanguageIndex = (DWORD)lang;
        Logf("install.win: not found - built-in full install used (%s, %d folders)\n", kLangNames[lang], n - 2);
        return 1;
    }
    if (ok && g_cfg.portableInstallPaths) FixInstallPaths();
    return ok;
}

// ---------------------------------------------------------------------------
// Sound service timing (diagnostics): the sound thread is woken on every 128 Hz
// tick to top up the DirectSound buffer. A late or slow top-up = a crackle.
// ---------------------------------------------------------------------------
extern "C" void SoundServeStub();
extern "C" void WsProjStub();
extern "C" void WsZoomStub();
extern "C" void HorizonRowStub();
extern "C" void HorizonStub0();
extern "C" void HorizonStub1();
extern "C" void HorizonStub2();
extern "C" void TexSizeStub();
extern "C" void XfWrapCockpit();
extern "C" void XfNeedlesStub();
extern "C" void HudModeStub();
extern "C" void HudRectStub();
extern "C" void HudFracStub();
extern "C" { int g_hrPlayer, g_hrIdx, g_hrW, g_hrH; DWORD g_fnRectBody = 0x480915; }
extern "C" { int g_hudW = 640; DWORD g_fnSetModeBody = 0x4BEF56; }
static bool g_hudIn = false;                      // the HUD's sizes come from g_hudW (InstallHud)
static bool g_pzMeasIn = false;                   // the in-race menus measure their strings by it too (InstallMenus)
extern "C" { extern float g_pzMeasF; }
extern "C" void MirrorRectStub();
extern "C" void MirrorProjStub();
extern "C" { int g_wsMirror = 0, g_mirrorLayout = 0; DWORD g_fnViewRectsTail = 0x4BE3D0, g_fnRectProj = 0x4BED20; }
static bool g_mirrorNarrowed = false;             // the mirror's rect as it stands was narrowed (MirrorRectFix)
extern "C" { int g_xfNeedle = 0; DWORD g_fnNeedles = 0x4880B0; }
extern "C" void XfWrapLoadScreen();
extern "C" void ModeFilterStub();
extern "C" void D3dFogStub();
static LONGLONG g_sndLast = 0, g_sndStart = 0;
static std::atomic<unsigned long> g_sndCalls{0};
static std::atomic<long> g_sndMaxGapUs{0}, g_sndMaxRunUs{0}, g_sndLateGaps{0};
static volatile LONG* g_eacLead = nullptr;     // eacsnd.dll's lead in samples (+0xA8CC) and rate (+0xA900), set by PatchEacsnd
static volatile LONG* g_eacRate = nullptr;
static LONG g_eacLastLead = 0;
static std::atomic<long> g_sndUnderruns{0};
// soundCapture (diagnostics): eacsnd.dll gets every block of sound from the
// game's mixer through a callback (the first of the 8 it's given, stored at
// +0xA904: stdcall (buffer, frames), called from its fill loop 0x2110). MixHook
// stands in for it, while a capture is on, and keeps a copy of what the mixer
// wrote.
typedef void (__stdcall *MixFn_t)(void*, int);
static void** g_eacMixSlot = nullptr;             // eacsnd.dll +0xA904, set by PatchEacsnd
static void** g_eacBuf = nullptr;                 // eacsnd.dll +0xA8A8: its output buffer
static MixFn_t g_mixOrig = nullptr;
// The copy goes into
// memory on the sound thread and from there to HPFrameFix_mix.wav on the log
// writer thread, within a second, so the sound thread never waits on the disk.
static BYTE* g_cap = nullptr;                     // capture memory (VirtualAlloc)
static DWORD g_capSize = 0;                       // its size in bytes
static std::atomic<DWORD> g_capLen{0};            // bytes filled so far
static DWORD g_capRate = 0; static int g_capCh = 0, g_capBits = 0;
static bool g_capOff = false;                     // full, failed, or the format changed
static int CaptureSeconds() { return g_cfg.soundCapture < 0 ? (kTestBuild ? 120 : 0) : g_cfg.soundCapture > 600 ? 600 : g_cfg.soundCapture; }
static void CaptureBlock(const void* src, int frames, DWORD rate, int ch, int bits) {   // sound thread
    if (g_capOff) return;
    if (!g_cap) {
        int sec = CaptureSeconds();
        if (sec <= 0 || !rate) { g_capOff = true; return; }
        g_capSize = (DWORD)sec * rate * ch * (bits / 8);
        g_cap = (BYTE*)VirtualAlloc(nullptr, g_capSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_cap) { g_capOff = true; return; }
        g_capRate = rate; g_capCh = ch; g_capBits = bits;
        LogfAsync("sound capture: the first %d s of the game's mix go to HPFrameFix_mix.wav (%lu Hz, %d channel(s), %d bit)\n",
                  sec, (unsigned long)rate, ch, bits);
    }
    if (rate != g_capRate || ch != g_capCh || bits != g_capBits) { g_capOff = true; return; }
    DWORD n = (DWORD)frames * ch * (bits / 8), at = g_capLen.load();
    if (n > g_capSize - at) { n = g_capSize - at; g_capOff = true; }
    memcpy(g_cap + at, src, n);
    g_capLen = at + n;
}
static void CaptureFlush() {                      // log writer thread only
    static FILE* f = nullptr; static DWORD done = 0; static bool failed = false;
    DWORD len = g_capLen.load();
    if (failed || !g_cap || len == done) return;
    if (!f) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%sHPFrameFix_mix.wav", g_asiDir);
        f = fopen(path, "wb");
        if (!f) { failed = true; return; }
        BYTE h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0 };
        WORD ch = (WORD)g_capCh, bits = (WORD)g_capBits, align = (WORD)(g_capCh * g_capBits / 8);
        DWORD rate = g_capRate, bps = rate * align;
        memcpy(h + 22, &ch, 2); memcpy(h + 24, &rate, 4); memcpy(h + 28, &bps, 4);
        memcpy(h + 32, &align, 2); memcpy(h + 34, &bits, 2); memcpy(h + 36, "data", 4);
        fwrite(h, 1, 44, f);
    }
    fseek(f, 44 + (long)done, SEEK_SET);
    fwrite(g_cap + done, 1, len - done, f);
    done = len;
    DWORD riff = 36 + len;
    fseek(f, 4, SEEK_SET);  fwrite(&riff, 4, 1, f);
    fseek(f, 40, SEEK_SET); fwrite(&len, 4, 1, f);
    fflush(f);
}
static void __stdcall MixHook(void* dst, int frames) {
    g_mixOrig(dst, frames);
    static void* fmtOf = nullptr; static int ch = 0, bits = 0;   // sound thread only
    static DWORD rate = 0;
    void* buf = g_eacBuf ? *g_eacBuf : nullptr;
    if (!buf || frames <= 0 || frames > 65536) return;
    if (buf != fmtOf) {                           // a new buffer: its format (IDirectSoundBuffer::GetFormat)
        BYTE wf[18] = {};
        typedef HRESULT (__stdcall *GetFmt_t)(void*, void*, DWORD, DWORD*);
        fmtOf = buf; ch = 0;
        if (((GetFmt_t)(*(void***)buf)[0x14 / 4])(buf, wf, sizeof wf, nullptr) >= 0) {
            ch = *(WORD*)(wf + 2); bits = *(WORD*)(wf + 14); rate = *(DWORD*)(wf + 4);
            if (ch < 1 || ch > 2 || (bits != 8 && bits != 16)) ch = 0;
        }
    }
    if (ch) CaptureBlock(dst, frames, rate, ch, bits);
}
extern "C" void __cdecl SoundServeBefore() {
    if (g_eacMixSlot && *g_eacMixSlot && *g_eacMixSlot != (void*)&MixHook) {   // (set, or set again, by the game)
        g_mixOrig = (MixFn_t)*g_eacMixSlot;
        *g_eacMixSlot = (void*)&MixHook;
    }
    LONGLONG now = Qpc();
    if (g_sndLast) {
        long gap = (long)((now - g_sndLast) * 1000000 / g_qpf.QuadPart);
        if (gap > g_sndMaxGapUs.load()) g_sndMaxGapUs = gap;
        if (gap > 15000) g_sndLateGaps++;
    }
    g_sndLast = now; g_sndStart = now;
}
extern "C" void __cdecl SoundServeAfter() {
    long run = (long)((Qpc() - g_sndStart) * 1000000 / g_qpf.QuadPart);
    if (run > g_sndMaxRunUs.load()) g_sndMaxRunUs = run;
    g_sndCalls++;
    if (g_eacLead) {                             // the DLL only grows its lead when the play cursor overtook the data: an underrun
        LONG l = *g_eacLead;
        if (g_eacLastLead && l > g_eacLastLead) g_sndUnderruns++;
        g_eacLastLead = l;
    }
}
static void InstallSoundTiming() {
    if (!g_knownExe) return;
    DWORD imm = (DWORD)&SoundServeStub;
    BYTE b[5] = { 0xB8 }; memcpy(b + 1, &imm, 4);
    Logf("sound timing: %s\n", PatchBytes(0x4E232C, b, 5) ? "logged" : "patch FAILED");
}

// ---------------------------------------------------------------------------
// Movie stream thread: let it finish exiting before KillThread
// ---------------------------------------------------------------------------
// ph = KillThread's argument (the stream struct's thread handle, on the heap);
// KillThread itself treats 0 and -1 as "no thread" and reads *ph otherwise.
extern "C" void __cdecl WaitStreamThread(HANDLE* ph) {
    if (!ph || ph == (HANDLE*)-1 || !*ph) return;
    if (WaitForSingleObject(*ph, 2000) == WAIT_TIMEOUT)
        LogfAsync("movies: stream thread still running after 2 s - killed as before\n");
}

// ---------------------------------------------------------------------------
// Stubs (Watcom code: preserve every register and the flags)
// ---------------------------------------------------------------------------
extern "C" void StepStub();
extern "C" void RenderStub();
extern "C" void ExtraRenderStub();
extern "C" void MenuWaitStub();
extern "C" void LoadBarStub();
extern "C" void InstallReadStub();
extern "C" void StreamKillStub();
extern "C" void SoundServeStub();
asm(".text\n"
    // call site of the sim step: run it (eax = step index, untouched), then snapshot
    ".globl _StepStub\n"
    "_StepStub:\n"
    "    call *_g_fnSimStep\n"
    "    pushfl\n"
    "    pushal\n"
    "    call _OnSimStep\n"
    "    popal\n"
    "    popfl\n"
    "    ret\n"
    // call site of the render: eax = argument
    ".globl _RenderStub\n"
    "_RenderStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    call _PreRender\n"
    "    popal\n"
    "    popfl\n"
    "    call *_g_fnRender\n"
    "    pushfl\n"
    "    pushal\n"
    "    call _PostRender\n"
    "    popal\n"
    "    popfl\n"
    "    ret\n"
    // replaces "cmp [0x7D3674],0 / jne 0x4C548E" (jumped to). edi = the game's
    // "first frame pending" local: left to the game.
    ".globl _ExtraRenderStub\n"
    "_ExtraRenderStub:\n"
    "    cmpl $0, 0x7D3674\n"
    "    jne 1f\n"
    "    pushl $0x4C5477\n"           // new step: original render decision
    "    ret\n"
    "1:  cmpl $0, _g_extraEnabled\n"
    "    je 2f\n"
    "    testl %edi, %edi\n"
    "    jne 2f\n"
    "    pushfl\n"
    "    pushal\n"
    "    call _NoteExtraRender\n"
    "    popal\n"
    "    popfl\n"
    "    movl 0x7D36A0, %eax\n"
    "    call _RenderStub\n"
    "2:  pushl $0x4C548E\n"
    "    ret\n"
    // replaces the menu's "wait for 2 ticks" loop (jumped to, inside 0x4D9140)
    ".globl _MenuWaitStub\n"
    "_MenuWaitStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    call *_g_fnMenuIdle\n"       // the work the wait loop used to do
    "    call _MenuFrame\n"           // optional limiter
    "    popal\n"
    "    popfl\n"
    "    movl 0x56415C, %eax\n"       // registers as the original loop leaves them
    "    addl $2, %eax\n"
    "    movl 0x79C1A4, %edx\n"
    "    pushl $0x4D9172\n"           // mov [0x56415C], edx
    "    ret\n"
    // the menu idle work (Watcom: may clobber eax-edx), callable from C
    ".globl _CallMenuIdle\n"
    "_CallMenuIdle:\n"
    "    pushal\n"
    "    call *_g_fnMenuIdle\n"
    "    popal\n"
    "    ret\n"
    // entry of the loading bar (stack: ret, float percent)
    ".globl _LoadBarStub\n"
    "_LoadBarStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    pushl 40(%esp)\n"            // percent
    "    call _LoadBarDecide\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    popfl\n"
    "    cmpl $0, _g_loadDraw\n"
    "    jne 3f\n"
    "    pushal\n"                    // skipped: keep the callback poll it starts with
    "    xorl %eax, %eax\n"
    "    call *_g_fnPoll\n"
    "    popal\n"
    "    ret $4\n"
    "3:  cmpl $0, _g_wsLoading\n"
    "    jne 5f\n"
    "4:  pushl %ebx\n"                 // original prologue, then continue
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    pushl %ebp\n"
    "    pushl $0x495236\n"
    "    ret\n"
    "5:  pushal\n"                    // drawn 4:3: the bar inside XfBegin/XfEnd
    "    pushl $1\n"
    "    call _XfBegin\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    pushl 4(%esp)\n"              // percent again, for the call below
    "    call 4b\n"                   // the original (ret 4 drops the copy)
    "    pushal\n"
    "    call _XfEnd\n"
    "    popal\n"
    "    ret $4\n"
    // a game function called with XfBegin(mode) / XfEnd around it, all
    // registers passed through: the call site's "call target" becomes
    // "call _XfWrapN" (N = which).
    ".globl _XfWrapCockpit\n"
    "_XfWrapCockpit:\n"
    "    pushal\n"
    "    pushl $2\n"
    "    call _XfBegin\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movl $0x489240, _g_xfTarget\n"
    "    jmp 6f\n"
    // SetVideoMode (0x4BEF50, eax = mode): its prologue here, then its body; on
    // the way back the HUD width is updated for the new screen
    ".globl _HudModeStub\n"
    "_HudModeStub:\n"
    "    call 1f\n"
    "    pushal\n"
    "    call _HudUpdate\n"
    "    popal\n"
    "    ret\n"
    "1:  pushl %ebx\n"
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    pushl %ebp\n"
    "    jmp *_g_fnSetModeBody\n"
    // HUD positions being changed (0x459EBD: lea edi, [ecx+eax+0x6FBC74]; the
    // item at [ebp-4]): its fractions taken back through the boxes' rule first
    ".globl _HudFracStub\n"
    "_HudFracStub:\n"
    "    pushal\n"
    "    pushl -4(%ebp)\n"
    "    call _HudFracInverse\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    leal 0x6FBC74(%ecx,%eax), %edi\n"
    "    ret\n"
    // HUD box placer (0x480910: eax = player, edx = box, ebx = view w, ecx = h)
    ".globl _HudRectStub\n"
    "_HudRectStub:\n"
    "    movl %eax, _g_hrPlayer\n"
    "    movl %edx, _g_hrIdx\n"
    "    movl %ebx, _g_hrW\n"
    "    movl %ecx, _g_hrH\n"
    "    call 1f\n"
    "    pushal\n"
    "    call _HudRectPost\n"
    "    popal\n"
    "    ret\n"
    "1:  pushl %esi\n"
    "    pushl %edi\n"
    "    pushl %ebp\n"
    "    movl %esp, %ebp\n"
    "    jmp *_g_fnRectBody\n"
    // view rects (0x4BE5E0; edi = its argument, the layout: 0 = outside views,
    // 1 = cockpit), its last call (0x4BE8C4, call 0x4BE3D0): the mirror's rect
    // is set by then, and is narrowed first
    ".globl _MirrorRectStub\n"
    "_MirrorRectStub:\n"
    "    movl %edi, _g_mirrorLayout\n"
    "    pushal\n"
    "    call _MirrorRectFix\n"
    "    popal\n"
    "    jmp *_g_fnViewRectsTail\n"
    // ViewSetup, mirror (0x4DBE0B, call 0x4BED20; eax = rect, edx = fov): the
    // projection is set up with g_wsMirror set, so it keeps the game's own angle
    ".globl _MirrorProjStub\n"
    "_MirrorProjStub:\n"
    "    movl $1, _g_wsMirror\n"
    "    call *_g_fnRectProj\n"
    "    movl $0, _g_wsMirror\n"
    "    ret\n"
    // cockpit needles (0x4894EC, call 0x4880B0 inside 0x489240; eax = car)
    ".globl _XfNeedlesStub\n"
    "_XfNeedlesStub:\n"
    "    movl $1, _g_xfNeedle\n"
    "    call *_g_fnNeedles\n"
    "    movl $0, _g_xfNeedle\n"
    "    ret\n"
    ".globl _XfWrapLoadScreen\n"
    "_XfWrapLoadScreen:\n"
    "    pushal\n"
    "    pushl $1\n"
    "    call _XfBegin\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movl $0x494FA0, _g_xfTarget\n"
    "6:  call *_g_xfTarget\n"
    "    pushal\n"
    "    call _XfEnd\n"
    "    popal\n"
    "    ret\n"
    // d3da setup (0x4B5D51, "mov [0x7A3A64], esi" = 0): fog mode and setstate wrapper
    ".globl _D3dFogStub\n"
    "_D3dFogStub:\n"
    "    pushal\n"
    "    call _D3dFogInit\n"
    "    popal\n"
    "    ret\n"
    // mode list filter (0x4BEDDD): esi = index, edi = w, ebx = h, ecx = bpp,
    // edx = [+0x14], [ebp-8] = flags, [ebp-4] = the THRASH mode table
    ".globl _ModeFilterStub\n"
    "_ModeFilterStub:\n"
    "    pushal\n"
    "    pushl -4(%ebp)\n"
    "    pushl %esi\n"
    "    call _ModeOk\n"
    "    addl $8, %esp\n"
    "    testl %eax, %eax\n"
    "    popal\n"
    "    je 7f\n"
    "    pushl $0x4BEE32\n"           // listed: on to the callback
    "    ret\n"
    "7:  pushl $0x4BEE63\n"           // skipped
    "    ret\n"
    // call site of KillThread for the movie stream thread (eax = &handle): wait
    // for the thread to end first, then the original KillThread (return address
    // still the caller's)
    ".globl _StreamKillStub\n"
    "_StreamKillStub:\n"
    "    pushfl\n"
    "    pushal\n"
    "    pushl %eax\n"
    "    call _WaitStreamThread\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    popfl\n"
    "    pushl $0x4DF870\n"
    "    ret\n"
    // the sound thread's service call (0x4E2234: lock, iSNDdirectserve, unlock),
    // timed. Called by the thread loop 0x48EC40; registers kept as it does.
    ".globl _SoundServeStub\n"
    "_SoundServeStub:\n"
    "    pushal\n"
    "    call _SoundServeBefore\n"
    "    popal\n"
    "    movl $0x4E2234, %eax\n"
    "    call *%eax\n"
    "    pushal\n"
    "    call _SoundServeAfter\n"
    "    popal\n"
    "    ret\n"
    // ViewSetup, a camera with a zoom (0x4DBEA4: fstp [0x56009C], 6 bytes): its x focal length
    ".globl _WsZoomStub\n"
    "_WsZoomStub:\n"
    "    fstps 0x56009C\n"
    "    pushal\n"
    "    call _WsZoomFocal\n"
    "    popal\n"
    "    ret\n"
    // a horizon row projected (the 7 calls to 0x479370): on the extra strips its points moved
    ".globl _HorizonRowStub\n"
    "_HorizonRowStub:\n"
    "    cmpl $0, _g_hzShift\n"
    "    je 1f\n"
    "    pushal\n"
    "    pushl %ebx\n"
    "    pushl %eax\n"
    "    call _HorizonRow\n"
    "    addl $8, %esp\n"
    "    movl %eax, 0x10(%esp)\n"     // the saved ebx
    "    popal\n"
    "1:  jmp *_g_fnHzRow\n"
    // the horizon's parts (calls at 0x47A90E, 0x47A92D, 0x47A93D; eax = the horizon)
    ".globl _HorizonStub0\n"
    "_HorizonStub0:\n"
    "    pushl $0x479680\n"
    "    jmp 1f\n"
    ".globl _HorizonStub1\n"
    "_HorizonStub1:\n"
    "    pushl $0x4799D0\n"
    "    jmp 1f\n"
    ".globl _HorizonStub2\n"
    "_HorizonStub2:\n"
    "    pushl $0x479B90\n"
    "1:  pushal\n"
    "    pushl %eax\n"                // the horizon
    "    pushl 0x24(%esp)\n"          // the part
    "    call _HorizonDraw\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    addl $4, %esp\n"
    "    ret\n"
    // ProjectionSetup 0x4DBD2B (replaces "mov edx,[ebp-0x18] / mov eax,esi /
    // mov [ebp-8],esi / call 0x4FD3F0"; esi = half the FOV in degrees): Hor+.
    // [ebp-8] gets the horizontal half FOV as a float (its fild is now a fld).
    ".globl _WsProjStub\n"
    "_WsProjStub:\n"
    "    pushl %ecx\n"
    "    pushl -0x18(%ebp)\n"       // vertical half FOV (int)
    "    pushl %esi\n"
    "    call _WsHorPlus\n"          // eax = frustum half FOV (int); g_wsHalfF, g_wsHalfVF, g_wsVertI set
    "    addl $8, %esp\n"
    "    popl %ecx\n"
    "    movl _g_wsHalfF, %edx\n"
    "    movl %edx, -8(%ebp)\n"
    "    movl _g_wsHalfVF, %edx\n"
    "    movl %edx, -0x18(%ebp)\n"   // (a float now: its fild at 0x4DBD48 is a fld too)
    "    movl _g_wsVertI, %edx\n"    // vertical half FOV for the frustum (int)
    "    pushl $1f\n"
    "    pushl $0x4FD3F0\n"          // frustum (eax, edx)
    "    ret\n"
    "1:  ret\n"
    // texture size (0x49CEB9, jmp from "cmp ebx,0x100"): eax = size struct,
    // ebx = the image's size, already <= the caller's limit. Largest power of
    // two <= ebx, at most TexCap() (the game: at most 256), into [eax], [eax+4].
    ".globl _TexSizeStub\n"
    "_TexSizeStub:\n"
    "    pushl %ecx\n"
    "    pushal\n"
    "    call _TexCap\n"
    "    movl %eax, 24(%esp)\n"      // -> ecx
    "    popal\n"
    "1:  cmpl $1, %ecx\n"
    "    jbe 2f\n"
    "    cmpl %ebx, %ecx\n"
    "    jle 2f\n"
    "    shrl $1, %ecx\n"
    "    jmp 1b\n"
    "2:  movl %ecx, 4(%eax)\n"
    "    movl %ecx, (%eax)\n"
    "    popl %ecx\n"
    "    pushl $0x49CF75\n"
    "    ret\n"
    // call site of the install.win reader: run it, then the fallback / folder fix.
    // eax = the (possibly replaced) result; the caller tests it with cmp.
    ".globl _InstallReadStub\n"
    "_InstallReadStub:\n"
    "    movl $0x4A6E10, %eax\n"
    "    call *%eax\n"
    "    pushal\n"
    "    pushl %eax\n"
    "    call _AfterInstallRead\n"
    "    addl $4, %esp\n"
    "    movl %eax, 28(%esp)\n"      // pushal slot of eax
    "    popal\n"
    "    ret\n");

// ---------------------------------------------------------------------------
// Installers
// ---------------------------------------------------------------------------
static void InstallInterpolation() {
    if (!g_knownExe || !g_cfg.interpolate) { Logf("interpolation: off\n"); return; }
    bool ok = true;
    for (DWORD at : hp::callSimStep) ok &= PatchRel32(at, 0xE8, (void*)&StepStub);
    ok &= PatchRel32(hp::callRender, 0xE8, (void*)&RenderStub);
    BYTE j[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
    DWORD rel = (DWORD)&ExtraRenderStub - (hp::skipRenderAt + 5);
    memcpy(j + 1, &rel, 4);
    ok &= PatchBytes(hp::skipRenderAt, j, sizeof(j));
    g_extraEnabled = ok ? 1 : 0;
    Logf("interpolation: %s\n", ok ? "installed" : "patch FAILED");
}

static void InstallMenuUnlock() {
    if (!g_knownExe || !g_cfg.unlockMenus) { Logf("menus: original 64 fps cap\n"); return; }
    bool ok = PatchRel32(hp::menuWaitAt, 0xE9, (void*)&MenuWaitStub);
    Logf("menus: %s\n", ok ? "unlocked" : "patch FAILED");
}

// ---- menu animations that the game steps once per frame --------------------
extern "C" { int g_logoN = 1, g_glowN = 1; }
extern "C" void LogoStepStub();
extern "C" void GlowPhaseStub();
extern "C" void GlowCountStub();

// steps an animation takes this frame: one per two ticks (64 a second)
struct TickSteps {
    DWORD last = 0;
    bool  have = false;
    int Take() {
        DWORD t = *(volatile DWORD*)0x79C1A4 >> 1;
        DWORD n = t - last;
        if (!have || n > 8) n = 1;       // first frame, or back in the menus after a while
        have = true; last = t;
        return (int)n;
    }
};
extern "C" void __cdecl LogoSteps() { static TickSteps s; g_logoN = s.Take(); }
// The menus' text fades (0x445AC0: [0x661088] up 10 a frame to 255, or down 15;
// 0x445F50: [0x661064] up 20 a frame) were made for the menus' 64 fps. Here
// each gets the 64ths of a second since its last frame instead, fractions
// carried over, so a fade takes the same time at any frame rate.
extern "C" int __cdecl FadeSteps(int id) {
    static LONGLONG last[3]; static double acc[3];
    if (id < 0 || id > 2) return 1;
    LONGLONG now = Qpc();
    double dt = last[id] ? (double)(now - last[id]) / (double)g_qpf.QuadPart : 1.0;
    last[id] = now;
    if (dt > 0.25 || dt < 0) { acc[id] = 0; return 1; }   // (the first frame of a fade)
    acc[id] += dt * 64.0;
    int n = (int)acc[id];
    acc[id] -= n;
    return n;
}
extern "C" void __cdecl GlowSteps() { static TickSteps s; g_glowN = s.Take(); }

__asm__(
    // the text fades: in place of "lea edi,[esi+0x14] / mov [0x661064],edi" (0x445F83),
    // "lea ebx,[ecx+0xA] / mov [0x661088],ebx" (0x445CC3) and "lea edi,[esi-0xF] /
    // mov [0x661088],edi" (0x445C99): the same per 64th of a second
    ".globl _FadeInStub\n"
    "_FadeInStub:\n"
    "    pushl %eax\n"
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl $0\n"
    "    call _FadeSteps\n"
    "    addl $4, %esp\n"
    "    imull $0x14, %eax, %edi\n"
    "    addl %esi, %edi\n"
    "    popl %edx\n"
    "    popl %ecx\n"
    "    popl %eax\n"
    "    movl %edi, 0x661064\n"
    "    ret\n"
    ".globl _TextFadeInStub\n"
    "_TextFadeInStub:\n"
    "    pushl %eax\n"
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl $1\n"
    "    call _FadeSteps\n"
    "    addl $4, %esp\n"
    "    imull $0xA, %eax, %ebx\n"
    "    popl %edx\n"
    "    popl %ecx\n"
    "    popl %eax\n"
    "    addl %ecx, %ebx\n"
    "    movl %ebx, 0x661088\n"
    "    ret\n"
    ".globl _TextFadeOutStub\n"
    "_TextFadeOutStub:\n"
    "    pushl %eax\n"
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    pushl $2\n"
    "    call _FadeSteps\n"
    "    addl $4, %esp\n"
    "    imull $0xF, %eax, %eax\n"
    "    movl %esi, %edi\n"
    "    subl %eax, %edi\n"
    "    popl %edx\n"
    "    popl %ecx\n"
    "    popl %eax\n"
    "    movl %edi, 0x661088\n"
    "    ret\n"
    // in place of the step at 0x4A469E (eax = logo * 4): ecx = x + n*dx, ebx = y + n*dy, edx = dx
    ".globl _LogoStepStub\n"
    "_LogoStepStub:\n"
    "    testl %eax, %eax\n"            // first logo: a new frame
    "    jnz 1f\n"
    "    call _LogoSteps\n"
    "    xorl %eax, %eax\n"
    "1:  movl 0x7A1AE8(%eax), %edx\n"
    "    movl _g_logoN, %ecx\n"
    "    movl %ecx, %ebx\n"
    "    imull %edx, %ecx\n"
    "    addl 0x55E95C(%eax), %ecx\n"
    "    imull 0x7A1B10(%eax), %ebx\n"
    "    addl 0x7A1AC0(%eax), %ebx\n"
    "    ret\n"
    // in place of "mov dx, [esi+0x46] / add edx, 2" at 0x4A4C35, the first thing
    // 0x4A4C20 does after a call: the frame's step count, then phase + 2n
    ".globl _GlowPhaseStub\n"
    "_GlowPhaseStub:\n"
    "    pushal\n"
    "    call _GlowSteps\n"
    "    popal\n"
    "    movw 0x46(%esi), %dx\n"
    "    addl _g_glowN, %edx\n"
    "    addl _g_glowN, %edx\n"
    "    ret\n"
    // in place of the countdown at 0x4A4D1B: 0 -> 300, else minus n, not below 0
    ".globl _GlowCountStub\n"
    "_GlowCountStub:\n"
    "    movl 0x55EB28, %eax\n"
    "    testl %eax, %eax\n"
    "    jz 2f\n"
    "    subl _g_glowN, %eax\n"
    "    jns 1f\n"
    "    xorl %eax, %eax\n"
    "1:  movl %eax, 0x55EB28\n"
    "    ret\n"
    "2:  movl $300, 0x55EB28\n"
    "    ret\n"
);

extern "C" void FadeInStub(); extern "C" void TextFadeInStub(); extern "C" void TextFadeOutStub();
static void InstallMenuAnimations() {
    if (!g_knownExe || !g_cfg.menuAnimations) { Logf("menu animations: one step per frame, as in the game\n"); return; }
    static const BYTE skipLogo[2]  = { 0xEB, 0x13 };     // jmp 0x4A46B8, past the rest of the old step
    static const BYTE nop2[2]      = { 0x90, 0x90 };
    static const BYTE skipCount[2] = { 0xEB, 0x17 };     // jmp 0x4A4D39, past the rest of the old countdown
    bool ok = PatchRel32(0x4A469E, 0xE8, (void*)&LogoStepStub)  && PatchBytes(0x4A46A3, skipLogo, 2) &&
              PatchRel32(0x4A4C35, 0xE8, (void*)&GlowPhaseStub) && PatchBytes(0x4A4C3A, nop2, 2) &&
              PatchRel32(0x4A4D1B, 0xE8, (void*)&GlowCountStub) && PatchBytes(0x4A4D20, skipCount, 2);
    static const BYTE nop4[4] = { 0x90, 0x90, 0x90, 0x90 };
    ok = ok && PatchRel32(0x445F83, 0xE8, (void*)&FadeInStub) && PatchBytes(0x445F88, nop4, 4) &&
         PatchRel32(0x445CC3, 0xE8, (void*)&TextFadeInStub) && PatchBytes(0x445CC8, nop4, 4) &&
         PatchRel32(0x445C99, 0xE8, (void*)&TextFadeOutStub) && PatchBytes(0x445C9E, nop4, 4);
    Logf("menu animations: %s\n", ok ? "background logos, background fade, spark, glow and text fades step 64 times a second"
                                     : "patch FAILED");
}

static void InstallLoading() {
    bool ws = g_knownExe && g_cfg.widescreen && g_cfg.wsLoading;
    if (!g_knownExe || (!g_cfg.speedUpLoading && !ws)) { Logf("loading: original\n"); return; }
    bool ok = PatchRel32(hp::fnLoadBar, 0xE9, (void*)&LoadBarStub);
    if (ok && ws) g_wsLoading = 1;
    if (g_cfg.speedUpLoading)
        Logf("loading: %s (loading bar redrawn at most every %d ms)\n",
             ok ? "sped up" : "patch FAILED", g_cfg.loadScreenRenderMs);
}

static void InstallInstallPaths() {
    if (!g_knownExe || (!g_cfg.portableInstallPaths && !g_cfg.installWinFallback)) {
        Logf("install.win: as written\n"); return;
    }
    bool ok = PatchRel32(hp::callReadInstall, 0xE8, (void*)&InstallReadStub);
    if (!ok) { Logf("install.win: patch FAILED\n"); return; }
    Logf("install.win:%s%s\n",
         g_cfg.portableInstallPaths ? " drive-letter folders read from the game folder when present;" : "",
         g_cfg.installWinFallback ? " built-in full install if the file is missing" : "");
}

// The startup check asks 0x4B62A0 whether install.win's line 2 is the hidden
// word "remote" (an install type that also turns off parts of the game via
// [0x6FBB54], so it's not used); if not, it wants the CD in a drive. The call
// becomes "mov eax,1" (= remote) at this one site, so only the CD search goes;
// [0x6FBB54] is set by its own call (0x472B38) and still reads "local".
// ---------------------------------------------------------------------------
// Car detail. 0x4B9DF0 (car drawing) takes the car's distance and the table
// [0x55FDF4 + carDetail*16] (carDetail = [0x6FBC1C], 0 = high): 4 floats,
// beyond [0] model LOD 1, beyond [1] LOD 2, beyond [2] LOD 3, beyond [3] not
// drawn. 0x4BB5D0 reads the same table (x0.75 in split screen).
// 0x4BBAD0: every car at detail level L (car+0x8B4, 0..3) costs 4-L points;
// while the total is over the budget [0x4B7A50 + carDetail*4] (10, 8, 6) the
// farthest car at 1 goes to 2, else the farthest at 0 goes to 1.
// ---------------------------------------------------------------------------
static void InstallCarDetail() {
    if (!g_knownExe) return;
    float* lod = (float*)0x55FDF4;
    bool changed = false;
    for (int lvl = 0; lvl < 3; lvl++) {
        float* t = lod + lvl * 4;
        if (g_cfg.carDrawDistance > 0) { t[3] = (float)g_cfg.carDrawDistance; changed = true; }
        if (g_cfg.carLodScale > 0.0f && g_cfg.carLodScale != 1.0f) {
            for (int i = 0; i < 3; i++) t[i] = t[i] * g_cfg.carLodScale;
            changed = true;
        }
        for (int i = 0; i < 3; i++) if (t[i] > t[3]) t[i] = t[3];
    }
    if (changed)
        Logf("cars: LOD switches (high detail) at %.0f / %.0f / %.0f m, drawn up to %.0f m\n",
             lod[0], lod[1], lod[2], lod[3]);
    else Logf("cars: the game's LOD distances\n");
    if (g_cfg.fullDetailCars) {
        static const int kBudget[3] = { 64, 64, 64 };   // 16 cars x 4 points: never over
        bool ok = PatchBytes(0x4B7A50, kBudget, sizeof(kBudget));
        Logf("cars: %s\n", ok ? "no detail budget (every car keeps its detail)" : "budget patch FAILED");
    }
}

// ---------------------------------------------------------------------------
// Texture sizes. Textures are loaded square, at a power of two: 0x49CEA0 picks
// the largest one <= the image's size, at most the caller's limit and at most
// 256. Callers' limits: 128 for a set loaded with the track (0x41DB10, with
// the loading bar; 0x41DB7B), 256 for the
// player's car and 128 for every other car (0x4B7D30). maxTextureSize raises
// all of them - with the Direct3D renderer. What a renderer says of itself is
// no help: _THRASH_about gives 256 as the largest texture for d3da.dll and
// softtria.dll alike (+0x14 width, +0x20 height), and d3da.dll takes larger
// ones all the same, where softtria.dll draws them as stripes (HD cars). So any
// other renderer keeps the game's 256 (g_texBig, set as the renderer is loaded).
// ---------------------------------------------------------------------------
static int Pow2Floor(int v) { int p = 1; while (p * 2 <= v && p < (1 << 14)) p *= 2; return p; }
static int g_texCfg = 256;
static bool g_texBig = false;                     // the renderer DLL loaded last is the Direct3D one (HkLoadLibraryA)
extern "C" int __cdecl TexCap() {
    int cap = g_texBig ? g_texCfg : 256;          // (g_texCfg is 256 or more)
    static int logged = 0;
    if (cap != logged) {
        logged = cap;
        LogfAsync("textures: up to %dx%d%s\n", cap, cap, cap < g_texCfg ? " (this renderer takes no larger ones)" : "");
    }
    return cap;
}
// ---------------------------------------------------------------------------
// Air drag. 0x4A2260, once per sim step for each car (from 0x4A2871): while
// the car is in the air (word car+0x164 != 0; set by 0x42F4A2, cleared on
// landing by 0x4A2630) it works out a drag from the car's horizontal velocity
// (car+0xA4 x, +0xAC z) squared, weighted by how the car's axes (car+0xC0..)
// line up with it, times the float at [0x53C8B4] (0.03125), and takes it off
// both components (0x4A25E2, 0x4A260E). That float is read only there
// (0x4A25A3), so that instruction reads airDrag x 0.03125 from here instead.
// ---------------------------------------------------------------------------
static float g_airDragK = 0.03125f;
static void InstallAirDrag() {
    if (!g_knownExe) return;
    float k = g_cfg.airDrag;
    if (!(k >= 0.0f)) k = 0.0f;                   // (negative or NaN: none)
    if (k > 10.0f) k = 10.0f;
    if (k == 1.0f) { Logf("air drag: the game's\n"); return; }
    g_airDragK = 0.03125f * k;
    BYTE b[6] = { 0xD9, 0x05 };                   // fld dword ptr [&g_airDragK]
    float* at = &g_airDragK; memcpy(b + 2, &at, 4);
    Logf("air drag: x%.2f: %s\n", k, PatchBytes(0x4A25A3, b, 6) ? "on" : "patch FAILED");
}

static void InstallTextureSize() {
    if (!g_knownExe) return;
    int want = g_cfg.maxTextureSize;
    if (want < 256) want = 256;
    g_texCfg = Pow2Floor(want > 4096 ? 4096 : want);
    if (g_texCfg == 256) {
        if (g_cfg.fullDetailCars) {     // other cars' textures like the player's
            static const BYTE b[5] = { 0xBB, 0x00, 0x01, 0x00, 0x00 };
            Logf("textures: other cars %s\n", PatchBytes(0x4B7D5B, b, 5) ? "256x256 like yours" : "patch FAILED");
        }
        Logf("textures: the game's sizes (256x256 at most)\n");
        return;
    }
    BYTE lim[5] = { 0xBB }; memcpy(lim + 1, &g_texCfg, 4);
    bool ok = PatchRel32(0x49CEB9, 0xE9, (void*)&TexSizeStub) &&
              PatchBytes(0x41DB7B, lim, 5) && PatchBytes(0x4B7D54, lim, 5) &&
              (!g_cfg.fullDetailCars || PatchBytes(0x4B7D5B, lim, 5));
    Logf("textures: up to %dx%d (track and %s cars too; 256 with any renderer but Direct3D): %s\n",
         g_texCfg, g_texCfg, g_cfg.fullDetailCars ? "all" : "your", ok ? "on" : "patch FAILED");
}

// ---------------------------------------------------------------------------
// Closing the window. The window procedure dispatches through handlers
// registered at 0x4CD7F2..: WM_CLOSE (0x10) -> 0x4CCE40 (PostQuitMessage: the
// window thread stops pumping messages, the game hangs) and WM_SYSCOMMAND
// (0x112) -> 0x4CCE90, which eats SC_CLOSE (and SC_MINIMIZE / MAXIMIZE /
// RESTORE), so Alt+F4 and the title bar / window manager close do nothing. The
// game quits through its own message 0x466 + DestroyWindow, never WM_CLOSE.
// Both handlers (stdcall, 6 arguments) are wrapped: a close ends the process.
// ---------------------------------------------------------------------------
typedef LRESULT (WINAPI *WndHandler_t)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
static void ExitFromOutside(const char* how) {
    Logf("window closed from outside the game (%s) - exiting\n", how);
    if (g_log) fflush(g_log);
    TerminateProcess(GetCurrentProcess(), 0);
}
static LRESULT WINAPI HkWmClose(DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f) {
    ExitFromOutside("WM_CLOSE");
    return ((WndHandler_t)0x4CCE40)(a, b, c, d, e, f);
}
static LRESULT WINAPI HkWmSysCommand(DWORD a, DWORD b, DWORD c, DWORD wparam, DWORD e, DWORD f) {
    if ((wparam & 0xFFF0) == SC_CLOSE) ExitFromOutside("SC_CLOSE");
    return ((WndHandler_t)0x4CCE90)(a, b, c, wparam, e, f);
}
static void InstallD3dFog() {
    if (!g_knownExe || !g_cfg.d3dFog) { Logf("d3d fog: off\n"); return; }
    BYTE b[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
    DWORD rel = (DWORD)&D3dFogStub - (0x4B5D51 + 5); memcpy(b + 1, &rel, 4);
    bool ok = PatchBytes(0x4B5D51, b, 6);
    // the cockpit picture is drawn without fog (XfBegin/XfEnd around it)
    if (ok && !(g_knownExe && g_cfg.widescreen && g_cfg.wsCockpit)) ok = PatchRel32(0x482CA3, 0xE8, (void*)&XfWrapCockpit);
    Logf("d3d fog: %s\n", ok ? "turned on when the Direct3D renderer starts" : "patch FAILED");
}

static void InstallCloseFix() {
    if (!g_knownExe || !g_cfg.closeExits) { Logf("close: original (closing the window does nothing)\n"); return; }
    DWORD c = (DWORD)&HkWmClose, s = (DWORD)&HkWmSysCommand;
    bool ok = PatchBytes(0x4CD7FF, &c, 4) && PatchBytes(0x4CD83B, &s, 4);
    Logf("close: %s\n", ok ? "closing the window ends the game" : "patch FAILED");
}

// ---------------------------------------------------------------------------
// Widescreen.
//  - Display modes: the options' mode list (0x4BED90) skips every mode where
//    w*3 != h*4 (jne at 0x4BEDF8); wsUnlockResolutions removes that test.
//  - 3D view: ProjectionSetup (0x4DBCE0; x, y, w, h, fov) takes half the FOV
//    (45 for the default 90) as the horizontal half angle and 13/16 of it as
//    the vertical one, and sets the focal lengths w / tan(h) and h / tan(v)
//    (0x4BF260; [0x56009C], [0x5600A0]) and the view frustum (0x4FD3F0, int
//    degrees). So any screen shows the same angles: a wider screen stretches
//    the 4:3 picture. Hor+: the horizontal half angle becomes
//    atan(tan(h) * (W/H) / (4/3)) for the screen (mode [0x7CDAE0] x [0x7CDAE4]),
//    the vertical one stays: the 4:3 view with more at the sides. Every view
//    (main, split screen, mirror) gets the same factor, so they keep their
//    original proportions relative to each other.
// ---------------------------------------------------------------------------
// Rain and snow (0x4DEE36 / 0x4DED7A, when [0x8CA22C] isn't 0) are drawn in a
// pass of their own with a 120 degree view (0x4DBDA0: edx = 0x78), and their
// flakes live in a space made for that view. Widened like the rest, the screen's
// outer parts show what lies beyond that space, where flakes close to the
// camera come out as big white blocks at the screen's edge. So that one pass
// keeps the game's own horizontal angle, and to keep the flakes' shape the
// vertical one is narrowed instead (tan(v) / factor): the same snow over the
// whole width, a little larger (wsWeather).
// The horizon (0x47A880: its parts 0x479680, 0x4799D0 and 0x479B90, set up
// from the track's .hrz files, with their own night and weather versions) is
// a ring of 16 pieces laid out flat: each part draws 8 of them as a strip
// facing the camera, its rows 9 points each (0x479500 lines them up, 0x479370
// projects a row), the pieces as wide as a sixteenth of the ring's
// circumference. The strip covers about 57 degrees either side of the view's
// middle and slides by up to a piece as the camera turns (which pieces it
// shows follows the heading, 0x479160), so its right end can be as close as
// 45 degrees: enough for the game's 90 degree view, not for a wider one. At
// night and on snow tracks the sky behind it is darker than the horizon's
// glow, and the right side of the screen showed a dark rectangle where the
// strip ended. So on a screen wider than 4:3 each part is drawn twice more,
// its strip moved by its own length to the right and to the left (its rows'
// points moved before they're projected), the pieces' textures moved by 8 as
// well (the ring's rotation [0x55D1CC] + 0.5): three strips end to end, the
// ring's next pieces either side, at the same depth.
extern "C" { float g_wsHalfF = 45.0f, g_wsHalfVF = 36.0f; int g_wsVertI = 36; }
static double WsFactor() {
    int w = Rd<int>(0x7CDAE0), h = Rd<int>(0x7CDAE4);
    if (w <= 0 || h <= 0) return 1.0;
    return (w * 3.0) / (h * 4.0);
}
extern "C" void __cdecl HudUpdate();
extern "C" int __cdecl WsHorPlus(int half, int vert) {
    if (g_cfg.wsHud) HudUpdate();                 // the HUD width follows the view too
    g_wsHalfVF = (float)vert; g_wsVertI = vert;
    double f = g_wsMirror && g_mirrorNarrowed ? 1.0 : WsFactor();   // (a narrowed mirror has its 4:3 shape: the game's own angle)
    if (f <= 1.0001 || half <= 0 || half >= 89) { g_wsHalfF = (float)half; return half; }
    const double kDeg = 3.14159265358979323846 / 180.0;
    if (half == 60 && g_cfg.wsWeather && Rd<int>(0x8CA22C) != 0 && vert > 0 && vert < 89) {
        double vv = atan(tan(vert * kDeg) / f) / kDeg;
        g_wsHalfF = (float)half; g_wsHalfVF = (float)vv; g_wsVertI = (int)ceil(vv);
        static bool logged = false;
        if (!logged) { logged = true; LogfAsync("widescreen: rain and snow keep their %d degree width (vertical half angle %d -> %.1f)\n", half * 2, vert, vv); }
        return half;
    }
    double hh = atan(tan(half * kDeg) * f) / kDeg;
    if (hh > 88.0) hh = 88.0;
    g_wsHalfF = (float)hh;
    static int lastHalf = 0; static double lastF = 0;
    if (half != lastHalf || fabs(f - lastF) > 1e-6) {
        lastHalf = half; lastF = f;
        LogfAsync("widescreen: %dx%d - horizontal FOV %d -> %.1f degrees\n",
                  Rd<int>(0x7CDAE0), Rd<int>(0x7CDAE4), half * 2, hh * 2);
    }
    return (int)ceil(hh);
}
// A camera with a zoom of its own (cam+0x54: the cameras at the start and the
// finish of a race) doesn't use the angle: after ProjectionSetup, ViewSetup
// (0x4DBE8F) sets the float focal lengths [0x56009C] and [0x5600A0] to half
// the view's width and height in pixels times the zoom. On a wider screen x
// came out as many times longer as the screen is wider than 4:3, and the
// picture was stretched. x is divided by that factor, which gives the 4:3
// proportions, as WsHorPlus does with the angle.
extern "C" void __cdecl WsZoomFocal() {
    double f = WsFactor();
    if (f > 1.0001) *(volatile float*)0x56009C = (float)(Rd<float>(0x56009C) / f);
}
// a horizon part (Watcom: eax = the horizon), then on a wider screen its two more strips
static inline void HorizonCall(DWORD fn, DWORD hz) {
    __asm__ volatile("call *%1" : "+a"(hz) : "r"(fn) : "memory", "cc");
}
extern "C" { int g_hzShift = 0; DWORD g_fnHzRow = 0x479370; }
static int g_hzRow = 0;                           // rows projected so far in this drawing of the part
extern "C" void __cdecl HorizonDraw(DWORD fn, DWORD hz) {
    HorizonCall(fn, hz);
    if (WsFactor() <= 1.0001) return;
    volatile float* rot = (volatile float*)0x55D1CC;
    float save = *rot;
    *rot = save + 0.5f;
    g_hzShift = 1;  g_hzRow = 0; HorizonCall(fn, hz);
    g_hzShift = -1; g_hzRow = 0; HorizonCall(fn, hz);
    g_hzShift = 0;
    *rot = save;
}
// a row's points (0x479370: eax = count, ebx = 3 floats each) moved by the strip's
// length times g_hzShift, in a copy: the game's own points stay as they are. The
// length comes from the part's first row, the strip's edge, all 9 points set: in
// 0x479B90 the other two rows are worked out piece by piece, 8 points, the 9th
// never written.
extern "C" const float* __cdecl HorizonRow(int n, const float* p) {
    static float row[9 * 3], d[3];
    if (n != 9) return p;
    if (g_hzRow++ == 0)
        for (int c = 0; c < 3; c++) d[c] = (p[8 * 3 + c] - p[c]) * (float)g_hzShift;
    for (int i = 0; i < 9 * 3; i++) row[i] = p[i] + d[i % 3];
    return row;
}
// ---------------------------------------------------------------------------
// 2D pictures drawn with a transform. Between XfBegin(mode) and XfEnd the
// THRASH draw calls (drawquad [0x9EF94C], drawtri [0x9EF974], drawtrifan
// [0x9EF960]) go through wrappers that copy the vertices (32 bytes each,
// screen x at +0, y at +4) and move them, and THRASH_clip [0x9EF92C]
// (x0, y0, x1, y1; set by 0x4BEBE0, e.g. around the steering wheel) gets
// the same move so the clipped parts line up with the moved picture:
//  1 = 4:3 in the middle: x' = cx + (x - cx) * 4H / 3W (loading screen, whose
//      picture and bar the game scales to the whole screen: 0x494FA0, 0x495230);
//  2 = zoom: y' = cy + (y - cy) * 3W / 4H, x unchanged (cockpit picture
//      0x489240, scaled by the game to the screen's width and height): it keeps
//      its shape, centred, its top and bottom beyond the screen. The needles
//      (0x4880B0) are placed and sized with W/640 on both axes - already the
//      zoomed size, but from the top - so they only move: y' = y - cy (k - 1).
// The table entries are swapped in for the call only; nothing else is touched.
// ---------------------------------------------------------------------------
typedef void (WINAPI *TDraw4_t)(void*, void*, void*, void*);
typedef void (WINAPI *TDraw3_t)(void*, void*, void*);
typedef void (WINAPI *TDrawN_t)(int, void*);
typedef int  (WINAPI *TClip_t)(int, int, int, int);
static TDraw4_t g_oQuad; static TDraw3_t g_oTri; static TDrawN_t g_oFan; static TClip_t g_oClip;
static int g_xfMode = 0, g_xfDepth = 0, g_xfW, g_xfH;
static bool g_xfCockpit = false;                  // the cockpit picture (0x489240) is being drawn
static float g_xfCx, g_xfCy, g_xfK;
static const int kVtx = 32;
static_assert(kVtx == 32, "TVERTEX");
static inline void XfVertex(BYTE* dst, const void* src) {
    memcpy(dst, src, kVtx);
    float* v = (float*)dst;
    if (g_xfMode == 1)  v[0] = g_xfCx + (v[0] - g_xfCx) * g_xfK;
    else if (g_xfNeedle) v[1] += g_xfCy * (1.0f - g_xfK);     // already at the zoomed size
    else                v[1] = g_xfCy + (v[1] - g_xfCy) * g_xfK;
}
static int XfClipCoord(int c, float mid, int lim) {
    double r = floor(mid + (c - mid) * g_xfK + 0.5);
    return r < 0 ? 0 : r > lim ? lim : (int)r;
}
static int WINAPI XfClip(int x0, int y0, int x1, int y1) {
    if (g_xfMode == 1) { x0 = XfClipCoord(x0, g_xfCx, g_xfW); x1 = XfClipCoord(x1, g_xfCx, g_xfW); }
    else               { y0 = XfClipCoord(y0, g_xfCy, g_xfH); y1 = XfClipCoord(y1, g_xfCy, g_xfH); }
    return g_oClip(x0, y0, x1, y1);
}
// d3da draws with D3DDP_DONOTCLIP, so a vertex moved off the screen gives
// garbage (stray triangles across the picture): a polygon that leaves the
// screen is cut at its edges here (x, y, z, rhw and colours interpolated in
// screen space, u/v perspective-correct through rhw) and drawn as a fan.
struct XfV { float x, y, z, rhw; DWORD col, spec; float u, v; };
static XfV XfLerp(const XfV& a, const XfV& b, float t) {
    XfV r;
    r.x = a.x + (b.x - a.x) * t; r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t; r.rhw = a.rhw + (b.rhw - a.rhw) * t;
    DWORD c = 0, s = 0;
    for (int k = 0; k < 32; k += 8) {
        float ca = (float)((a.col >> k) & 255), cb = (float)((b.col >> k) & 255);
        float sa = (float)((a.spec >> k) & 255), sb = (float)((b.spec >> k) & 255);
        c |= (DWORD)(int)(ca + (cb - ca) * t + 0.5f) << k;
        s |= (DWORD)(int)(sa + (sb - sa) * t + 0.5f) << k;
    }
    r.col = c; r.spec = s;
    if (a.rhw > 0 && b.rhw > 0 && r.rhw > 0) {
        r.u = (a.u * a.rhw + (b.u * b.rhw - a.u * a.rhw) * t) / r.rhw;
        r.v = (a.v * a.rhw + (b.v * b.rhw - a.v * a.rhw) * t) / r.rhw;
    } else { r.u = a.u + (b.u - a.u) * t; r.v = a.v + (b.v - a.v) * t; }
    return r;
}
// one edge: axis 0 = x, 1 = y; keep the side where sign * (coord - lim) <= 0
static int XfClipEdge(const XfV* in, int n, XfV* out, int axis, float lim, float sign) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const XfV& a = in[i]; const XfV& b = in[(i + 1) % n];
        float da = sign * ((axis ? a.y : a.x) - lim), db = sign * ((axis ? b.y : b.x) - lim);
        if (da <= 0) out[m++] = a;
        if ((da < 0 && db > 0) || (da > 0 && db < 0)) out[m++] = XfLerp(a, b, da / (da - db));
    }
    return m;
}
// The software renderer, given the triangles and fans that the cut below makes
// of a picture's pieces, draws a black line along each piece's far edges: there
// it reads the texture one texel past its end (u or v = 1). It does take u/v
// from a triangle's corners - which it doesn't for a quad, where it always
// shows the whole texture, so the pieces can't be cut and kept as quads. So
// for it a piece's highest u and v are pulled in (texel: by how much, 1/256 = a
// texel of a 256 texture), and its corners go to whole pixels, so two pieces
// share their edge exactly. It also leaves the last row of a triangle's pixels
// undrawn - a see-through line where one piece ends and the next begins - so a
// piece's bottom edge (and its right one, with overlapX) goes a pixel further;
// the piece after it draws over that.
static bool g_softRenderer = false;               // the renderer DLL loaded last is the software one (HkLoadLibraryA)
static void XfSoftPrep(XfV* v, int n, float texel, bool overlapX) {
    float u0 = v[0].u, u1 = v[0].u, w0 = v[0].v, w1 = v[0].v;
    for (int i = 0; i < n; i++) { v[i].x = floorf(v[i].x + 0.5f); v[i].y = floorf(v[i].y + 0.5f); }
    float x0 = v[0].x, x1 = v[0].x, y0 = v[0].y, y1 = v[0].y;
    for (int i = 1; i < n; i++) {
        if (v[i].u < u0) u0 = v[i].u;
        if (v[i].u > u1) u1 = v[i].u;
        if (v[i].v < w0) w0 = v[i].v;
        if (v[i].v > w1) w1 = v[i].v;
        if (v[i].x < x0) x0 = v[i].x;
        if (v[i].x > x1) x1 = v[i].x;
        if (v[i].y < y0) y0 = v[i].y;
        if (v[i].y > y1) y1 = v[i].y;
    }
    for (int i = 0; i < n; i++) {
        if (u1 - u0 > 4 * texel && v[i].u == u1) v[i].u -= texel;
        if (w1 - w0 > 4 * texel && v[i].v == w1) v[i].v -= texel;
        if (y1 - y0 > 2 && v[i].y == y1) v[i].y += 1.0f;
        if (overlapX && x1 - x0 > 2 && v[i].x == x1) v[i].x += 1.0f;
    }
}
static void XfDraw(XfV* v, int n) {                // already moved
    bool out = false;
    for (int i = 0; i < n && !out; i++)
        out = v[i].x < 0 || v[i].y < 0 || v[i].x > (float)g_xfW || v[i].y > (float)g_xfH;
    if (out && g_softRenderer && !g_xfNeedle) XfSoftPrep(v, n, 1.0f / 256.0f, false);
    if (!out) {
        if (n == 4)      g_oQuad(&v[0], &v[1], &v[2], &v[3]);
        else if (n == 3) g_oTri(&v[0], &v[1], &v[2]);
        else             g_oFan(n - 2, v);
        return;
    }
    // each triangle as the driver would draw it (0, i, i + 1: quads and fans
    // alike), so the corner order of a quad doesn't matter
    for (int i = 1; i + 1 < n; i++) {
        XfV a[8], b[8], t[3] = { v[0], v[i], v[i + 1] };
        int m = XfClipEdge(t, 3, a, 1, 0.0f, -1.0f);   if (m < 3) continue;
        m = XfClipEdge(a, m, b, 1, (float)g_xfH, 1.0f); if (m < 3) continue;
        m = XfClipEdge(b, m, a, 0, 0.0f, -1.0f);       if (m < 3) continue;
        m = XfClipEdge(a, m, b, 0, (float)g_xfW, 1.0f); if (m < 3) continue;
        if (m == 3) g_oTri(&b[0], &b[1], &b[2]); else g_oFan(m - 2, b);
    }
}
static void WINAPI XfQuad(void* a, void* b, void* c, void* d) {
    XfV v[4];
    XfVertex((BYTE*)&v[0], a); XfVertex((BYTE*)&v[1], b); XfVertex((BYTE*)&v[2], c); XfVertex((BYTE*)&v[3], d);
    XfDraw(v, 4);
}
static void WINAPI XfTri(void* a, void* b, void* c) {
    XfV v[3];
    XfVertex((BYTE*)&v[0], a); XfVertex((BYTE*)&v[1], b); XfVertex((BYTE*)&v[2], c);
    XfDraw(v, 3);
}
// drawtrifan(triangles, vertices): triangles + 2 vertices in a row.
static void WINAPI XfFan(int tris, void* verts) {
    static XfV v[64];                              // game thread only
    int n = tris + 2;
    if (tris <= 0 || n > 64) { g_oFan(tris, verts); return; }
    for (int i = 0; i < n; i++) XfVertex((BYTE*)&v[i], (BYTE*)verts + i * kVtx);
    XfDraw(v, n);
}
static void FogPause(bool pause);
extern "C" void __cdecl XfBegin(int mode) {
    if (g_xfDepth++) return;                      // nested: the outer mode stays
    g_xfCockpit = mode == 2;
    FogPause(true);                               // 2D pictures: no fog
    if (!g_cfg.widescreen || (mode == 2 && !g_cfg.wsCockpit) || (mode == 1 && !g_cfg.wsLoading)) { g_xfMode = 0; return; }
    int w = Rd<int>(0x7CDAE0), h = Rd<int>(0x7CDAE4);
    double f = (w > 0 && h > 0) ? (w * 3.0) / (h * 4.0) : 1.0;
    // zoom only for the single full-screen view (split screen: [0x6FD3B0] = 1)
    if (f <= 1.0001 || (mode == 2 && Rd<int>(0x6FD3B0) == 1)) { g_xfMode = 0; return; }
    g_xfMode = mode;
    g_xfW = w; g_xfH = h; g_xfCx = w * 0.5f; g_xfCy = h * 0.5f;
    g_xfK = mode == 1 ? (float)(1.0 / f) : (float)f;
    g_oQuad = Rd<TDraw4_t>(0x9EF94C); g_oTri = Rd<TDraw3_t>(0x9EF974); g_oFan = Rd<TDrawN_t>(0x9EF960);
    g_oClip = Rd<TClip_t>(0x9EF92C);
    if (!g_oQuad || !g_oTri || !g_oFan || !g_oClip) { g_xfMode = 0; return; }
    *(volatile DWORD*)0x9EF94C = (DWORD)&XfQuad;
    *(volatile DWORD*)0x9EF974 = (DWORD)&XfTri;
    *(volatile DWORD*)0x9EF960 = (DWORD)&XfFan;
    *(volatile DWORD*)0x9EF92C = (DWORD)&XfClip;
}
extern "C" void __cdecl XfEnd() {
    if (!g_xfDepth || --g_xfDepth) return;
    g_xfCockpit = false;
    FogPause(false);
    if (!g_xfMode) return;
    if (Rd<DWORD>(0x9EF94C) == (DWORD)&XfQuad) *(volatile DWORD*)0x9EF94C = (DWORD)g_oQuad;
    if (Rd<DWORD>(0x9EF974) == (DWORD)&XfTri)  *(volatile DWORD*)0x9EF974 = (DWORD)g_oTri;
    if (Rd<DWORD>(0x9EF960) == (DWORD)&XfFan)  *(volatile DWORD*)0x9EF960 = (DWORD)g_oFan;
    if (Rd<DWORD>(0x9EF92C) == (DWORD)&XfClip) *(volatile DWORD*)0x9EF92C = (DWORD)g_oClip;
    g_xfMode = 0;
}

// ---------------------------------------------------------------------------
// The options' mode list (0x4BED90) walks the renderer's modes (table [ebp-4]:
// count +0x3C, entries +0x40, 40 bytes each: w, h, bpp, ?, flags, +0x14, +0x18)
// and lists a mode if +0x14 >= 2, w*3 == h*4, bpp 15 or 16, flags bit 0 set and
// bit 1 clear, and w*h*2 <= half the video memory the game assumes for its
// driver ([0x55FED0], 4 MB or less: nothing above about 1024x768). The menu
// list (0x448210) holds 20 entries. ModeOk decides instead, once per list:
// the game's +0x14 and flag tests, at least 640x480, any shape, no memory test,
// each resolution once (32-bit preferred with ws32bit, else 16-bit), and when
// there are more than 20, 640x480 plus the 19 largest.
// ---------------------------------------------------------------------------
static bool g_modeKeep[256];
// a mode the list may show (the game's +0x14 and flag tests, at least 640x480,
// 15/16-bit, or 24/32-bit with ws32bit); 0 = no, else its rank (higher = preferred)
// The software renderer (softtria.dll) lists 32-bit modes too but only ever
// writes 16-bit pixels: in a 32-bit mode its picture fills the left half of the
// screen in wrong colours. So for it only the 15/16-bit modes count.
static int ModeRank(const int* m) {
    int w = m[0], h = m[1], bpp = m[2], flags = m[4], f14 = m[5];
    if (f14 < 2 || !(flags & 1) || (flags & 2) || w < 640 || h < 480) return 0;
    // (and it draws whole blocks of 16 columns: at 854 wide the picture is 848
    // with a black strip, and the HUD's right edge is cut. Only widths it fills.)
    if (g_softRenderer && (w & 15)) return 0;
    if (bpp == 15 || bpp == 16) return bpp == 16 ? 2 : 1;
    if (g_cfg.ws32bit && !g_softRenderer && (bpp == 32 || bpp == 24)) return bpp == 32 ? 4 : 3;
    return 0;
}
extern "C" int __cdecl ModeOk(int index, BYTE* table) {
    if (index == 1) {                                // first entry: decide the whole list
        memset(g_modeKeep, 0, sizeof(g_modeKeep));
        int n = *(int*)(table + 0x3C);
        BYTE* e = *(BYTE**)(table + 0x40);
        if (n > 255) n = 255;
        struct M { int i, w, h, bpp; } best[256]; int nb = 0;
        for (int i = 1; i <= n; i++) {
            int* m = (int*)(e + i * 40);
            int w = m[0], h = m[1], bpp = m[2];
            int rank = ModeRank(m);
            if (!rank) continue;
            int k = 0; while (k < nb && !(best[k].w == w && best[k].h == h)) k++;
            if (k == nb) { best[nb++] = { i, w, h, bpp }; continue; }
            int rk = ModeRank((int*)(e + best[k].i * 40));
            if (rank > rk) best[k] = { i, w, h, bpp };
        }
        while (nb > 20) {                            // drop the smallest (never 640x480)
            int s = -1;
            for (int k = 0; k < nb; k++) {
                if (best[k].w == 640 && best[k].h == 480) continue;
                if (s < 0 || best[k].w * best[k].h < best[s].w * best[s].h) s = k;
            }
            if (s < 0) break;
            best[s] = best[--nb];
        }
        for (int k = 0; k < nb; k++) g_modeKeep[best[k].i] = true;
        LogfAsync("widescreen: %d display modes listed\n", nb);
    }
    return index >= 0 && index < 256 && g_modeKeep[index];
}

// ---------------------------------------------------------------------------
// Direct3D fog. The driver setup (0x4B59C0, one case per renderer; [0x7A3A58]
// = 1 << renderer index) sets the game's fog mode [0x7A3A64]: 4 for the Glide
// drivers, 0 for the rest. With 0 the game fades distant colours itself
// (0x479860); otherwise it sends fog colour (THRASH state 15), density (14)
// and, per polygon, fog mode (21) = [0x7A3A64] or 0 (cached in [0x554E5C]).
//
// On Glide (voodoo2a) the vertex's rhw is Glide's oow, and the fog table
// (type 4) is fog(w) = (1 - e^(-w/v)) / (1 - e^(-w63/v)) for w = 1/oow, v =
// the density value. Density 0 = no fog (the game sends it at every track
// load, 0x470B90, and for clear weather).
//
// d3da.dll has D3D vertex fog: state 21 = 1 turns it on (FOGENABLE), 0 off,
// 15 sets FOGCOLOR; as it copies each vertex it writes specular =
// table[round(z * 255)] << 24 (255 = no fog) - by z, not w, in 256 steps.
// Its table builder (21 = 4) doesn't match this game's z, and density 0 is
// 1/0 = infinitely thick to it.
//
// So for d3da: the game's mode becomes 4 like Glide's; its setstate is
// wrapped (21 = 4 -> 21 = 1, or 0 while the density is 0; 14 is kept here);
// d3da's 16 specular writes are taken out (its own lookup does nothing then);
// and the draw calls are wrapped to give every vertex, while fog is on,
// Glide's fog for its own w = 1/rhw. Same curve as on Glide, per vertex.
// ---------------------------------------------------------------------------
typedef int (WINAPI *TSetState_t)(int, int);
typedef void (WINAPI *TDraw2_t)(void*, void*);
typedef void (WINAPI *TMesh_t)(int, void*, DWORD*);
static TSetState_t g_oSetState = nullptr;
// the renderer's own setstate and the wrapper the game gets for it (the menus'
// draw wrappers further down: MnSetState notes the texture in use)
static TSetState_t g_mnSetState = nullptr;
static int WINAPI MnSetState(int s, int v);
static TDraw4_t g_fogQuad; static TDraw3_t g_fogTri; static TDrawN_t g_fogFan; static TDraw2_t g_fogLine;
static TMesh_t  g_fogQuadM, g_fogTriM, g_fogLineM;
static int    g_fogValue = 0;               // game's density value (0 = no fog)
static bool   g_fogOn = false;              // polygons drawn with fog right now
static double g_fogD = 0, g_fogNorm = 1;    // density, Glide's normalisation
// specular alpha (255 = clear) for a vertex: Glide's table value for w = 1/rhw
static inline DWORD FogSpec(const BYTE* vtx) {
    float rhw = ((const float*)vtx)[3];
    if (!(rhw > 0.0f) || g_fogD <= 0) return 0xFF000000u;
    double f = (1.0 - exp(-g_fogD / rhw)) / g_fogNorm;
    int a = 255 - (int)(f * 255.0 + 0.5);
    a = a < 0 ? 0 : a > 255 ? 255 : a;
    return (DWORD)a << 24;
}
static inline void FogCopy(BYTE* dst, const void* src) {
    memcpy(dst, src, 32); *(DWORD*)(dst + 0x14) = FogSpec(dst);
}
static void WINAPI FogQuad(void* a, void* b, void* c, void* d) {
    if (!g_fogOn) return g_fogQuad(a, b, c, d);
    BYTE v[4][32]; FogCopy(v[0], a); FogCopy(v[1], b); FogCopy(v[2], c); FogCopy(v[3], d);
    g_fogQuad(v[0], v[1], v[2], v[3]);
}
static void WINAPI FogTri(void* a, void* b, void* c) {
    if (!g_fogOn) return g_fogTri(a, b, c);
    BYTE v[3][32]; FogCopy(v[0], a); FogCopy(v[1], b); FogCopy(v[2], c);
    g_fogTri(v[0], v[1], v[2]);
}
static void WINAPI FogLine(void* a, void* b) {
    if (!g_fogOn) return g_fogLine(a, b);
    BYTE v[2][32]; FogCopy(v[0], a); FogCopy(v[1], b);
    g_fogLine(v[0], v[1]);
}
// growing vertex buffer (game thread only)
static BYTE* g_fogBuf = nullptr; static int g_fogBufN = 0;
static BYTE* FogBuf(int n) {
    if (n > g_fogBufN) {
        free(g_fogBuf); g_fogBufN = n + 1024;
        g_fogBuf = (BYTE*)malloc((size_t)g_fogBufN * 32);
        if (!g_fogBuf) g_fogBufN = 0;
    }
    return g_fogBuf;
}
// drawtrifan(triangles, vertices): triangles + 2 vertices in a row
static void WINAPI FogFan(int tris, void* verts) {
    int n = tris + 2;
    BYTE* b = (g_fogOn && tris > 0 && verts) ? FogBuf(n) : nullptr;
    if (!b) return g_fogFan(tris, verts);
    for (int i = 0; i < n; i++) FogCopy(b + i * 32, (BYTE*)verts + i * 32);
    g_fogFan(tris, b);
}
// meshes: (count, vertices, DWORD indices: count * per)
static void FogMesh(TMesh_t f, int n, void* verts, DWORD* idx, int per) {
    if (!g_fogOn || n <= 0 || !verts || !idx) return f(n, verts, idx);
    DWORD mx = 0;
    for (int i = 0; i < n * per; i++) if (idx[i] > mx) mx = idx[i];
    BYTE* b = mx < 65536 ? FogBuf((int)mx + 1) : nullptr;
    if (!b) return f(n, verts, idx);
    for (DWORD i = 0; i <= mx; i++) FogCopy(b + i * 32, (BYTE*)verts + i * 32);
    f(n, b, idx);
}
static void WINAPI FogQuadM(int n, void* v, DWORD* i) { FogMesh(g_fogQuadM, n, v, i, 4); }
static void WINAPI FogTriM(int n, void* v, DWORD* i)  { FogMesh(g_fogTriM, n, v, i, 3); }
static void WINAPI FogLineM(int n, void* v, DWORD* i) { FogMesh(g_fogLineM, n, v, i, 2); }

// The cockpit picture and the loading screen (drawn between XfBegin and
// XfEnd) are 2D: on Glide their w is tiny so fog never touches them. Fog is
// off while they're drawn and what the game asks for meanwhile is applied
// afterwards.
static bool g_fogPaused = false, g_fogWanted = false;
static int WINAPI FogSetState(int s, int v);
static void FogPause(bool pause) {
    if (!g_oSetState || pause == g_fogPaused) return;
    if (pause) {
        g_fogWanted = g_fogOn;
        g_fogPaused = true;
        if (g_fogOn) { g_fogOn = false; g_oSetState(21, 0); }
    } else {
        g_fogPaused = false;
        if (g_fogWanted) FogSetState(21, 4);
    }
}
static int WINAPI FogSetState(int s, int v) {
    if (g_fogPaused && s == 21) { g_fogWanted = v == 4; return 1; }
    if (s == 21 && v == 4) {
        if (g_fogValue <= 0) { g_fogOn = false; return g_oSetState(21, 0); }
        g_fogOn = true;
        return g_oSetState(21, 1);
    }
    if (s == 21) g_fogOn = false;
    if (s == 14) {
        if (v != g_fogValue) {
            g_fogValue = v;
            g_fogD = v > 0 ? 1.0 / v : 0.0;
            g_fogNorm = g_fogD > 0 ? 1.0 - exp(-52428.8 * g_fogD) : 1.0;   // w63 = 2^18 / 5
            if (g_fogNorm <= 0) g_fogNorm = 1;
        }
        return 1;
    }
    return g_oSetState(s, v);
}
// d3da.dll as analysed: setstate at +0x2B58, and the 16 places that write the
// table's fog into a vertex's specular (mov [..+0x14], reg)
struct FogSite { DWORD rva; BYTE len; BYTE b[4]; };
static const FogSite kFogSites[] = {
    { 0x43C5, 4, { 0x89, 0x4C, 0x06, 0x14 } }, { 0x4443, 4, { 0x89, 0x54, 0x06, 0x14 } },
    { 0x44C9, 4, { 0x89, 0x54, 0x01, 0x14 } }, { 0x459C, 4, { 0x89, 0x4C, 0x06, 0x14 } },
    { 0x4632, 4, { 0x89, 0x4C, 0x07, 0x14 } }, { 0x46D0, 3, { 0x89, 0x48, 0x14 } },
    { 0x47A9, 4, { 0x89, 0x74, 0x01, 0x14 } }, { 0x4813, 4, { 0x89, 0x74, 0x01, 0x14 } },
    { 0x4882, 4, { 0x89, 0x44, 0x29, 0x14 } }, { 0x48ED, 4, { 0x89, 0x74, 0x01, 0x14 } },
    { 0x49FF, 4, { 0x89, 0x4C, 0x06, 0x14 } }, { 0x4A62, 4, { 0x89, 0x4C, 0x06, 0x14 } },
    { 0x4AC5, 4, { 0x89, 0x4C, 0x06, 0x14 } }, { 0x4B1E, 4, { 0x89, 0x5C, 0x01, 0x14 } },
    { 0x4CCA, 3, { 0x89, 0x42, 0xF4 } },       { 0x4D89, 4, { 0x89, 0x44, 0x1E, 0x14 } },
};
static bool FogPatchDriver(DWORD setstate) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)setstate, &m) || !m) return false;
    DWORD base = (DWORD)m;
    if (setstate - base != 0x2B58) return false;
    for (const FogSite& s : kFogSites)
        if (!Readable(base + s.rva, s.len) || memcmp((void*)(base + s.rva), s.b, s.len)) return false;
    for (const FogSite& s : kFogSites) {
        static const BYTE nop[4] = { 0x90, 0x90, 0x90, 0x90 };
        if (!PatchBytes(base + s.rva, nop, s.len)) return false;
    }
    return true;
}
extern "C" void __cdecl D3dFogInit() {
    *(volatile DWORD*)0x7A3A64 = 0;
    if (!g_cfg.d3dFog) return;
    DWORD ss = Rd<DWORD>(0x9EF96C);
    TDraw4_t q = Rd<TDraw4_t>(0x9EF94C); TDraw3_t tr = Rd<TDraw3_t>(0x9EF974); TDrawN_t fan = Rd<TDrawN_t>(0x9EF960);
    TDraw2_t ln = Rd<TDraw2_t>(0x9EF928);
    TMesh_t qm = Rd<TMesh_t>(0x9EF930), tm = Rd<TMesh_t>(0x9EF924), lm = Rd<TMesh_t>(0x9EF90C);
    if (!ss || !q || !tr || !fan || !ln || !qm || !tm || !lm || ss == (DWORD)&FogSetState) return;
    if (!FogPatchDriver(ss == (DWORD)&MnSetState ? (DWORD)g_mnSetState : ss)) { LogfAsync("d3d fog: off - d3da.dll isn't the analysed version\n"); return; }
    g_fogValue = 0; g_fogD = 0; g_fogNorm = 1; g_fogOn = false;
    g_oSetState = (TSetState_t)ss; g_fogQuad = q; g_fogTri = tr; g_fogFan = fan; g_fogLine = ln;
    g_fogQuadM = qm; g_fogTriM = tm; g_fogLineM = lm;
    *(volatile DWORD*)0x9EF94C = (DWORD)&FogQuad;
    *(volatile DWORD*)0x9EF974 = (DWORD)&FogTri;
    *(volatile DWORD*)0x9EF960 = (DWORD)&FogFan;
    *(volatile DWORD*)0x9EF928 = (DWORD)&FogLine;
    *(volatile DWORD*)0x9EF930 = (DWORD)&FogQuadM;
    *(volatile DWORD*)0x9EF924 = (DWORD)&FogTriM;
    *(volatile DWORD*)0x9EF90C = (DWORD)&FogLineM;
    *(volatile DWORD*)0x9EF96C = (DWORD)&FogSetState;
    *(volatile DWORD*)0x7A3A64 = 4;
    LogfAsync("d3d fog: on\n");
}

// ---------------------------------------------------------------------------
// Race HUD. Its panels sit at the edges of the view rect (0x749758...), but
// every size in it - insets, sprites (0x47F650), digits, the map and lap
// list - is the screen width [0x7CDAC8] times a 1/640 factor, on both axes,
// so on a wide screen everything is wider and taller. Those reads (found by
// way of VEG's widescreen patch, which changes the same ones) get the width
// of a 4:3 screen of the same height instead: min(W, H * 4/3), the same as
// W on 4:3 screens and in the 640x480 menus. Updated after every mode set.
// ---------------------------------------------------------------------------
static const DWORD kHudWidthOps[] = {           // operand of fild/mov [0x7CDAC8]
    0x47F65C, 0x47F675, 0x47F6AA, 0x47F7DF, 0x47F810, 0x47F84A, 0x47F936, 0x47F980,
    0x47F9BA, 0x47FA6D, 0x47FA86, 0x47FAF1, 0x480189, 0x480FCF, 0x480FFD, 0x48101C,
    0x48103A, 0x481074, 0x48109A, 0x4810C2, 0x4810EC, 0x481324, 0x48134B, 0x481370,
    0x481391, 0x4813D2, 0x481408, 0x481436, 0x481478, 0x481AE7, 0x481B00, 0x481B1F,
    0x481B3D, 0x481B74, 0x481B99, 0x481BC1, 0x481BEB, 0x481D0E, 0x481D46, 0x481DE8,
    0x481E20, 0x481E92, 0x481EB7, 0x481EF5, 0x481F3D, 0x484B7B, 0x48723A, 0x487253,
    0x487286, 0x4872B5, 0x48A672, 0x48A716, 0x48AC08, 0x48AE11, 0x48AE89, 0x48AEA2,
    0x48D40C,
};
extern "C" void __cdecl HudUpdate() {
    int w = Rd<int>(0x7CDAC8), h = Rd<int>(0x7CDACC);
    int hw = w;
    if (w > 0 && h > 0) { int w43 = (h * 4 + 1) / 3; if (w43 < w) hw = w43; }
    if (hw <= 0) hw = 640;
    if (hw != g_hudW) LogfAsync("widescreen: HUD sized for %d wide (screen %dx%d)\n", hw, w, h);
    g_hudW = hw;
    g_pzMeasF = (g_pzMeasIn && w > hw) ? (float)w / (float)hw : 1.0f;
}
// The boxes themselves (0x480910, 23 per player at 0x749758 + player * 0x170,
// 16 bytes each: x0, y0, x1, y1) are layout fractions times the view's width
// and height. Their x is redone in 4:3 proportions, kept at the left, the
// right or the middle by where the box's middle is (like High Stakes'
// layout boxes in HSFrameFix); boxes nearly as wide as the view stay as
// they are.
static inline double HudZoneOff(double f0, double f1, double extra);
extern "C" void __cdecl HudRectPost() {
    int W = g_hrW, H = g_hrH, sw = Rd<int>(0x7CDAC8), sh = Rd<int>(0x7CDACC);
    if (W <= 0 || H <= 0 || sw <= 0 || sh <= 0 || g_hrPlayer < 0 || g_hrPlayer > 1 || g_hrIdx < 0 || g_hrIdx >= 23) return;
    double k = sw * 3.0 / (sh * 4.0);                 // screen aspect over 4:3
    if (k <= 1.001) return;
    int* r = (int*)(0x749758 + g_hrPlayer * 0x170 + g_hrIdx * 16);
    double f0 = (double)r[0] / W, f1 = (double)r[2] / W;
    if (f1 - f0 > 0.9) return;
    double w43 = W / k, extra = W - w43;
    double off = HudZoneOff(f0, f1, extra);
    r[0] = (int)lround(w43 * f0 + off);
    r[2] = (int)lround(w43 * f1 + off);
}
// The other way round. The HUD's positions can be changed in the game (the
// in-race menu's "Heads Up Display": every box becomes something to drag),
// and there the game works from the boxes as they are on the screen: each
// drag item starts at its box (0x45CA..), and 0x459DB0 turns an item's
// place back into the layout's fractions - x over the screen width - and
// has the boxes laid out again. With the boxes moved and narrowed above,
// that put the narrowed box into the layout, to be narrowed again: every
// visit squeezed the boxes further, and the result was kept with the player's
// settings (dials stretched tall, on 4:3 screens as well). So the fractions
// 0x459DB0 has worked out ([item+0x44] = left, [item+0x4C] = right) are taken
// back through the rule above before they go into the layout (0x459EBD): laid
// out again, the box is exactly where the item was dragged to.
static inline double HudZoneOff(double f0, double f1, double extra) {
    double c = (f0 + f1) * 0.5;
    return c < 0.4 ? 0.0 : c > 0.6 ? extra : extra * 0.5;
}
extern "C" void __cdecl HudFracInverse(BYTE* item) {
    int sw = Rd<int>(0x7CDAC8), sh = Rd<int>(0x7CDACC);
    if (!item || sw <= 0 || sh <= 0) return;
    double k = sw * 3.0 / (sh * 4.0);
    if (k <= 1.001) return;
    float* fx0 = (float*)(item + 0x44); float* fx1 = (float*)(item + 0x4C);
    double a = *fx0, b = *fx1;                        // the box on the screen, over the screen's width
    if (b - a > 0.9) return;                          // (left as they are, both ways)
    double w43 = sw / k, extra = sw - w43;
    static const double kOff[3] = { 0.5, 0.0, 1.0 };  // middle, left, right
    double g0 = 0, g1 = 0; bool found = false;
    for (int i = 0; i < 3 && !found; i++) {
        double off = extra * kOff[i];
        g0 = (a * sw - off) / w43; g1 = (b * sw - off) / w43;
        found = g1 - g0 <= 0.9 && fabs(HudZoneOff(g0, g1, extra) - off) < 0.5;
    }
    if (!found) {                                     // (a place no layout gives: the nearest side's rule)
        double c = (a + b) * 0.5, off = c < 1.0 / 3 ? 0.0 : c > 2.0 / 3 ? extra : extra * 0.5;
        g0 = (a * sw - off) / w43; g1 = (b * sw - off) / w43;
    }
    *fx0 = (float)g0; *fx1 = (float)g1;
}
// The rear-view mirror. Its frame is one of those boxes, so with the HUD in
// 4:3 proportions it's narrower than before - but the picture inside is a 3D
// view with a rect of its own (0x7CDB48: x, y, w, h for the projection, then
// x, y, w, h for the clip), set by the view rect setup (0x4BE5E0) as fractions
// of the screen, and it stuck out of the frame on both sides. The rect gets the
// same treatment as the boxes, after every setup; and as that gives it the
// shape it has on a 4:3 screen, its projection keeps the game's own angle
// instead of the wider Hor+ one (MirrorProjStub, WsHorPlus). Only for the
// outside views' layout (0): the cockpit's mirror (layout 1) is a hole in the
// cockpit picture, which has a transform of its own, and is left as it is.
extern "C" void __cdecl MirrorRectFix() {
    g_mirrorNarrowed = false;
    // the screen's size as this setup knows it: the main view's rect (0x7CDB88),
    // which it has just written. (The screen rect at 0x7CDAC0, which the HUD
    // code reads later, still holds the last mode's size at this point: on the
    // way into a race that's the menus' 640x480, and nothing was narrowed.)
    int sw = Rd<int>(0x7CDB90), sh = Rd<int>(0x7CDB94);
    {
        static int lw = -1, lh = -1, ll = -1;         // game thread only
        if (sw != lw || sh != lh || g_mirrorLayout != ll) {
            lw = sw; lh = sh; ll = g_mirrorLayout;
            LogfAsync("widescreen: view rects set up for %dx%d, layout %d (screen rect %dx%d, mode %dx%d)\n", sw, sh, g_mirrorLayout,
                      Rd<int>(0x7CDAC8), Rd<int>(0x7CDACC), Rd<int>(0x7CDAE0), Rd<int>(0x7CDAE4));
        }
    }
    if (g_mirrorLayout != 0 || sw <= 0 || sh <= 0 || Rd<int>(0x6FD3B0) == 1) return;   // (split screen: left as it is)
    double k = sw * 3.0 / (sh * 4.0);
    if (k <= 1.001) return;
    int* r = (int*)0x7CDB48;
    double w43 = sw / k, extra = sw - w43;
    for (int part = 0; part < 2; part++, r += 4) {                // projection rect, clip rect
        if (r[2] <= 0) continue;
        double f0 = (double)r[0] / sw, f1 = (double)(r[0] + r[2]) / sw;
        if (f0 < 0 || f1 > 1.0 || f1 - f0 > 0.9) continue;
        double c = (f0 + f1) * 0.5, off = c < 0.4 ? 0.0 : c > 0.6 ? extra : extra * 0.5;
        int x0 = (int)lround(w43 * f0 + off), x1 = (int)lround(w43 * f1 + off);
        if (part == 0) {
            g_mirrorNarrowed = true;
            static int lx = -1, lw = -1;              // game thread only
            if (x0 != lx || x1 - x0 != lw) {
                lx = x0; lw = x1 - x0;
                LogfAsync("widescreen: mirror picture x %d..%d -> %d..%d\n", r[0], r[0] + r[2], x0, x1);
            }
        }
        r[0] = x0; r[2] = x1 - x0;
    }
}
static bool InstallHud() {
    for (DWORD a : kHudWidthOps) if (Rd<DWORD>(a) != 0x7CDAC8) { Logf("widescreen: HUD site 0x%06lX differs - HUD left as it is\n", (unsigned long)a); return false; }
    g_hudW = 640;
    DWORD var = (DWORD)&g_hudW;
    bool ok = true;
    for (DWORD a : kHudWidthOps) ok &= PatchBytes(a, &var, 4);
    BYTE f[6] = { 0xDB, 0x05, 0, 0, 0, 0 };          // fld [width float] -> fild [g_hudW]
    memcpy(f + 2, &var, 4);
    ok &= PatchBytes(0x48AB47, f, 6) && PatchBytes(0x48ABFA, f, 6);
    ok &= PatchRel32(0x4BEF50, 0xE9, (void*)&HudModeStub);
    ok &= PatchRel32(0x480910, 0xE9, (void*)&HudRectStub);
    static const BYTE nop2[2] = { 0x90, 0x90 };
    ok &= PatchRel32(0x459EBD, 0xE8, (void*)&HudFracStub) && PatchBytes(0x459EC2, nop2, 2);
    ok &= PatchRel32(0x4BE8C4, 0xE8, (void*)&MirrorRectStub) && PatchRel32(0x4DBE0B, 0xE8, (void*)&MirrorProjStub);
    g_hudIn = ok;
    return ok;
}

// ---------------------------------------------------------------------------
// Menus at the race resolution. The game shows its menus in a 640x480 mode
// ([0x55FEC8], set at startup 0x4B60FD, back from a race 0x4A35E8 and after a
// reset 0x44B1E5) and the race in the one picked in the options
// (GetSelectedMode 0x4482F0 -> 0x4A3422). SetVideoMode (0x4BEF50) copies the
// requested mode's entry to [0x7CDAE0] - the size the game draws for - and
// calls the renderer's setvideomode(mode, depth, flag).
//
// Around the menu calls the renderer gets another mode of the same colour
// depth (the movie player writes pixels in the menu mode's format): the race
// resolution (or the desktop's). The game still draws for 640x480, so the
// renderer's draw calls - wrapped when the game looks them up (GetProcAddress,
// from ThrashLoadDriver 0x4F8390) - move every vertex from 640x480 to a 4:3
// area in the middle (or stretch it), clip rects too; the bars are cleared
// before each page flip. The movie player (MAD, 0x517580...) locks the frame
// and writes a 640x480 picture at its top left (it's told the frame is
// 640x480), which is then scaled up in place. The software renderer draws
// screen-aligned quads as unscaled blits, so its menus stay 640x480.
//
// The game saves only the race resolution's width ([0x6FBC18]) and at startup
// looks for a 4:3 15/16-bit mode of that width (0x4BEE80, at 0x47327E,
// 0x4B610C and for the defaults 0x472ECD); with every resolution listed that
// finds the wrong mode or none. So the race resolution is kept in the ini
// (wsRaceWidth/Height) and the lookup answers with the listed mode of that
// size (or of the 4:3 size its own lookup found, as the list has it).
// ---------------------------------------------------------------------------
typedef void* (WINAPI *TAbout_t)();
typedef int   (WINAPI *TSetMode_t)(int, int, int);
typedef void  (WINAPI *TVoidFn_t)();
typedef void* (WINAPI *TLockFn_t)();
typedef void  (WINAPI *TUnlockFn_t)(void*);
typedef void  (WINAPI *TP1_t)(void*);
typedef void  (WINAPI *TP2_t)(void*, void*);
typedef void  (WINAPI *TP3_t)(void*, void*, void*);
typedef void  (WINAPI *TP4_t)(void*, void*, void*, void*);
typedef void  (WINAPI *TStrip_t)(int, void*);
typedef void  (WINAPI *TMeshFn_t)(int, void*, DWORD*);
typedef int   (WINAPI *TClipFn_t)(int, int, int, int);
static TAbout_t g_mnAbout; static TSetMode_t g_mnSetMode; static TVoidFn_t g_mnFlip, g_mnClear;
static TLockFn_t g_mnLock; static TUnlockFn_t g_mnUnlock;
static TP1_t g_mnPoint; static TP2_t g_mnLine; static TP3_t g_mnTri; static TP4_t g_mnQuad;
static TStrip_t g_mnFan, g_mnStrip, g_mnLStrip, g_mnPStrip;
static TMeshFn_t g_mnTriM, g_mnQuadM, g_mnLineM, g_mnPointM;
static TClipFn_t g_mnClip;
static HMODULE g_mnMod = nullptr;
static bool g_mnSoft = false;                     // software renderer: menus left alone
static volatile bool g_mnXf = false;              // menus mapped right now
// The software renderer's menus (wsMenusSoft): it can't be given its pictures
// at another size - it fits a quad's whole texture to it whatever the u/v say,
// and as triangles the pictures come out with smeared edges and doubled
// letters. But it draws into memory, so the menus are left exactly as the game
// draws them, 640x480 at the top left of the bigger screen, and the finished
// frame is scaled to the menu area in place just before each page flip
// (MnFlip), as a movie's frames already are. g_mnSoftFrame: that's on.
static bool g_mnSoftFrame = false;
static float g_mnSx = 1, g_mnSy = 1, g_mnOx = 0, g_mnOy = 0;
static int g_mnRealW = 0, g_mnRealH = 0, g_mnRealBpp = 16, g_mnRealIdx = 0;
static int g_mnFeCall = 0, g_mnFeTarget = 0;
static int g_mnClipRect[4] = { 0, 0, 640, 480 };  // as the game set it
static int g_dispW = 0, g_dispH = 0;              // the desktop at startup

static int* ModeEntryPtr(int idx) {
    if (!g_mnAbout || idx < 1) return nullptr;
    BYTE* a = (BYTE*)g_mnAbout();
    if (!a || IsBadReadPtr(a, 0x44)) return nullptr;
    int n = *(int*)(a + 0x3C); BYTE* e = *(BYTE**)(a + 0x40);
    if (idx > n || !e || IsBadReadPtr(e + idx * 40, 40)) return nullptr;
    return (int*)(e + idx * 40);
}
// the mode of that size: bpp > 0 = that colour depth (15 and 16 count as one),
// else the one the options list would show
static int FindModeWH(int w, int h, int bpp) {
    if (!g_mnAbout || w <= 0 || h <= 0) return 0;
    BYTE* a = (BYTE*)g_mnAbout();
    if (!a || IsBadReadPtr(a, 0x44)) return 0;
    int n = *(int*)(a + 0x3C), best = 0, bestRank = 0;
    for (int i = 1; i <= n && i < 256; i++) {
        int* m = ModeEntryPtr(i);
        if (!m || m[0] != w || m[1] != h) continue;
        if (bpp > 0) {
            bool same = m[2] == bpp || ((m[2] == 15 || m[2] == 16) && (bpp == 15 || bpp == 16));
            if (same && (!best || m[2] == bpp)) best = i;
            continue;
        }
        int r = ModeRank(m);
        if (r > bestRank) { bestRank = r; best = i; }
    }
    return best;
}

// ---- draw wrappers: 640x480 -> the screen, while the menus are mapped -----
struct MnV { float f[8]; };
static bool g_pzPending = false;                  // in-race menus: the next vertex decides where its item goes (PzDecide)
static void PzDecide(float x, float y);
// alternatePixelCenter: Direct3D (as d3da.dll uses it) takes a pixel's centre to
// be at whole coordinates, where the game's 2D pictures are laid out for
// pixels that start there. So every vertex on its way to the Direct3D
// renderer is moved half a pixel up and left; pictures drawn 1:1 then land
// texel on pixel. Not the cockpit picture (0x489240: dashboard pieces,
// needles, wheel; g_xfCockpit): the game gives those pieces u and v already
// moved by half a texel (0.5/256 to 1 + 0.5/256 for a whole 256 piece), so
// they are texel on pixel as they come and the move would blur them.
static bool g_apc = false;                        // on for the renderer loaded now
// menuTexelInset: the in-race menus' pictures are laid out for 640x480, a texel
// to a pixel. Enlarged for a bigger screen and filtered, a picture's outermost
// pixels sit up to half a texel past its outermost texels' centres and take in
// what's beyond the picture (the texture's other side, or its neighbour) - a
// thin line beside a button. So while such a menu is drawn (g_mnInset, with
// the screen's size over 640x480 in g_mnInsetX/Y) a rectangle's u and v are
// pulled in by half a texel, a texel being what one 640x480 pixel covers.
//
// The front-end menus enlarged by wsMenus need the same (g_mnFeInset; there the
// game's coordinates are 640x480's, so the scale is 1). And their big pictures -
// backgrounds, titles, cut into tiles - the game draws with the filter off
// (0x4D7160: setstate(7, 0) around them when [0x7A3A58] has 0x40 and the
// picture is more than 10 wider than high or the reverse; d3da.dll's state 7 is
// D3DRENDERSTATE_TEXTUREMAG/MIN, 0 = nearest, 1 = linear), surely so the tiles
// don't show their edges. Enlarged, unfiltered looks jagged; with the tiles'
// u and v pulled in the filter can stay on (wsMenuFilter, MnFeFilterState in
// place of that call).
//
// How much half a texel is: with d3da.dll a texture's handle (setstate 1, or
// settexture) points at its record, which starts with its width and height
// (talloc, +0x3534, stores its first two arguments there), and u and v run 0
// to 1 over it. That's used when it reads sanely; otherwise a texel is taken
// to be what one 640x480 pixel of the rectangle covers, which is right for a
// picture drawn at its own size and too little for one drawn bigger.
static void* g_mnTex = nullptr;                   // the texture in use, as the game last set it
typedef void (WINAPI *TSetTex_t)(void*);
static TSetTex_t g_mnSetTex = nullptr;
static int WINAPI MnSetState(int s, int v) { if (s == 1) g_mnTex = (void*)v; return g_mnSetState(s, v); }
static void WINAPI MnSetTex(void* h) { g_mnTex = h; g_mnSetTex(h); }
static bool g_mnD3D = false, g_mnInset = false, g_mnFeInset = false;
// Filtered, a picture with see-through parts gets a dark rim: the menus'
// pictures of that kind are 16-bit with one bit of alpha (THRASH type 3, the
// game's 0x7E; 0x4D5F40 picks the type), their see-through texels are black,
// and the filter mixes that black into the picture's outermost texels (the
// reason the game draws them unfiltered). So when the front-end hands such a
// picture to the renderer (tupdate, while the menus are enlarged), every
// see-through texel that touches the picture is given the average colour of
// the picture's texels around it; it stays see-through, and the rim mixes with
// its own colour. d3da.dll has colour keying off (render state 41 = 0), so the
// colour of a see-through texel matters to nothing else.
typedef int (WINAPI *TTUpdate_t)(void*, void*, int);
static TTUpdate_t g_mnTUpdate = nullptr;
static int WINAPI MnTUpdate(void* h, void* data, int lvl) {
    if (g_mnFeInset && lvl == 0 && g_mnD3D && g_cfg.wsMenuFilter && h && data && !IsBadReadPtr(h, 0x14)) {
        const int* r = (const int*)h; int w = r[0], hh = r[1];
        if (r[4] == 3 && w >= 2 && hh >= 2 && w <= 1024 && hh <= 1024 && !IsBadWritePtr(data, (size_t)w * hh * 2)) {
            static WORD* cp = nullptr;
            if (!cp) cp = (WORD*)malloc((size_t)1024 * 1024 * 2);
            if (cp) {
                WORD* px = (WORD*)data;
                memcpy(cp, px, (size_t)w * hh * 2);
                for (int y = 0; y < hh; y++) for (int x = 0; x < w; x++) {
                    if (cp[y * w + x] & 0x8000) continue;
                    int n = 0, cr = 0, cg = 0, cb = 0;
                    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                        int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= hh) continue;
                        WORD c = cp[yy * w + xx];
                        if (c & 0x8000) { n++; cr += (c >> 10) & 31; cg += (c >> 5) & 31; cb += c & 31; }
                    }
                    if (n) px[y * w + x] = (WORD)(((cr / n) << 10) | ((cg / n) << 5) | (cb / n));
                }
                static bool logged = false;
                if (!logged) { logged = true; LogfAsync("menus: see-through pictures get their rims coloured for the filter\n"); }
            }
        }
    }
    return g_mnTUpdate(h, data, lvl);
}
// The menus' thin lines (the stripes that slide about the background) are
// plain rectangles two 640x480 pixels high, drawn by the game every frame.
// Enlarged by a factor that isn't whole their edges fall inside pixels, and
// the renderer, which draws a pixel or doesn't, makes them two pixels thick
// here and three there. So in the enlarged front-end a rectangle with no
// texture, up to four game pixels thin, is drawn as its whole rows plus the
// rows its top and bottom edges cut through, those with the vertex alpha
// scaled by how much of the row the line covers - what the filter does to the
// pictures around it. (The menus draw with alpha blending on: their faded
// pictures are plain vertex alpha in the same run of calls.) Its left and
// right ends go to whole pixels. Returns false if it isn't such a rectangle.
static bool MnPlainLine(MnV* const q[4], const void* const src[4]) {
    float lo[2], hi[2], tlo[2], thi[2];
    for (int j = 0; j < 2; j++) {
        lo[j] = hi[j] = ((const float*)src[0])[j]; tlo[j] = thi[j] = q[0]->f[j];
        for (int i = 1; i < 4; i++) {
            float f = ((const float*)src[i])[j], t = q[i]->f[j];
            if (f < lo[j]) { lo[j] = f; tlo[j] = t; }
            if (f > hi[j]) { hi[j] = f; thi[j] = t; }
        }
        for (int i = 0; i < 4; i++) { float f = ((const float*)src[i])[j]; if (f != lo[j] && f != hi[j]) return false; }
        if (hi[j] <= lo[j]) return false;
    }
    if (hi[1] - lo[1] > 4.0f) return false;
    // (a pixel row r covers r to r + 1 of c = the vertex's y + 0.5, with or without alternatePixelCenter)
    float x0 = floorf(tlo[0] + 1.0f) - 0.5f, x1 = floorf(thi[0] + 1.0f) - 0.5f;
    float c0 = tlo[1] + 0.5f, c1 = thi[1] + 0.5f, r0 = floorf(c0 + 0.01f), r1 = floorf(c1 + 0.01f);
    struct Row { float a, b, cov; } rows[3]; int n = 0;
    if (r0 == r1) rows[n++] = { r0, r0 + 1.0f, c1 - c0 };
    else {
        float core = r0;
        if (c0 - r0 > 0.01f) { rows[n++] = { r0, r0 + 1.0f, r0 + 1.0f - c0 }; core = r0 + 1.0f; }
        if (r1 > core) rows[n++] = { core, r1, 1.0f };
        if (c1 - r1 > 0.01f) rows[n++] = { r1, r1 + 1.0f, c1 - r1 };
    }
    for (int k = 0; k < n; k++) {
        MnV v[4];
        for (int i = 0; i < 4; i++) {
            v[i] = *q[i];
            const float* f = (const float*)src[i];
            v[i].f[0] = f[0] == lo[0] ? x0 : x1;
            v[i].f[1] = (f[1] == lo[1] ? rows[k].a : rows[k].b) - 0.5f;
            DWORD c; memcpy(&c, &v[i].f[4], 4);
            DWORD al = (DWORD)lrintf((float)(c >> 24) * (rows[k].cov > 1.0f ? 1.0f : rows[k].cov));
            c = (c & 0x00FFFFFF) | (al << 24); memcpy(&v[i].f[4], &c, 4);
        }
        g_mnQuad(&v[0], &v[1], &v[2], &v[3]);
    }
    return true;
}
static float g_mnInsetX = 1, g_mnInsetY = 1;
static inline bool MnInsetOn() { return g_mnInset || (g_mnFeInset && g_mnXf); }
static inline bool MnActive() { return g_mnXf || g_apc || g_mnInset; }
static int WINAPI MnFeFilterState(int s, int v) {
    TSetState_t f = Rd<TSetState_t>(0x9EF96C);
    if (!f) return 0;
    if (s == 7 && v == 0 && g_cfg.wsMenuFilter && g_mnFeInset && g_mnXf) v = 1;
    return f(s, v);
}
static void MnInset(MnV* const q[4], const void* const src[4]) {
    float lo[4], hi[4];                           // x, y, u, v as the game gave them
    static const int at[4] = { 0, 1, 6, 7 };
    for (int j = 0; j < 4; j++) {
        lo[j] = hi[j] = ((const float*)src[0])[at[j]];
        for (int i = 1; i < 4; i++) {
            float f = ((const float*)src[i])[at[j]];
            if (f < lo[j]) lo[j] = f;
            if (f > hi[j]) hi[j] = f;
        }
    }
    for (int i = 0; i < 4; i++) {                 // a rectangle with its picture upright only
        const float* f = (const float*)src[i];
        if ((f[0] != lo[0] && f[0] != hi[0]) || (f[1] != lo[1] && f[1] != hi[1]) ||
            (f[6] != lo[2] && f[6] != hi[2]) || (f[7] != lo[3] && f[7] != hi[3])) return;
    }
    const float sc[2] = { g_mnInset ? g_mnInsetX : 1.0f, g_mnInset ? g_mnInsetY : 1.0f };
    int dim[2] = { 0, 0 };
    if (g_mnD3D && g_mnTex && !IsBadReadPtr(g_mnTex, 8)) {
        memcpy(dim, g_mnTex, 8);
        for (int j = 0; j < 2; j++) if (dim[j] < 1 || dim[j] > 4096 || (dim[j] & (dim[j] - 1))) dim[0] = dim[1] = 0;
    }
    for (int j = 0; j < 2; j++) {
        float d = hi[j] - lo[j], t = hi[2 + j] - lo[2 + j];
        if (d < 1.0f || t <= 0.0f) continue;
        float in = 0.5f * t * sc[j] / d;          // half of what one 640x480 pixel covers
        if (dim[j] && t <= 1.001f && lo[2 + j] >= -0.001f && hi[2 + j] <= 1.001f) in = 0.5f / (float)dim[j];
        if (in > t * 0.25f) in = t * 0.25f;
        for (int i = 0; i < 4; i++) {
            float& u = q[i]->f[6 + j];
            if (u == lo[2 + j]) u += in; else u -= in;
        }
    }
}
static inline void MnXv(MnV& d, const void* s) {
    if (g_pzPending) PzDecide(((const float*)s)[0], ((const float*)s)[1]);
    memcpy(&d, s, 32);
    if (g_mnXf) { d.f[0] = d.f[0] * g_mnSx + g_mnOx; d.f[1] = d.f[1] * g_mnSy + g_mnOy; }
    if (g_apc && !g_xfCockpit) { d.f[0] -= 0.5f; d.f[1] -= 0.5f; }
}
static inline int MnX(int x) { return (int)lrintf(x * g_mnSx + g_mnOx); }
static inline int MnY(int y) { return (int)lrintf(y * g_mnSy + g_mnOy); }
static MnV* g_mnBuf = nullptr; static int g_mnBufN = 0;
static MnV* MnArr(const void* v, int n) {
    if (n > g_mnBufN) {
        free(g_mnBuf); g_mnBufN = n + 1024;
        g_mnBuf = (MnV*)malloc(sizeof(MnV) * g_mnBufN);
        if (!g_mnBuf) { g_mnBufN = 0; return nullptr; }
    }
    for (int i = 0; i < n; i++) MnXv(g_mnBuf[i], (const BYTE*)v + i * 32);
    return g_mnBuf;
}
static void WINAPI MnPoint(void* a) { if (!MnActive()) return g_mnPoint(a); MnV x; MnXv(x, a); g_mnPoint(&x); }
static void WINAPI MnLine(void* a, void* b) { if (!MnActive()) return g_mnLine(a, b); MnV x, y; MnXv(x, a); MnXv(y, b); g_mnLine(&x, &y); }
static void WINAPI MnTri(void* a, void* b, void* c) {
    if (!MnActive()) return g_mnTri(a, b, c);
    MnV x, y, z; MnXv(x, a); MnXv(y, b); MnXv(z, c); g_mnTri(&x, &y, &z);
}
// A rectangle showing a whole texture (u and v 0 to 1), as the tiles of the
// menus' big pictures are: pulling u and v in would shift what each tile shows
// by half a texel towards its middle, a small jog where two tiles meet. For
// those the enlarged front-end has the renderer hold the edge texels instead
// (d3da.dll's state 13: 0 = D3DTADDRESS_CLAMP, 1 = WRAP; the game keeps what
// it last set in [0x554E60]) - nothing moves, and nothing from the far side
// gets in.
static bool MnWholeTex(const void* const src[4]) {
    bool x0 = false, x1 = false, y0 = false, y1 = false;
    for (int i = 0; i < 4; i++) {
        const float* f = (const float*)src[i];
        if ((f[6] != 0.0f && f[6] != 1.0f) || (f[7] != 0.0f && f[7] != 1.0f)) return false;
        (f[6] == 0.0f ? x0 : x1) = true; (f[7] == 0.0f ? y0 : y1) = true;
    }
    return x0 && x1 && y0 && y1;
}
static void WINAPI MnQuad(void* a, void* b, void* c, void* d) {
    if (!MnActive()) return g_mnQuad(a, b, c, d);
    MnV x, y, z, w; MnXv(x, a); MnXv(y, b); MnXv(z, c); MnXv(w, d);
    if (MnInsetOn()) {
        MnV* const q[4] = { &x, &y, &z, &w }; const void* const s[4] = { a, b, c, d };
        if (g_mnTex && !g_mnInset && g_mnD3D && g_mnSetState && MnWholeTex(s)) {
            g_mnSetState(13, 0);
            g_mnQuad(&x, &y, &z, &w);
            g_mnSetState(13, Rd<int>(0x554E60) == 0 ? 0 : 1);
            return;
        }
        if (g_mnTex) MnInset(q, s);
        else if (!g_mnInset && g_cfg.wsMenuFilter && MnPlainLine(q, s)) return;
    }
    g_mnQuad(&x, &y, &z, &w);
}
// strips and fans: (count, vertices); count + extra vertices in a row
static void MnStripCall(TStrip_t f, int n, void* v, int extra) {
    if (!MnActive() || n <= 0 || !v) return f(n, v);
    MnV* b = MnArr(v, n + extra);
    if (!b) return f(n, v);
    if (MnInsetOn() && f == g_mnFan && n == 2) {    // (a rectangle as a fan)
        MnV* const q[4] = { b, b + 1, b + 2, b + 3 };
        const BYTE* p = (const BYTE*)v; const void* const s[4] = { p, p + 32, p + 64, p + 96 };
        MnInset(q, s);
    }
    f(n, b);
}
static void WINAPI MnFan(int n, void* v)    { MnStripCall(g_mnFan, n, v, 2); }
static void WINAPI MnStrip(int n, void* v)  { MnStripCall(g_mnStrip, n, v, 2); }
static void WINAPI MnLStrip(int n, void* v) { MnStripCall(g_mnLStrip, n, v, 1); }
static void WINAPI MnPStrip(int n, void* v) { MnStripCall(g_mnPStrip, n, v, 0); }
static void MnMeshCall(TMeshFn_t f, int n, void* v, DWORD* idx, int per) {
    if (!MnActive() || n <= 0 || !v || !idx) return f(n, v, idx);
    DWORD mx = 0;
    for (int i = 0; i < n * per; i++) if (idx[i] > mx) mx = idx[i];
    MnV* b = mx < 65536 ? MnArr(v, (int)mx + 1) : nullptr;
    if (!b) return f(n, v, idx);
    f(n, b, idx);
}
static void WINAPI MnTriM(int n, void* v, DWORD* i)   { MnMeshCall(g_mnTriM, n, v, i, 3); }
static void WINAPI MnQuadM(int n, void* v, DWORD* i)  { MnMeshCall(g_mnQuadM, n, v, i, 4); }
static void WINAPI MnLineM(int n, void* v, DWORD* i)  { MnMeshCall(g_mnLineM, n, v, i, 2); }
static void WINAPI MnPointM(int n, void* v, DWORD* i) { MnMeshCall(g_mnPointM, n, v, i, 1); }
static int MnClipNow(int x0, int y0, int x1, int y1) {
    return g_mnXf ? g_mnClip(MnX(x0), MnY(y0), MnX(x1), MnY(y1)) : g_mnClip(x0, y0, x1, y1);
}
static int WINAPI MnClipW(int x0, int y0, int x1, int y1) {
    g_mnClipRect[0] = x0; g_mnClipRect[1] = y0; g_mnClipRect[2] = x1; g_mnClipRect[3] = y1;
    return MnClipNow(x0, y0, x1, y1);
}
// the bars: the clear only covers the clip rect, so each bar is cleared on its own
static void MnClearBars() {
    if (!g_mnClip || !g_mnClear || g_mnRealW <= 0) return;
    int x0 = MnX(0), y0 = MnY(0), x1 = MnX(640), y1 = MnY(480);
    int r[4][4], n = 0;
    if (x0 > 0)          { int q[4] = { 0, 0, x0, g_mnRealH };  memcpy(r[n++], q, sizeof q); }
    if (x1 < g_mnRealW)  { int q[4] = { x1, 0, g_mnRealW, g_mnRealH }; memcpy(r[n++], q, sizeof q); }
    if (y0 > 0)          { int q[4] = { x0, 0, x1, y0 };        memcpy(r[n++], q, sizeof q); }
    if (y1 < g_mnRealH)  { int q[4] = { x0, y1, x1, g_mnRealH }; memcpy(r[n++], q, sizeof q); }
    if (!n) return;
    for (int i = 0; i < n; i++) { g_mnClip(r[i][0], r[i][1], r[i][2], r[i][3]); g_mnClear(); }
    MnClipNow(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
}
static void MnScaleFrame(const BYTE* from = nullptr, int fromPitch = 0);
static void* g_mnLockReal = nullptr;
static BYTE* g_mnSrc = nullptr;                   // the 640x480 picture MnScaleFrame scaled last
static int   g_mnSrcBp = 0;                       // its bytes per pixel (0: none yet)
static void WINAPI MnFlip() {
    if (g_mnXf) MnClearBars();
    bool soft = g_mnSoftFrame && g_mnLock && g_mnUnlock && !g_mnLockReal;
    if (soft) {
        g_mnSrcBp = 0;
        void* p = g_mnLock();
        if (p && !IsBadReadPtr(p, 0x14)) { g_mnLockReal = p; MnScaleFrame(); g_mnLockReal = nullptr; }
        if (p) g_mnUnlock(p);
    }
    g_mnFlip();
    // The frame the game draws on next gets the unscaled picture back at its
    // top left, so a menu that only redraws what changed finds what it left.
    if (soft && g_mnSrcBp) {
        void* p = g_mnLock();
        if (p && !IsBadReadPtr(p, 0x14)) {
            BYTE* bits = *(BYTE**)p; int pitch = *(int*)((BYTE*)p + 4);
            if (bits && pitch >= 640 * g_mnSrcBp && g_mnRealH >= 480)
                for (int y = 0; y < 480; y++)
                    memcpy(bits + (size_t)y * pitch, g_mnSrc + (size_t)y * 640 * g_mnSrcBp, (size_t)640 * g_mnSrcBp);
        }
        if (p) g_mnUnlock(p);
    }
}

// ---- movies: the locked frame ------------------------------------------------
// lockwindow returns { bits, pitch, format (+8), width (+0xC), height (+0x10),
// ... }. The movie player takes all of it once when a movie starts (0x5179D0,
// its lock at 0x517A36) and works out its own values from the row length
// there (0x509A30 -> player+0x2C); each frame's lock (0x51789B, and 0x524823)
// then only updates the frame's address and row length (player+0x20, +0x28).
// While the menus are in a mode of their own the player gets its own 640x480
// buffer instead of the screen, its rows as long as the ones it took when the
// movie started, and at unlock that picture is scaled to the menu area
// (wsMovies; 0: copied to the top left as it is). Not the screen
// itself: the intro starts while the screen is still 640x480 (rows of 1280
// bytes), before the menus' mode (854x480: 1712), and its frames, written
// with rows of the new length but the old values, came out as smeared bands.
static BYTE  g_mnLockCopy[0x40];
static bool  g_mnLockMovie = false;
static BYTE* g_mvBuf = nullptr;                   // the movie's own 640x480 frame
static int   g_mvPitch = 0;                       // its rows' length: the one the player took at its start
static int   g_mvBp = 2;
static void* WINAPI MnLock() {
    DWORD ret = (DWORD)__builtin_return_address(0);
    void* p = g_mnLock();
    g_mnLockReal = p; g_mnLockMovie = false;
    if (!p || IsBadReadPtr(p, sizeof(g_mnLockCopy))) return p;
    // the movie player's three lock calls (0x51789B, 0x517A36 at a movie's start, 0x524823)
    bool movie = ret == 0x5178A1 || ret == 0x517A3C || ret == 0x524829;
    bool mapped = g_mnXf || g_mnSoftFrame;        // the menus in a mode other than the 640x480 the game asked for
    int bp = g_mnRealBpp >= 24 ? 4 : 2;
    if (ret == 0x517A3C) g_mvPitch = mapped ? 640 * bp : *(int*)((BYTE*)p + 4);   // what it takes now
    if (!mapped) return p;
    if (!movie) {
        static int logged = 0;
        if (logged < 4) { logged++; LogfAsync("widescreen: frame locked from 0x%06lX in the menus - left as it is\n", (unsigned long)ret); }
        return p;
    }
    int pitch = g_mvPitch >= 640 * bp && g_mvPitch <= 4096 * 4 ? g_mvPitch : 640 * bp;
    static size_t have = 0;
    if (!g_mvBuf || have < (size_t)pitch * 480) {
        free(g_mvBuf); have = (size_t)pitch * 480;
        g_mvBuf = (BYTE*)calloc(have, 1);
        if (!g_mvBuf) { have = 0; return p; }
    }
    g_mvPitch = pitch; g_mvBp = bp;
    memcpy(g_mnLockCopy, p, sizeof(g_mnLockCopy));
    *(BYTE**)g_mnLockCopy = g_mvBuf; *(int*)(g_mnLockCopy + 4) = pitch;
    *(int*)(g_mnLockCopy + 0x0C) = 640; *(int*)(g_mnLockCopy + 0x10) = 480;
    g_mnLockMovie = true;
    return g_mnLockCopy;
}
// scales a 640x480 picture to the menu area of the locked frame: the one at
// its own top left, or from (the movie's buffer)
static void MnScaleFrame(const BYTE* from, int fromPitch) {
    BYTE* bits = *(BYTE**)g_mnLockReal; int pitch = *(int*)((BYTE*)g_mnLockReal + 4);
    int bp = g_mnRealBpp >= 24 ? 4 : 2;
    if (!bits || pitch < g_mnRealW * bp || g_mnRealW < 640 || g_mnRealH < 480) return;
    if (!from) { from = bits; fromPitch = pitch; }
    BYTE*& src = g_mnSrc;
    if (!src) src = (BYTE*)malloc((size_t)640 * 480 * 4);
    if (!src) return;
    g_mnSrcBp = bp;
    for (int y = 0; y < 480; y++) memcpy(src + (size_t)y * 640 * bp, from + (size_t)y * fromPitch, (size_t)640 * bp);
    for (int y = 0; y < g_mnRealH; y++) memset(bits + (size_t)y * pitch, 0, (size_t)g_mnRealW * bp);
    int X0 = MnX(0), Y0 = MnY(0), X1 = MnX(640), Y1 = MnY(480);
    if (X0 < 0) X0 = 0;
    if (Y0 < 0) Y0 = 0;
    if (X1 > g_mnRealW) X1 = g_mnRealW;
    if (Y1 > g_mnRealH) Y1 = g_mnRealH;
    static int sxs[8192];
    if (X1 > 8192) X1 = 8192;
    for (int x = X0; x < X1; x++) { int sx = (int)((x - g_mnOx) / g_mnSx); sxs[x] = sx < 0 ? 0 : sx > 639 ? 639 : sx; }
    for (int y = Y0; y < Y1; y++) {
        int sy = (int)((y - g_mnOy) / g_mnSy); sy = sy < 0 ? 0 : sy > 479 ? 479 : sy;
        BYTE* drow = bits + (size_t)y * pitch; const BYTE* srow = src + (size_t)sy * 640 * bp;
        if (bp == 4) { DWORD* d = (DWORD*)drow; const DWORD* s = (const DWORD*)srow; for (int x = X0; x < X1; x++) d[x] = s[sxs[x]]; }
        else         { WORD* d = (WORD*)drow;   const WORD* s = (const WORD*)srow;   for (int x = X0; x < X1; x++) d[x] = s[sxs[x]]; }
    }
}
static void WINAPI MnUnlock(void* info) {
    if (info == g_mnLockCopy && g_mnLockReal) {
        if (g_mnLockMovie && g_mvBuf) {
            if (g_mnXf && g_cfg.wsMovies) MnScaleFrame(g_mvBuf, g_mvPitch);
            else {                                // at the top left (wsMovies=0; wsMenusSoft: scaled at the flip)
                BYTE* bits = *(BYTE**)g_mnLockReal; int pitch = *(int*)((BYTE*)g_mnLockReal + 4);
                if (bits && pitch >= 640 * g_mvBp)
                    for (int y = 0; y < 480; y++) memcpy(bits + (size_t)y * pitch, g_mvBuf + (size_t)y * g_mvPitch, (size_t)640 * g_mvBp);
            }
        }
        info = g_mnLockReal;
    }
    g_mnLockReal = nullptr; g_mnLockMovie = false;
    g_mnUnlock(info);
}

static int WINAPI MnSetMode(int idx, int depth, int flag) {
    int use = idx;
    if (g_mnFeCall && g_mnFeTarget > 0 && g_mnFeTarget != idx) {
        int* m = ModeEntryPtr(g_mnFeTarget);
        if (m) {                                    // the game's own depth / flag rule for that mode (0x4BEFDB)
            use = g_mnFeTarget;
            depth = m[5] > 2 ? 2 : m[5];
            flag = depth <= m[6] ? 1 : 0;
        }
    }
    int r = g_mnSetMode(use, depth, flag);
    if (r) {
        g_mnRealIdx = use;
        if (int* m = ModeEntryPtr(use)) { g_mnRealW = m[0]; g_mnRealH = m[1]; g_mnRealBpp = m[2]; }
    }
    if (use != idx) LogfAsync("widescreen: menus in display mode %d (%dx%d, %d-bit) instead of %d%s\n",
                              use, g_mnRealW, g_mnRealH, g_mnRealBpp, idx, r ? "" : " - FAILED");
    return r;
}

typedef FARPROC (WINAPI *GPA_t)(HMODULE, LPCSTR);
static GPA_t g_oGPA = nullptr;
static FARPROC WINAPI HkGetProcAddress(HMODULE m, LPCSTR name) {
    FARPROC p = g_oGPA(m, name);
    if (!p || !name || !HIWORD((DWORD)name) || strncmp(name, "_THRASH_", 8)) return p;
    if (m != g_mnMod) {
        g_mnMod = m;
        char fn[MAX_PATH] = {}; GetModuleFileNameA(m, fn, MAX_PATH);
        const char* b = strrchr(fn, '\\'); b = b ? b + 1 : fn;
        g_mnSoft = !_strnicmp(b, "soft", 4) || !_strnicmp(b, "null", 4);
        g_mnD3D = !_strnicmp(b, "d3d", 3); g_mnTex = nullptr;
        g_apc = g_cfg.alternatePixelCenter && g_mnD3D;
        if (g_cfg.alternatePixelCenter) LogfAsync("pixel centre: %s - %s\n", b, g_apc ? "every vertex moved by (-0.5, -0.5), the cockpit picture's left as they are" : "left as it is (Direct3D only)");
        if (g_mnSoft) LogfAsync("widescreen: %s - its menus %s\n", b, g_cfg.wsMenusSoft ? "scaled up frame by frame (wsMenusSoft)" : "stay 640x480");
    }
    struct W { const char* n; void** real; void* wr; };
    static const W kW[] = {
        { "_THRASH_drawpoint@4", (void**)&g_mnPoint, (void*)&MnPoint },
        { "_THRASH_drawline@8", (void**)&g_mnLine, (void*)&MnLine },
        { "_THRASH_drawtri@12", (void**)&g_mnTri, (void*)&MnTri },
        { "_THRASH_drawquad@16", (void**)&g_mnQuad, (void*)&MnQuad },
        { "_THRASH_drawtrifan@8", (void**)&g_mnFan, (void*)&MnFan },
        { "_THRASH_drawtristrip@8", (void**)&g_mnStrip, (void*)&MnStrip },
        { "_THRASH_drawlinestrip@8", (void**)&g_mnLStrip, (void*)&MnLStrip },
        { "_THRASH_drawpointstrip@8", (void**)&g_mnPStrip, (void*)&MnPStrip },
        { "_THRASH_drawtrimesh@12", (void**)&g_mnTriM, (void*)&MnTriM },
        { "_THRASH_drawquadmesh@12", (void**)&g_mnQuadM, (void*)&MnQuadM },
        { "_THRASH_drawlinemesh@12", (void**)&g_mnLineM, (void*)&MnLineM },
        { "_THRASH_drawpointmesh@12", (void**)&g_mnPointM, (void*)&MnPointM },
        { "_THRASH_clip@16", (void**)&g_mnClip, (void*)&MnClipW },
        { "_THRASH_setvideomode@12", (void**)&g_mnSetMode, (void*)&MnSetMode },
        { "_THRASH_pageflip@0", (void**)&g_mnFlip, (void*)&MnFlip },
        { "_THRASH_lockwindow@0", (void**)&g_mnLock, (void*)&MnLock },
        { "_THRASH_unlockwindow@4", (void**)&g_mnUnlock, (void*)&MnUnlock },
        { "_THRASH_setstate@8", (void**)&g_mnSetState, (void*)&MnSetState },
        { "_THRASH_settexture@4", (void**)&g_mnSetTex, (void*)&MnSetTex },
        { "_THRASH_tupdate@12", (void**)&g_mnTUpdate, (void*)&MnTUpdate },
    };
    if (!strcmp(name, "_THRASH_clearwindow@0")) g_mnClear = (TVoidFn_t)(void*)p;
    if (!strcmp(name, "_THRASH_about@0"))       g_mnAbout = (TAbout_t)(void*)p;
    for (const W& w : kW) if (!strcmp(name, w.n)) { *w.real = (void*)p; return (FARPROC)w.wr; }
    return p;
}

// ---- the mode calls ------------------------------------------------------------
static void SaveIniInt(const char* key, int value) {     // replaces "key=..." in HPFrameFix.ini (or adds it)
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%sHPFrameFix.ini", g_asiDir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    static char buf[65536];
    size_t n = 0, kl = strlen(key); char line[512]; bool done = false, full = false;
    while (fgets(line, sizeof(line), f)) {
        char out[600];
        const char* q = line; while (*q == ' ' || *q == '\t') q++;
        if (!done && !_strnicmp(q, key, kl) && (q[kl] == '=' || q[kl] == ' ' || q[kl] == '\t')) {
            snprintf(out, sizeof(out), "%s=%d\n", key, value); done = true;
        } else snprintf(out, sizeof(out), "%s", line);
        size_t l = strlen(out);
        if (n + l < sizeof(buf)) { memcpy(buf + n, out, l); n += l; } else full = true;
    }
    fclose(f);
    if (!done) {
        char out[96]; snprintf(out, sizeof(out), "%s=%d\n", key, value);
        size_t l = strlen(out);
        if (n + l < sizeof(buf)) { memcpy(buf + n, out, l); n += l; } else full = true;
    }
    if (full) { LogfAsync("ini: too big to update %s\n", key); return; }
    if ((f = fopen(path, "w"))) { fwrite(buf, 1, n, f); fclose(f); }
}
extern "C" { DWORD g_fnSetVideoMode = 0x4BEF50, g_fnGetSelMode = 0x4482F0, g_fnModeLookup = 0x4BEE80; int g_mnFeSite = 0; }
extern "C" int __cdecl GameCall0(DWORD fn);       // a game function, no arguments; all registers but eax kept
static bool MenusOn() {
    return g_knownExe && g_cfg.widescreen && g_cfg.wsMenus && (!g_mnSoft || g_cfg.wsMenusSoft) && g_mnAbout && g_mnSetMode;
}
// eax = the menu mode the game asks for; g_mnFeSite: 1 = startup
static void PzLimitsOff();
extern "C" void __cdecl MnBeforeFeMode(int feIdx) {
    PzLimitsOff();
    g_mnFeCall = 1; g_mnFeTarget = 0;
    if (!MenusOn()) return;
    int* fe = ModeEntryPtr(feIdx);
    int bpp = fe ? fe[2] : 16, w = 0, h = 0;
    // wsMenuResolution=1: the resolution chosen in the game's settings, whatever
    // it is - 640x480 too, which is then simply the game's own menu mode. At
    // startup the settings aren't loaded yet, so it's the size kept in the ini
    // from last time (none yet: the game's 640x480). The desktop's size is only
    // used when it's asked for (2).
    if (g_cfg.wsMenuResolution == 2) { w = g_dispW; h = g_dispH; }
    else {
        if (g_mnFeSite != 1) {
            int sel = GameCall0(g_fnGetSelMode);
            if (int* m = ModeEntryPtr(sel)) { w = m[0]; h = m[1]; }
        }
        if (w <= 0 || h <= 0) { w = g_cfg.wsRaceWidth; h = g_cfg.wsRaceHeight; }
    }
    int t = (w > 640 || h > 480) ? FindModeWH(w, h, bpp) : 0;
    g_mnFeTarget = t;
    // the game doesn't call the renderer when it asks for the mode it asked for last
    if (t > 0 && t != g_mnRealIdx && Rd<int>(0x55FEC0) == feIdx) *(volatile int*)0x55FEC0 = -1;
    static int lastLogged = -1;
    if (t != lastLogged) { lastLogged = t; LogfAsync("widescreen: menus wanted at %dx%d -> display mode %d\n", w, h, t); }
}
extern "C" void __cdecl MnAfterFeMode() {
    g_mnFeCall = 0;
    int W = g_mnRealW, H = g_mnRealH;
    bool on = MenusOn() && W > 0 && H > 0 && (W != 640 || H != 480) && g_mnFeTarget > 0 && g_mnRealIdx == g_mnFeTarget;
    if (on) {
        if (g_cfg.wsMenus == 2) { g_mnSx = W / 640.0f; g_mnSy = H / 480.0f; g_mnOx = g_mnOy = 0; }
        else {
            float s = W / 640.0f < H / 480.0f ? W / 640.0f : H / 480.0f;
            g_mnSx = g_mnSy = s; g_mnOx = (W - 640.0f * s) * 0.5f; g_mnOy = (H - 480.0f * s) * 0.5f;
        }
        if (g_mnSoft) { g_mnSoftFrame = true; on = false; }   // (drawn as it is, scaled at the flip)
        else if (g_mnClip) MnClipNow(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
    } else g_mnSoftFrame = false;
    g_mnXf = on;
    g_mnFeInset = on && g_mnD3D && (g_cfg.menuTexelInset || g_cfg.wsMenuFilter) && (g_mnSx != 1.0f || g_mnSy != 1.0f);
}
extern "C" void __cdecl MnBeforeRaceMode(int raceIdx) {
    g_mnFeCall = 0;
    PzLimitsOff();
    g_mnSoftFrame = false; g_mnFeInset = false;
    if (g_mnXf) { g_mnXf = false; if (g_mnClip) g_mnClip(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]); }
    // a race in the menus' own mode number (640x480) while the screen is in
    // another: the game would skip the call, so it's made to happen
    if (g_mnRealIdx && raceIdx != g_mnRealIdx && Rd<int>(0x55FEC0) == raceIdx) *(volatile int*)0x55FEC0 = -1;
}
extern "C" void __cdecl MnAfterRaceMode() {
    int W = Rd<int>(0x7CDAE0), H = Rd<int>(0x7CDAE4);
    if (W > 0 && H > 0 && (W != g_cfg.wsRaceWidth || H != g_cfg.wsRaceHeight)) {
        g_cfg.wsRaceWidth = W; g_cfg.wsRaceHeight = H;
        SaveIniInt("wsRaceWidth", W); SaveIniInt("wsRaceHeight", H);
        LogfAsync("widescreen: race resolution %dx%d kept for next time\n", W, H);
    }
}
// width -> mode (0x4BEE80 at the three calls): the remembered size if it's that
// width, else the size the game's own lookup found - as the list shows it
extern "C" int __cdecl MnModeLookup(int w, int found) {
    if (!g_cfg.wsUnlockResolutions) return found;     // (the game's own list: its own lookup fits it)
    int h = 0;
    if (w > 0 && w == g_cfg.wsRaceWidth && g_cfg.wsRaceHeight > 0) h = g_cfg.wsRaceHeight;
    else if (int* m = ModeEntryPtr(found)) { w = m[0]; h = m[1]; }
    int i = h ? FindModeWH(w, h, 0) : 0;
    if (i && i != found) LogfAsync("widescreen: saved resolution %dx%d -> display mode %d (the game's lookup: %d)\n", w, h, i, found);
    return i ? i : found;
}
// ---- the in-race menus (pause and what it leads to) -----------------------------
// RenderFrame (0x4BC680) draws them over the race picture when it's asked to
// (0x4BC7AB: call 0x44BDD0). They're laid out for 640x480 and the game scales
// that to the whole screen, so on a wide screen they're stretched. 0x44BDD0:
//  - 0x4A75E0 draws the frame: the tinted panels, the border, the title bar;
//  - 0x44A4F0 draws the items, layer by layer, each through its own draw
//    function (0x44A5DC: call [ebx+0x2C], ebx = the item);
//  - text isn't drawn on the spot: every letter is a quad added to one of two
//    lists, one per font (0x7F74C0 and 0x81B4D0: +4 = the next free vertex,
//    counting down from 0xFFF; vertices of 32 bytes from +0x10), which 0x4D1340
//    draws and empties (13 callers; here at 0x44BDFE);
//  - 0x434B60 draws the mouse pointer (0x44BE03).
// wsPauseMenu=1: the frame is left as it is, over the whole screen. Everything
// else keeps its 4:3 shape: x' = x * 4H/3W + an offset, through the draw
// wrappers above (sprites, as they're drawn) and in the lists (letters, before
// they're drawn). The offset puts an item in the middle block - what a 4:3
// screen shows, centred - except in the bottom row (y > 85% of the height),
// where an item that starts in the left or right 30% keeps to that side
// (Continue, Quit). One offset per item, decided by the first thing it draws,
// so an item and its text move as one. The pointer is placed by the same rule,
// so it's over what it points at. wsPauseMenu=2: all of it, frame too, in the
// middle block.
// The mouse: the game keeps its place in [0x5F38F0] (x) and [0x5F38F4] (y),
// held between the limits [0x5F38E0]..[0x5F38E8] (x) as it's read (0x435310).
// The pointer is drawn by the items' rule, so it's over what it points at -
// which squeezes the whole width the mouse can have into the middle block.
// So while an in-race menu is up (wsPauseMenu=1 or 2) the x limits are widened by
// what the block leaves at each side, counted in the game's own measure: the
// pointer then reaches the screen's edges. PzLimitsOff puts them back.
// Text: these menus take their letter sizes from the HUD's three (0x451530
// picks [0x791B4C], [0x791B50] or [0x791B54], which the HUD setup works out
// from the screen width at 0x484B7B), measure their strings with them and
// place and size things by what they measure - a button is its text plus a
// margin. The game's layout here is "everything times W/640", which the rule
// above undoes; but with the HUD in 4:3 proportions those sizes already are
// what a 4:3 screen has, and the letters (and the buttons around them) came
// out narrowed twice. So with both on:
//  - what the menus are told a string measures is its width over the factor
//    (0x452020 turns pixels into 640ths with the screen width at 0x452055,
//    which becomes the HUD's width; 0x451D37, the other way to measure, gets
//    the factor on its result): narrowed with the rest, that is the string's
//    real width again, so buttons and alignment come out as on a 4:3 screen;
//  - a string's letters keep their size and spacing, and only the place the
//    string starts is moved by the rule. For that each letter's quad is noted
//    as it's asked for (0x4D06F0: where on the line, which line, how big),
//    and letters that follow one another on a line are one string.
static bool g_pzOn = false, g_pzZones = false, g_pzXfSaved = false;
static float g_pzSave[4], g_pzK = 1, g_pzW = 0, g_pzH = 0;
static bool g_pzLimOn = false;
static int g_pzLimSave[2], g_pzLimSet[2];
static unsigned long g_pzLimFrame = 0;
static void PzLimitsOff() {
    if (!g_pzLimOn) return;
    g_pzLimOn = false;
    volatile int* mn = (volatile int*)0x5F38E0; volatile int* mx = (volatile int*)0x5F38E8; volatile int* x = (volatile int*)0x5F38F0;
    if (*mn != g_pzLimSet[0] || *mx != g_pzLimSet[1]) return;     // (the game has set its own since)
    *mn = g_pzLimSave[0]; *mx = g_pzLimSave[1];
    if (*x < *mn) *x = *mn;
    if (*x > *mx) *x = *mx;
}
extern "C" void __cdecl PzLimitsCheck() {         // every frame (PreRender): the menu has gone
    if (g_pzLimOn && (unsigned long)g_renders - g_pzLimFrame > 1) PzLimitsOff();
}
// The pointer's place is kept here, as g_pzU: the game's x it would have if the
// whole screen were the middle block's rule (screen x = PzMid() + U k), from
// -ext to W + ext. Each frame the movement the game added to its stored x since
// we last wrote it goes into U, and the stored x is set to what the game must
// test its items with (PzHit): U itself above the bottom row; in the bottom
// row, whose side parts are drawn at the screen's edges, the x of whatever is
// drawn under the pointer. So the pointer never jumps, and what's under it is
// what reacts. The arrow keys don't move the stored x a little: they put it
// on the next item's x (a jump of at least a tenth of the screen in one
// frame, which the mouse never makes). Added to U that left the pointer
// short of the bottom row's side parts, over nothing; so a jump puts U where
// that item is drawn instead (PzUFromGame, the other way round from PzHit).
static float g_pzU = 0; static int g_pzHitSet = 0;
static inline float PzMid() { return g_pzW * 0.5f * (1.0f - g_pzK); }   // where the middle block starts
static inline bool PzBottomRow() { return Rd<int>(0x5F38F4) > (int)(g_pzH * 0.85f); }
static float PzHit(float U) {
    if (!g_pzZones || !PzBottomRow()) return U;   // (wsPauseMenu=2: all of it is the middle block)
    float W = g_pzW, k = g_pzK, mid = PzMid(), P = mid + U * k;
    float l = W * 0.3f, r = W * 0.7f;             // (PzDecide's bounds)
    if (P < l * k + mid * 0.5f) return P / k;                 // left part, and its half of the gap
    if (P > W - (W - r) * k - mid * 0.5f) return W - (W - P) / k;   // right part likewise
    return U < l ? l : U > r ? r : U;                         // middle part
}
static float PzUFromGame(float G) {
    if (!g_pzZones || !PzBottomRow()) return G;
    float W = g_pzW, k = g_pzK, mid = PzMid(), l = W * 0.3f, r = W * 0.7f;
    float P = G < l ? G * k : G > r ? W - (W - G) * k : mid + G * k;   // where the game's x G is drawn
    return (P - mid) / k;
}
static void PzLimitsFrame() {                     // every frame the menu is drawn
    volatile int* mn = (volatile int*)0x5F38E0; volatile int* mx = (volatile int*)0x5F38E8; volatile int* x = (volatile int*)0x5F38F0;
    if (g_pzLimOn && (*mn != g_pzLimSet[0] || *mx != g_pzLimSet[1])) g_pzLimOn = false;   // (the game's own now)
    if (!g_pzLimOn) { g_pzLimSave[0] = *mn; g_pzLimSave[1] = *mx; g_pzLimOn = true; g_pzU = (float)*x; g_pzHitSet = *x; }
    int ext = (int)lrintf(g_pzW * 0.5f * (1.0f - g_pzK) / g_pzK);
    g_pzLimSet[0] = g_pzLimSave[0] - ext; g_pzLimSet[1] = g_pzLimSave[1] + ext;
    *mn = g_pzLimSet[0]; *mx = g_pzLimSet[1];
    int d = *x - g_pzHitSet;
    if ((float)abs(d) >= g_pzW * 0.1f) g_pzU = PzUFromGame((float)*x);   // (an arrow key: onto the item)
    else g_pzU += (float)d;
    if (g_pzU < (float)g_pzLimSet[0]) g_pzU = (float)g_pzLimSet[0];
    if (g_pzU > (float)g_pzLimSet[1]) g_pzU = (float)g_pzLimSet[1];
    g_pzHitSet = (int)lrintf(PzHit(g_pzU));
    *x = g_pzHitSet;
    g_pzLimFrame = (unsigned long)g_renders;
}
static const DWORD kPzLists[2] = { 0x7F74C0, 0x81B4D0 };
static int g_pzMark[2];
extern "C" { int g_pzOnC = 0; DWORD g_fnGlyphBody = 0x4D06F6, g_fnStrWidth = 0x4D1480; float g_pzMeasF = 1.0f; }
extern "C" void PzGlyphStub();
extern "C" void PzMeasureStub();
static bool g_pzRigid = false;                    // letters keep their size (the HUD's 4:3 sizes, on a wider screen)
// The screen where the HUD's positions are changed is one of these menus, and
// each thing to drag is an item (drawn by 0x45BCB0) that shows a piece of the
// HUD where its box is on the screen. Those are left where they are (g_pzPlain,
// for the one item), and while that screen is up (g_pzCustom: such an item was
// in the menu's last frame) the pointer is too, with the mouse's own limits:
// it is over a box when the game finds it over that box. The buttons keep to
// the middle block's rule; the game takes them for a little wider than drawn.
static bool g_pzPlain = false, g_pzCustom = false, g_pzCustomSeen = false;
// which list and its next free vertex when the letter began; pen x and letter size (floats, kept as their bits:
// nothing here may touch the FPU, the game has values on its stack), the line's y
struct PzGlyph { int list, idx; DWORD xBits, sizeBits; int y; };
static inline float PzF(DWORD bits) { float f; memcpy(&f, &bits, 4); return f; }
static PzGlyph g_pzGlyph[1024]; static int g_pzGlyphN = 0;
// a[0] = pen x (float), a[1] = the line's y, a[5] = "only measuring"
extern "C" void __cdecl PzGlyphNote(const DWORD* a) {
    int list = Rd<int>(0x840184);
    if (a[5] || g_pzGlyphN >= 1024 || list < 0 || list > 1) return;
    PzGlyph& r = g_pzGlyph[g_pzGlyphN++];
    r.list = list; r.idx = Rd<int>(kPzLists[list] + 4); r.xBits = a[0]; r.sizeBits = Rd<DWORD>(0x84018C); r.y = (int)a[1];
}
static void PzDecide(float x, float y) {
    g_pzPending = false;
    if (y > g_pzH * 0.85f) {
        if (x < g_pzW * 0.3f) g_mnOx = 0.0f;
        else if (x > g_pzW * 0.7f) g_mnOx = g_pzW * (1.0f - g_pzK);
    }
}
// the letters added since the last time: moved like the item they belong to
static void PzLetters() {
    if (g_pzPlain) {                              // (left where they are)
        for (int i = 0; i < 2; i++) g_pzMark[i] = Rd<int>(kPzLists[i] + 4);
        g_pzGlyphN = 0;
        return;
    }
    for (int i = 0; i < 2; i++) {
        int cur = Rd<int>(kPzLists[i] + 4), mark = g_pzMark[i];
        g_pzMark[i] = cur;
        if (cur < -1 || mark > 0xFFF || cur >= mark) continue;       // (nothing new, or the list was emptied)
        if (g_pzPending) { const float* v = (const float*)(kPzLists[i] + 0x10 + mark * 32); PzDecide(v[0], v[1]); }
        static unsigned char rigid[0x1000];
        bool anyRigid = false;
        if (g_pzRigid && g_pzGlyphN) {
            memset(rigid, 0, sizeof rigid);
            bool have = false; float x0 = 0, px = 0, py = 0, ps = 0;
            for (int g = 0; g < g_pzGlyphN; g++) {
                const PzGlyph& r = g_pzGlyph[g];
                if (r.list != i) continue;
                float x = PzF(r.xBits), y = (float)r.y, size = PzF(r.sizeBits);
                float sz = have && ps > size ? ps : size;
                if (!have || x < px - 0.5f || x - px > 40.0f * sz || fabsf(y - py) > 12.0f * sz) x0 = x;   // a new string
                have = true; px = x; py = y; ps = size;
                int after = cur;                                    // (a letter that made no quad - a space - has none to move)
                for (int h = g + 1; h < g_pzGlyphN; h++) if (g_pzGlyph[h].list == i) { after = g_pzGlyph[h].idx; break; }
                if (after != r.idx - 4) continue;
                for (int k = r.idx; k > r.idx - 4; k--) {
                    if (k <= cur || k > mark || k < 0 || k > 0xFFF) continue;
                    float* v = (float*)(kPzLists[i] + 0x10 + k * 32);
                    v[0] += g_mnOx - x0 * (1.0f - g_pzK);
                    rigid[k] = 1; anyRigid = true;
                }
            }
        }
        for (int k = cur + 1; k <= mark; k++) {
            if (anyRigid && k >= 0 && k <= 0xFFF && rigid[k]) continue;
            float* v = (float*)(kPzLists[i] + 0x10 + k * 32);
            v[0] = v[0] * g_pzK + g_mnOx;
        }
    }
    g_pzGlyphN = 0;
}
extern "C" void __cdecl PauseXfBegin() {
    g_pzOn = false; g_mnInset = false;
    if (g_mnXf || !g_mnClip) return;
    int W = Rd<int>(0x7CDAE0), H = Rd<int>(0x7CDAE4);
    if (W <= 0 || H <= 0) return;
    if (g_cfg.menuTexelInset && g_mnD3D && (W > 640 || H > 480)) {
        g_mnInset = true; g_mnInsetX = W / 640.0f; g_mnInsetY = H / 480.0f;
        static bool logged = false;
        if (!logged) { logged = true; LogfAsync("in-race menus: pictures sampled half a texel in from their edges (%dx%d)\n", W, H); }
    }
    // (the software renderer too: only the pictures' places and sizes change here, which it follows)
    if (!g_cfg.widescreen || !g_cfg.wsPauseMenu) return;
    double f = W * 3.0 / (H * 4.0);
    if (f <= 1.0001) return;
    g_pzSave[0] = g_mnSx; g_pzSave[1] = g_mnSy; g_pzSave[2] = g_mnOx; g_pzSave[3] = g_mnOy;
    g_pzK = (float)(1.0 / f); g_pzW = (float)W; g_pzH = (float)H;
    g_mnSx = g_pzK; g_mnSy = 1.0f; g_mnOx = PzMid(); g_mnOy = 0.0f;
    g_pzZones = g_cfg.wsPauseMenu == 1;
    g_pzPending = false; g_pzOn = true;
    g_pzGlyphN = 0; g_pzOnC = 1;
    g_pzRigid = g_hudIn && g_hudW > 0 && g_hudW < W;
    g_pzPlain = false; g_pzCustomSeen = false;
    if (g_pzCustom) PzLimitsOff();
    else PzLimitsFrame();
    for (int i = 0; i < 2; i++) g_pzMark[i] = Rd<int>(kPzLists[i] + 4);
    g_mnXf = !g_pzZones;                          // (the frame comes first: left alone with zones)
    if (g_mnXf) MnClipNow(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
}
// The pointer's draw (0x434B60) reads the mouse (0x435310 gives it [0x5F38F0],
// [0x5F38F4]), builds its quad there and drops it when a corner is off the
// screen - which, with the mouse's range widened, it is whenever the pointer
// should be outside the middle block. So for that one call the stored x is the
// place on the screen the items' rule gives the mouse (always on the screen),
// the wrappers only shrink the picture about that place, and afterwards the
// stored x is the game's own again, plus whatever the mouse moved meanwhile.
extern "C" void __cdecl PzItemBegin(const BYTE* item);
extern "C" void __cdecl PzItemEnd();
static bool g_pzPtrSub = false;
static int g_pzPtrL = 0, g_pzPtrP = 0;
extern "C" void __cdecl PzPtrBegin() {
    PzItemBegin(nullptr);
    if (!g_pzOn) return;
    // (wsPauseMenu=2: the menu is clipped to the middle block; the pointer may leave it)
    if (!g_pzZones && g_mnClip) g_mnClip(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
    if (g_pzCustom) { g_pzPlain = true; g_pzPending = false; g_mnXf = false; return; }
    volatile int* gx = (volatile int*)0x5F38F0;
    g_pzPtrL = *gx;
    float W = g_pzW, k = g_pzK, P = PzMid() + (g_pzU + (float)(g_pzPtrL - g_pzHitSet)) * k;
    P = P < 0 ? 0 : P > W ? W : P;
    g_pzPtrP = (int)lrintf(P);
    *gx = g_pzPtrP;
    g_pzPending = false;
    g_mnOx = (float)g_pzPtrP * (1.0f - k);
    g_pzPtrSub = true;
}
extern "C" void __cdecl PzPtrEnd() {
    if (g_pzPtrSub) {
        volatile int* gx = (volatile int*)0x5F38F0;
        *gx = g_pzPtrL + (*gx - g_pzPtrP);
        g_pzPtrSub = false;
    }
    PzItemEnd();
    if (g_pzOn && !g_pzZones && g_mnClip) MnClipNow(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
}
extern "C" void __cdecl PauseXfEnd() {
    g_mnInset = false;
    if (!g_pzOn) return;
    g_pzOn = false; g_pzPending = false; g_mnXf = false;
    g_pzOnC = 0;
    g_pzPlain = false; g_pzCustom = g_pzCustomSeen;
    g_mnSx = g_pzSave[0]; g_mnSy = g_pzSave[1]; g_mnOx = g_pzSave[2]; g_mnOy = g_pzSave[3];
    g_mnClip(g_mnClipRect[0], g_mnClipRect[1], g_mnClipRect[2], g_mnClipRect[3]);
}
// an item's draw (0x44A5DC), and the pointer's (0x44BE03)
extern "C" void __cdecl PzItemBegin(const BYTE* item) {
    if (!g_pzOn) return;
    g_pzPending = false; g_mnOx = PzMid();
    PzLetters();                                  // (letters from outside any item: the middle block)
    if (item && *(const DWORD*)(item + 0x2C) == 0x45BCB0) {   // a HUD box to drag (the item is the one about to be drawn)
        g_pzPlain = true; g_pzCustomSeen = true; g_mnXf = false;
        return;
    }
    g_pzPending = g_pzZones;
    g_mnXf = true;
}
extern "C" void __cdecl PzItemEnd() {
    if (!g_pzOn) return;
    PzLetters();
    if (g_pzPlain) { g_pzPlain = false; g_mnXf = true; }
    g_pzPending = false; g_mnOx = PzMid(); g_mnOy = 0.0f;
}
// the text flush (0x4D1340): the letters are placed first, and then drawn as they are
extern "C" void __cdecl PzFlushBegin() {
    if (!g_pzOn) return;
    PzLetters();
    g_pzXfSaved = g_mnXf; g_mnXf = false;
}
extern "C" void __cdecl PzFlushEnd() {
    if (!g_pzOn) return;
    g_mnXf = g_pzXfSaved;
    for (int i = 0; i < 2; i++) g_pzMark[i] = Rd<int>(kPzLists[i] + 4);
}
extern "C" { DWORD g_fnRaceMenuDraw = 0x44BDD0, g_fnPointerDraw = 0x434B60, g_fnTextFlushBody = 0x4D134B; }
extern "C" void PauseMenuStub();
extern "C" void PzItemStub();
extern "C" void PzPointerStub();
extern "C" void PzFlushStub();
extern "C" void MnFeStubStartup();
extern "C" void MnFeStub();
extern "C" void MnRaceStub();
extern "C" void MnLookupStub();
asm(".text\n"
    // a string measured for the in-race menus (0x451D37: call 0x4D1480)
    ".globl _PzMeasureStub\n"
    "_PzMeasureStub:\n"
    "    call *_g_fnStrWidth\n"
    "    fmuls _g_pzMeasF\n"
    "    ret\n"
    // a letter's quad (0x4D06F0, jmp from its entry; [esp+4] on = its arguments):
    // noted while an in-race menu is drawn, then its first six bytes and on
    ".globl _PzGlyphStub\n"
    "_PzGlyphStub:\n"
    "    cmpl $0, _g_pzOnC\n"
    "    je 1f\n"
    "    pushal\n"
    "    leal 0x24(%esp), %eax\n"
    "    pushl %eax\n"
    "    call _PzGlyphNote\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "1:  pushl %ecx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    pushl %ebp\n"
    "    movl %esp, %ebp\n"
    "    jmp *_g_fnGlyphBody\n"
    // RenderFrame's call of the in-race menu's draw (0x4BC7AB)
    ".globl _PauseMenuStub\n"
    "_PauseMenuStub:\n"
    "    pushal\n"
    "    call _PauseXfBegin\n"
    "    popal\n"
    "    call *_g_fnRaceMenuDraw\n"
    "    pushal\n"
    "    call _PauseXfEnd\n"
    "    popal\n"
    "    ret\n"
    // the menu draw loop's call of an item's draw (0x44A5DC: call [ebx+0x2C];
    // test eax, eax)
    ".globl _PzItemStub\n"
    "_PzItemStub:\n"
    "    pushal\n"
    "    pushl %ebx\n"
    "    call _PzItemBegin\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    call *0x2c(%ebx)\n"
    "    pushal\n"
    "    call _PzItemEnd\n"
    "    popal\n"
    "    testl %eax, %eax\n"
    "    ret\n"
    // the pointer's draw (0x44BE03): placed like an item
    ".globl _PzPointerStub\n"
    "_PzPointerStub:\n"
    "    pushal\n"
    "    call _PzPtrBegin\n"
    "    popal\n"
    "    call *_g_fnPointerDraw\n"
    "    pushal\n"
    "    call _PzPtrEnd\n"
    "    popal\n"
    "    ret\n"
    // the text flush (0x4D1340, jmp from its entry): its first 11 bytes are
    // here, the rest runs in place and returns to this stub
    ".globl _PzFlushStub\n"
    "_PzFlushStub:\n"
    "    pushal\n"
    "    call _PzFlushBegin\n"
    "    popal\n"
    "    call 1f\n"
    "    pushal\n"
    "    call _PzFlushEnd\n"
    "    popal\n"
    "    ret\n"
    "1:  pushl %edx\n"
    "    pushl %ebp\n"
    "    movl %esp, %ebp\n"
    "    testb $2, 0x7a3a58\n"
    "    jmp *_g_fnTextFlushBody\n"
    // int GameCall0(fn)
    ".globl _GameCall0\n"
    "_GameCall0:\n"
    "    pushal\n"
    "    movl 36(%esp), %ecx\n"
    "    call *%ecx\n"
    "    movl %eax, 28(%esp)\n"
    "    popal\n"
    "    ret\n"
    // the menu mode calls (eax = mode): startup 0x4B60FD, the others
    ".globl _MnFeStubStartup\n"
    "_MnFeStubStartup:\n"
    "    movl $1, _g_mnFeSite\n"
    "    jmp 1f\n"
    ".globl _MnFeStub\n"
    "_MnFeStub:\n"
    "    movl $0, _g_mnFeSite\n"
    "1:  pushal\n"
    "    pushl %eax\n"
    "    call _MnBeforeFeMode\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    call *_g_fnSetVideoMode\n"
    "    pushal\n"
    "    call _MnAfterFeMode\n"
    "    popal\n"
    "    ret\n"
    // the race's mode call 0x4A3422
    ".globl _MnRaceStub\n"
    "_MnRaceStub:\n"
    "    pushal\n"
    "    pushl %eax\n"
    "    call _MnBeforeRaceMode\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    call *_g_fnSetVideoMode\n"
    "    pushal\n"
    "    call _MnAfterRaceMode\n"
    "    popal\n"
    "    ret\n"
    // width -> mode 0x4BEE80 (eax = width, edx = ?; returns eax)
    ".globl _MnLookupStub\n"
    "_MnLookupStub:\n"
    "    pushl %eax\n"
    "    call *_g_fnModeLookup\n"
    "    pushal\n"
    "    pushl %eax\n"                  // found
    "    pushl 36(%esp)\n"              // width (pushed above pushal)
    "    call _MnModeLookup\n"
    "    addl $8, %esp\n"
    "    movl %eax, 28(%esp)\n"
    "    popal\n"
    "    addl $4, %esp\n"
    "    ret\n"
);
// The draw wrappers: the game gets the renderer's functions by name through
// GetProcAddress, which is where it's handed the wrappers instead. Hooked once,
// for whichever of the menus, the in-race menus and alternatePixelCenter wants it.
static bool InstallDrawWrappers() {
    static int state = -1;                        // -1 = not tried yet, else whether it's in
    if (state < 0) {
        void** slot = nullptr;
        ForEachImport("KERNEL32.dll", [&](void** s, const char* nm) { if (!strcmp(nm, "GetProcAddress")) slot = s; });
        state = slot && PatchPointer(slot, (void*)&HkGetProcAddress, (void**)&g_oGPA) ? 1 : 0;
    }
    return state == 1;
}
static void InstallMenus() {
    if (!g_cfg.wsMenus) Logf("widescreen: menus 640x480 (the game's)\n");
    if (!g_cfg.wsMenus && !g_cfg.wsPauseMenu) return;             // (menuTexelInset in a race needs wsPauseMenu's hook)
    g_dispW = GetSystemMetrics(SM_CXSCREEN); g_dispH = GetSystemMetrics(SM_CYSCREEN);
    bool ok = InstallDrawWrappers();
    if (g_cfg.wsPauseMenu) {
        BYTE j[11] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        DWORD rel = (DWORD)&PzFlushStub - (0x4D1340 + 5); memcpy(j + 1, &rel, 4);
        BYTE g[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
        rel = (DWORD)&PzGlyphStub - (0x4D06F0 + 5); memcpy(g + 1, &rel, 4);
        bool pz = ok && PatchRel32(0x44A5DC, 0xE8, (void*)&PzItemStub) && PatchRel32(0x44BE03, 0xE8, (void*)&PzPointerStub) &&
                  PatchBytes(0x4D1340, j, 11) && PatchBytes(0x4D06F0, g, 6) && PatchRel32(0x4BC7AB, 0xE8, (void*)&PauseMenuStub);
        if (pz && g_hudIn) {                      // (the HUD's letter sizes: strings measured to match)
            DWORD var = (DWORD)&g_hudW;
            g_pzMeasIn = PatchBytes(0x452057, &var, 4) && PatchRel32(0x451D37, 0xE8, (void*)&PzMeasureStub);
            if (!g_pzMeasIn) pz = false;
        }
        Logf("widescreen: in-race menus, %s: %s\n", g_cfg.wsPauseMenu == 2 ? "4:3 in the middle" : "frame over the screen, items in 4:3 proportions",
             pz ? "on" : "patch FAILED");
    }
    if (!g_cfg.wsMenus) return;
    ok = ok && PatchRel32(0x4B60FD, 0xE8, (void*)&MnFeStubStartup) && PatchRel32(0x4A35E8, 0xE8, (void*)&MnFeStub) &&
         PatchRel32(0x44B1E5, 0xE8, (void*)&MnFeStub) && PatchRel32(0x4A3422, 0xE8, (void*)&MnRaceStub) &&
         PatchRel32(0x4B610C, 0xE8, (void*)&MnLookupStub) && PatchRel32(0x472ECD, 0xE8, (void*)&MnLookupStub) &&
         PatchRel32(0x47327E, 0xE8, (void*)&MnLookupStub);
    if (ok && g_cfg.wsMenuFilter) {
        BYTE c[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
        DWORD rel = (DWORD)&MnFeFilterState - (0x4D7252 + 5); memcpy(c + 1, &rel, 4);
        Logf("widescreen: enlarged menus' big pictures filtered (Direct3D): %s\n", PatchBytes(0x4D7252, c, 6) ? "on" : "patch FAILED");
    }
    Logf("widescreen: menus at the %s resolution (%s): %s\n", g_cfg.wsMenuResolution == 2 ? "desktop's" : "chosen",
         g_cfg.wsMenus == 2 ? "stretched" : "4:3 in the middle", ok ? "on" : "patch FAILED");
}

// The HUD's panels (0x4844C0) are a frame (eight quads, shown by 0x47FBE0 and
// hidden by 0x47FCB0) and a see-through fill. With the software renderer
// ([0x7A3A58] bit 1) the game leaves the fills out - and for the standings
// list (frame [0x791AD8]) the frame as well: its test at 0x4847B5 jumps past
// both (0x4847BC: jne 0x484941), where the same test for the panel before it
// (0x48472B) only skips the fill. softStandingsBox: that jump goes to the
// frame's own lines (0x4847E5) instead, so the list is boxed like the map.
static void InstallSoftStandingsBox() {
    if (!g_knownExe || !g_cfg.softStandingsBox) return;
    DWORD rel = 0x4847E5 - 0x4847C2;
    Logf("hud: standings frame with the software renderer: %s\n", PatchBytes(0x4847BE, &rel, 4) ? "on" : "patch FAILED");
}

static void InstallWidescreen() {
    // (the cockpit picture is told apart by the wrapper around its draw call, whether or not anything else wants that)
    if (g_knownExe && g_cfg.alternatePixelCenter &&
        !(InstallDrawWrappers() && PatchRel32(0x482CA3, 0xE8, (void*)&XfWrapCockpit))) Logf("pixel centre: patch FAILED\n");
    if (!g_knownExe || !g_cfg.widescreen) { Logf("widescreen: off\n"); return; }
    if (g_cfg.wsUnlockResolutions) {
        BYTE b[9] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };
        DWORD rel = (DWORD)&ModeFilterStub - (0x4BEDDD + 5); memcpy(b + 1, &rel, 4);
        Logf("widescreen: every display mode listed%s: %s\n", g_cfg.ws32bit ? " (32-bit too)" : "",
             PatchBytes(0x4BEDDD, b, 9) ? "on" : "patch FAILED");
    }
    if (g_cfg.wsCockpit)
        Logf("widescreen: cockpit picture keeps its shape: %s\n",
             PatchRel32(0x482CA3, 0xE8, (void*)&XfWrapCockpit) &&
             PatchRel32(0x4894EC, 0xE8, (void*)&XfNeedlesStub) ? "on" : "patch FAILED");
    if (g_cfg.wsHud) Logf("widescreen: HUD in 4:3 proportions: %s\n", InstallHud() ? "on" : "off");
    InstallMenus();
    if (g_cfg.wsLoading)
        Logf("widescreen: loading screen 4:3: %s\n",
             PatchRel32(0x4A3468, 0xE8, (void*)&XfWrapLoadScreen) ? "on" : "patch FAILED");
    if (g_cfg.wsFixFOV) {
        BYTE b[13] = { 0xE8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        DWORD rel = (DWORD)&WsProjStub - (0x4DBD2B + 5); memcpy(b + 1, &rel, 4);
        static const BYTE fld[3] = { 0xD9, 0x45, 0xF8 };
        bool ok = PatchBytes(0x4DBD3F, fld, 3) && PatchBytes(0x4DBD48, fld, 3) && PatchBytes(0x4DBD2B, b, 13);
        Logf("widescreen: Hor+ 3D view %s\n", ok ? "on" : "patch FAILED");
        if (ok) {
            static const BYTE nop = 0x90;
            Logf("widescreen: Hor+ for the cameras at the race's start and finish: %s\n",
                 PatchRel32(0x4DBEA4, 0xE8, (void*)&WsZoomStub) && PatchBytes(0x4DBEA9, &nop, 1) ? "on" : "patch FAILED");
            static const DWORD rows[] = { 0x4796FF, 0x47971D, 0x479A51, 0x479A6F, 0x479D33, 0x479D50, 0x479D6D };
            bool hz = true;
            for (DWORD a : rows) hz = hz && PatchRel32(a, 0xE8, (void*)&HorizonRowStub);
            hz = hz && PatchRel32(0x47A90E, 0xE8, (void*)&HorizonStub0) && PatchRel32(0x47A92D, 0xE8, (void*)&HorizonStub1) &&
                 PatchRel32(0x47A93D, 0xE8, (void*)&HorizonStub2);
            Logf("widescreen: horizon extended to the screen's edges: %s\n", hz ? "on" : "patch FAILED");
        }
    }
}

static void InstallDiskSpaceFix() {
    // 0x4516A0 (eax = bytes wanted): 1 if the drive has them free. It multiplies
    // sectors per cluster, bytes per sector and free clusters with 32-bit imul and
    // compares signed, so a big disk fails it; and it answers 0 when the call fails.
    if (!g_knownExe || !g_cfg.noDiskSpaceCheck) { Logf("disk space check: on\n"); return; }
    static const BYTE yes[6] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };   // mov eax, 1 / ret
    Logf("disk space check: %s\n", PatchBytes(0x4516A0, yes, 6) ? "off" : "patch FAILED");
}

static void InstallMovieFix() {
    if (!g_knownExe || !g_cfg.fixMovieFreeze) { Logf("movies: original stream thread shutdown\n"); return; }
    bool ok = PatchRel32(hp::streamKillCall, 0xE8, (void*)&StreamKillStub);
    Logf("movies: %s\n", ok ? "stream thread allowed to exit before it's killed" : "patch FAILED");
}

static void InstallNoCD() {
    if (!g_knownExe || !g_cfg.noCDCheck) { Logf("CD check: on\n"); return; }
    static const BYTE kMovEax1[5] = { 0xB8, 0x01, 0x00, 0x00, 0x00 };
    bool ok = PatchBytes(hp::cdCheckCall, kMovEax1, sizeof(kMovEax1));
    Logf("CD check: %s\n", ok ? "off" : "patch FAILED");
}

// ---------------------------------------------------------------------------
// Portable registry: the game's own keys are answered here. The game opens
// them with RegOpenKeyExA and reads with RegQueryValueExA; it never writes the
// registry. "Thrash Driver" is renderer= from HPFrameFix.ini, "D3D Device" is
// d3dDevice=; a value the ini doesn't give comes from the Windows registry if
// it's there, and otherwise isn't there - which the game copes with (software
// renderer, primary display, no card workarounds, no network DLLs). Up to
// 0.1.0 the values were kept in HPFrameFix_registry.ini; a file left from then
// is still read, ahead of the Windows registry, but none is made any more.
// Other keys (Explorer's shell folders, the performance counters) go to the
// real registry.
// ---------------------------------------------------------------------------
typedef LONG (WINAPI *RegOpenKeyExA_t)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LONG (WINAPI *RegQueryValueExA_t)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (WINAPI *RegCloseKey_t)(HKEY);
typedef LONG (WINAPI *RegFlushKey_t)(HKEY);
static RegOpenKeyExA_t    g_oRegOpenKeyExA = nullptr;
static RegQueryValueExA_t g_oRegQueryValueExA = nullptr;
static RegCloseKey_t      g_oRegCloseKey = nullptr;
static RegFlushKey_t      g_oRegFlushKey = nullptr;
static char g_regIni[MAX_PATH] = {};     // HPFrameFix_registry.ini of an earlier version
static bool g_regIniOld = false;         // ... and it's there
static char g_rendererDll[64] = {};      // from renderer= ("" = the game's setting)
static HKEY g_realGameKey = nullptr;     // the real game key, when only renderer= is hooked

// The game matches "Thrash Driver" against its table of renderer DLLs at
// [0x55FD68] (case-sensitive, as a prefix) and quits with "files are corrupted"
// if nothing matches; it then loads the table's name, not the value.
static const char* const kThrashDlls[] = { "nulla.dll", "softtria.dll", "softlina.dll", "voodooa.dll",
                                           "voodoo2a.dll", "ragea.dll", "d3da.dll", "powervra.dll" };
// The game's match, without case: the first table name that starts with v
// ("d3da.dll", "D3DA.DLL", "d3da"...) -> that name, or null
static const char* ThrashName(const char* v) {
    size_t l = strlen(v);
    if (!l) return nullptr;
    for (const char* d : kThrashDlls) if (!_strnicmp(v, d, l)) return d;
    return nullptr;
}

// renderer= -> DLL name
static void ResolveRenderer() {
    static const struct { const char* alias; const char* dll; } kMap[] = {
        { "d3d", "d3da.dll" }, { "direct3d", "d3da.dll" },
        { "software", "softtria.dll" }, { "softtri", "softtria.dll" },
        { "voodoo", "voodooa.dll" }, { "3dfx", "voodooa.dll" }, { "glide", "voodooa.dll" },
        { "voodoo2", "voodoo2a.dll" },
    };
    g_rendererDll[0] = 0;
    if (!g_cfg.renderer[0]) return;
    for (const auto& m : kMap)
        if (!_stricmp(g_cfg.renderer, m.alias)) { lstrcpynA(g_rendererDll, m.dll, sizeof(g_rendererDll)); return; }
    if (const char* d = ThrashName(g_cfg.renderer)) { lstrcpynA(g_rendererDll, d, sizeof(g_rendererDll)); return; }
    Logf("renderer: \"%s\" isn't one of the game's renderers - ignored\n", g_cfg.renderer);
}

static LONG ServeValue(DWORD vtype, const void* buf, DWORD need, LPDWORD type, LPBYTE data, LPDWORD cb) {
    if (type) *type = vtype;
    if (!cb) return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!data) { *cb = need; return ERROR_SUCCESS; }
    if (*cb < need) { *cb = need; return ERROR_MORE_DATA; }
    memcpy(data, buf, need); *cb = need;
    return ERROR_SUCCESS;
}

enum { kVKeyGame = 1, kVKeyNps = 2 };
static const DWORD kVKeyBase = 0x7E570000;          // fake HKEY values for the virtual keys
struct VKeyDef { const char* sub; int id; };
static const VKeyDef kVKeys[] = {
    { "SOFTWARE\\Electronic Arts\\Need For Speed III", kVKeyGame },
    { "SOFTWARE\\EA-NPS\\DLL", kVKeyNps },
};
// unset = what the game does without the value (logged instead of "not set")
struct VValue { int key; const char* name; const char* entry; DWORD type; const char* unset; };
static const VValue kVValues[] = {
    { kVKeyGame, "Thrash Driver",         "ThrashDriver",         REG_SZ,    "not set - software renderer" },
    // THRASH_selectdisplay(n): the display adapter, 0 = the primary one
    { kVKeyGame, "D3D Device",            "D3DDevice",            REG_DWORD, "not set - primary display (0)" },
    // only compared with "RAGE PRO" and "STB Velocity 128" for card workarounds
    { kVKeyGame, "3D Device Description", "D3DDeviceDescription", REG_SZ,    "not set - no card workarounds (fine)" },
    { kVKeyNps,  "NPSDLL",                "NPSDLL",               REG_SZ,    "not set" },
    { kVKeyNps,  "LoginClient",           "LoginClient",          REG_SZ,    "not set" },
    { kVKeyNps,  "AnalyzeAPI",            "AnalyzeAPI",           REG_SZ,    "not set" },
    { kVKeyNps,  "NPSClientDLL",          "NPSClientDLL",         REG_SZ,    "not set" },
    { kVKeyNps,  "DEVELOP MODE",          "DevelopMode",          REG_SZ,    "not set" },
};
static const VKeyDef* VKeyFor(HKEY root, LPCSTR sub) {
    if (root != HKEY_LOCAL_MACHINE || !sub) return nullptr;
    char k[256]; lstrcpynA(k, sub, sizeof(k));
    size_t n = strlen(k); while (n && k[n - 1] == '\\') k[--n] = 0;
    for (const auto& v : kVKeys) if (!_stricmp(k, v.sub)) return &v;
    return nullptr;
}
static int VKeyId(HKEY h) {
    DWORD v = reinterpret_cast<DWORD>(h);
    return (v > kVKeyBase && v <= kVKeyBase + kVKeyNps) ? (int)(v - kVKeyBase) : 0;
}
static const VValue* VValueFor(int id, LPCSTR name) {
    const char* n = name ? name : "";
    for (const auto& v : kVValues) if (v.key == id && !_stricmp(n, v.name)) return &v;
    return nullptr;
}
static void VLogOnce(const char* entry, const char* what) {
    static char seen[16][24]; static int n = 0;
    for (int i = 0; i < n; i++) if (!strcmp(seen[i], entry)) return;
    if (n < 16) lstrcpynA(seen[n++], entry, 24);
    LogfAsync("registry: %s %s\n", entry, what);
}

// a value of one of the game's keys, straight from the Windows registry
static LONG RealValue(int id, LPCSTR name, LPDWORD type, LPBYTE data, LPDWORD cb) {
    HKEY k = nullptr;
    if (g_oRegOpenKeyExA(HKEY_LOCAL_MACHINE, kVKeys[id - 1].sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return ERROR_FILE_NOT_FOUND;
    LONG r = g_oRegQueryValueExA(k, name, nullptr, type, data, cb);
    g_oRegCloseKey(k);
    return r;
}

// The hooks are in place with portableRegistry=1, or with renderer= set (then
// only the game key's "Thrash Driver" is answered; the rest is the real registry).
static LONG WINAPI HkRegOpenKeyExA(HKEY root, LPCSTR sub, DWORD o, REGSAM sam, PHKEY out) {
    if (const VKeyDef* k = VKeyFor(root, sub)) {
        if (!out) return ERROR_INVALID_PARAMETER;
        if (g_cfg.portableRegistry) {
            *out = reinterpret_cast<HKEY>(kVKeyBase + k->id);
            return ERROR_SUCCESS;
        }
        if (k->id == kVKeyGame) {
            LONG r = g_oRegOpenKeyExA(root, sub, o, sam, out);
            if (r == ERROR_SUCCESS) { g_realGameKey = *out; return r; }
            *out = reinterpret_cast<HKEY>(kVKeyBase + k->id);   // not installed: still answer renderer=
            return ERROR_SUCCESS;
        }
    }
    return g_oRegOpenKeyExA(root, sub, o, sam, out);
}
static LONG WINAPI HkRegQueryValueExA(HKEY h, LPCSTR name, LPDWORD r, LPDWORD type, LPBYTE data, LPDWORD cb) {
    int id = VKeyId(h);
    bool thrash = name && !_stricmp(name, "Thrash Driver");
    if (thrash && g_rendererDll[0] && (id == kVKeyGame || (g_realGameKey && h == g_realGameKey))) {
        static bool logged = false;
        if (!logged) { logged = true; LogfAsync("renderer: %s (renderer=%s in HPFrameFix.ini)\n", g_rendererDll, g_cfg.renderer); }
        return ServeValue(REG_SZ, g_rendererDll, (DWORD)strlen(g_rendererDll) + 1, type, data, cb);
    }
    if (!id) return g_oRegQueryValueExA(h, name, r, type, data, cb);
    if (!g_cfg.portableRegistry) return ERROR_FILE_NOT_FOUND;    // renderer-only virtual key
    const VValue* v = VValueFor(id, name);
    if (!v) return RealValue(id, name, type, data, cb);
    if (!_stricmp(v->entry, "D3DDevice") && g_cfg.d3dDevice >= 0) {
        DWORD x = (DWORD)g_cfg.d3dDevice;
        VLogOnce(v->entry, "from d3dDevice= in HPFrameFix.ini");
        return ServeValue(REG_DWORD, &x, 4, type, data, cb);
    }
    char text[512] = {};
    if (g_regIniOld) GetPrivateProfileStringA("Registry", v->entry, "", text, sizeof(text), g_regIni);
    if (!text[0]) {
        LONG rr = RealValue(id, name, type, data, cb);
        VLogOnce(v->entry, rr == ERROR_SUCCESS || rr == ERROR_MORE_DATA ? "from the Windows registry" : v->unset);
        return rr;
    }
    if (thrash) {                    // the game's own spelling, so "D3DA.DLL" or "d3da" works too
        const char* d = ThrashName(text);
        if (!d) { VLogOnce(v->entry, "isn't one of the game's renderers - software used"); return ERROR_FILE_NOT_FOUND; }
        lstrcpynA(text, d, sizeof(text));
    }
    VLogOnce(v->entry, "read from HPFrameFix_registry.ini (a file of an earlier version)");
    if (v->type == REG_DWORD) {
        DWORD x = (DWORD)strtoul(text, nullptr, 0);
        return ServeValue(REG_DWORD, &x, 4, type, data, cb);
    }
    return ServeValue(v->type, text, (DWORD)strlen(text) + 1, type, data, cb);
}
static LONG WINAPI HkRegCloseKey(HKEY h) {
    if (VKeyId(h)) return ERROR_SUCCESS;
    if (h && h == g_realGameKey) g_realGameKey = nullptr;
    return g_oRegCloseKey(h);
}
static LONG WINAPI HkRegFlushKey(HKEY h) {
    if (VKeyId(h)) return ERROR_SUCCESS;
    return g_oRegFlushKey(h);
}

static void InstallPortableRegistry() {
    ResolveRenderer();
    if (!g_knownExe || (!g_cfg.portableRegistry && !g_rendererDll[0])) { Logf("registry: Windows registry\n"); return; }
    snprintf(g_regIni, sizeof(g_regIni), "%sHPFrameFix_registry.ini", g_asiDir[0] ? g_asiDir : g_gameDir);
    void** slots[4] = {};
    static const char* kNames[4] = { "RegOpenKeyExA", "RegQueryValueExA", "RegCloseKey", "RegFlushKey" };
    ForEachImport("ADVAPI32.dll", [&](void** slot, const char* nm) {
        for (int i = 0; i < 4; i++) if (!strcmp(nm, kNames[i])) slots[i] = slot;
    });
    for (int i = 0; i < 4; i++)
        if (!slots[i]) { Logf("registry: %s import not found - Windows registry used\n", kNames[i]); return; }
    g_oRegOpenKeyExA    = (RegOpenKeyExA_t)*slots[0];
    g_oRegQueryValueExA = (RegQueryValueExA_t)*slots[1];
    g_oRegCloseKey      = (RegCloseKey_t)*slots[2];
    g_oRegFlushKey      = (RegFlushKey_t)*slots[3];
    void* hooks[4] = { (void*)&HkRegOpenKeyExA, (void*)&HkRegQueryValueExA, (void*)&HkRegCloseKey,
                       (void*)&HkRegFlushKey };
    bool ok = true;
    for (int i = 0; i < 4; i++) ok &= PatchPointer(slots[i], hooks[i], nullptr);
    if (!ok) Logf("registry: patch FAILED - Windows registry used\n");
    else if (g_cfg.portableRegistry) {
        g_regIniOld = GetFileAttributesA(g_regIni) != INVALID_FILE_ATTRIBUTES;
        Logf("registry: answered by the mod (renderer=, d3dDevice=; the rest from the Windows registry if it's there)%s\n",
             g_regIniOld ? "; HPFrameFix_registry.ini of an earlier version is still read" : "");
    }
    else Logf("registry: Windows registry\n");
    if (ok && g_rendererDll[0]) Logf("renderer: %s\n", g_rendererDll);
}

// ---------------------------------------------------------------------------
// Game threads on one core (as SilentPatch's SingleProcAffinity for NFS3): the
// main thread and every thread the game (or eacsnd.dll) creates run on the
// first core; threads of Windows/Wine and of this mod keep all cores. Pinning
// the whole process instead would also put DirectSound's mixer on that core.
// ---------------------------------------------------------------------------
typedef HANDLE (WINAPI *CreateThread_t)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
static CreateThread_t g_oCreateThread = nullptr;
static volatile DWORD_PTR g_gameCore = 0;
static std::atomic<unsigned long> g_pinnedThreads{0};
// The game's threads, kept so they can be put back on their core (AffinityWatch)
static CRITICAL_SECTION g_gtLock; static bool g_gtLockInit = false;
static HANDLE g_gameThreads[64]; static int g_gameThreadN = 0;
static void NoteGameThread(HANDLE h) {
    HANDLE d = nullptr;
    if (!g_gtLockInit || !DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, 0, FALSE, DUPLICATE_SAME_ACCESS)) return;
    EnterCriticalSection(&g_gtLock);
    for (int i = 0; i < g_gameThreadN; )          // (threads that have ended make room)
        if (WaitForSingleObject(g_gameThreads[i], 0) == WAIT_OBJECT_0) { CloseHandle(g_gameThreads[i]); g_gameThreads[i] = g_gameThreads[--g_gameThreadN]; }
        else i++;
    if (g_gameThreadN < 64) g_gameThreads[g_gameThreadN++] = d; else CloseHandle(d);
    LeaveCriticalSection(&g_gtLock);
}
static HANDLE WINAPI HkCreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg,
                                    DWORD flags, LPDWORD id) {
    HANDLE h = g_oCreateThread(sa, stack, fn, arg, flags | CREATE_SUSPENDED, id);
    if (h) {
        // (under the lock: AffinityWatch either sees this thread in the list or has finished before it's pinned)
        if (g_gtLockInit) EnterCriticalSection(&g_gtLock);
        if (SetThreadAffinityMask(h, g_gameCore)) g_pinnedThreads++;
        NoteGameThread(h);
        if (g_gtLockInit) LeaveCriticalSection(&g_gtLock);
        if (!(flags & CREATE_SUSPENDED)) ResumeThread(h);
    }
    return h;
}
// Something else can pin the whole process to one core after this mod has
// loaded - a DirectDraw or Direct3D wrapper's "single core" option does that
// when the game starts its renderer. That puts the system's sound threads and
// this mod's own on the game's core after all, and leaves the game's threads
// with a core of ours that the process no longer has. Looked at once a second
// (the log writer's thread): if the process has come down to one core, it gets
// all of them back (widenAffinity), the game's threads go to the core the
// other one picked, and this mod's threads get every core again.
static void AffinityWatch() {
    if (!g_gameCore || !g_cfg.widenAffinity || !g_gtLockInit) return;
    DWORD_PTR proc = 0, sys = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) || !proc) return;
    if ((proc & (proc - 1)) || !(sys & (sys - 1))) return;        // (not pinned, or there is only one core)
    static int times = 0;
    EnterCriticalSection(&g_gtLock);              // (no game thread is created and pinned half way through this)
    if (!SetProcessAffinityMask(GetCurrentProcess(), sys)) {
        LeaveCriticalSection(&g_gtLock);
        if (times++ < 3) Logf("threads: the process was pinned to one core (mask %lX) after the mod loaded - couldn't be widened\n", (unsigned long)proc);
        return;
    }
    g_gameCore = proc;
    int n = 0;
    for (int i = 0; i < g_gameThreadN; i++) if (SetThreadAffinityMask(g_gameThreads[i], proc)) n++;
    LeaveCriticalSection(&g_gtLock);
    LONG mods = g_modThreadN;
    for (LONG i = 0; i < mods && i < 8; i++) if (g_modThreads[i]) SetThreadAffinityMask(g_modThreads[i], sys);
    if (times++ < 8) Logf("threads: something pinned the whole process to one core (mask %lX) after the mod loaded - "
                          "all cores given back, the game's %d thread(s) kept on that core\n", (unsigned long)proc, n);
}
static bool HookCreateThreadIn(HMODULE mod, const char* what) {
    bool hooked = false;
    ForEachImportIn(mod, "KERNEL32.dll", [&](void** s, const char* nm) {
        if (strcmp(nm, "CreateThread")) return;
        void* old = nullptr;
        if (*s != (void*)&HkCreateThread && PatchPointer(s, (void*)&HkCreateThread, &old)) {
            if (!g_oCreateThread) g_oCreateThread = (CreateThread_t)old;
            hooked = true;
        }
    });
    if (!hooked) Logf("threads: %s - CreateThread import not found\n", what);
    return hooked;
}
static void InstallGameThreadsOneCore() {
    if (!g_knownExe || !g_cfg.gameThreadsOneCore) { Logf("threads: all cores\n"); return; }
    DWORD_PTR proc = 0, sys = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) || !proc) { Logf("threads: affinity unknown - all cores\n"); return; }
    // A process that's already on one core (taskset, a launcher's or wrapper's
    // single-core option) has the system's threads on that core too - under
    // Wine that's DirectSound's mixer and the audio driver's threads, which then
    // wait behind the game thread whenever it's busy, and the sound pops. It
    // gets all the system's cores back (which gives them to every thread it has
    // now), and from here on it's the usual arrangement: the game's own threads
    // on the core it was on, the rest free.
    DWORD_PTR was = proc;
    if (!(proc & (proc - 1))) {
        if (!g_cfg.widenAffinity || !(sys & (sys - 1)) || !SetProcessAffinityMask(GetCurrentProcess(), sys)) {
            Logf("threads: the game already has one core (process mask %lX, system mask %lX)%s\n", (unsigned long)proc, (unsigned long)sys,
                 !g_cfg.widenAffinity ? "" : (sys & (sys - 1)) ? " - couldn't be widened" : " - the system shows only one");
            return;
        }
        Logf("threads: the process was on one core (mask %lX) - given all of them (mask %lX)\n", (unsigned long)proc, (unsigned long)sys);
        proc = sys;
    }
    g_gameCore = (was & (was - 1)) ? proc & (~proc + 1) : was;   // (the core it was pinned to, if it was)
    InitializeCriticalSection(&g_gtLock); g_gtLockInit = true;   // (before the hook: HkCreateThread uses it)
    if (!HookCreateThreadIn(GetModuleHandleA(nullptr), "nfs3.exe")) {
        if (was != proc) SetProcessAffinityMask(GetCurrentProcess(), was);   // (can't pin the game's threads: as it was)
        g_gameCore = 0; return;
    }
    SetThreadAffinityMask(GetCurrentThread(), g_gameCore);      // DllMain runs on the game's main thread
    NoteGameThread(GetCurrentThread());
    int core = 0; while (!((g_gameCore >> core) & 1)) core++;
    Logf("threads: the game's threads on core %d, the rest on all cores\n", core);
}

// ---------------------------------------------------------------------------
// Renderer DLLs from the drivers folder
// ---------------------------------------------------------------------------
typedef HMODULE (WINAPI *LoadLibraryA_t)(LPCSTR);
static LoadLibraryA_t g_oLoadLibraryA = nullptr;
static char g_driversDir[MAX_PATH] = {};     // with a trailing backslash
static bool g_driversExist = false;

static bool IsRendererDll(LPCSTR name) {
    for (const char* d : kThrashDlls) if (!_stricmp(name, d)) return true;
    return false;
}
// eacsnd.dll (the game's DirectSound driver, RVAs below) has three ways to play:
//  - primary: it mixes straight into DirectSound's primary buffer
//    (DSSCL_WRITEPRIMARY), 20 ms ahead of the play cursor. Used unless
//    DirectSound reports an emulated driver.
//  - emulated (DSCAPS_EMULDRIVER): a 1.9 s looping secondary buffer written
//    8192 samples (~370 ms) ahead: no crackling, but sound lags far behind.
//  - secondary: what it falls back to when it can't get the primary buffer: a
//    0.4 s secondary buffer written 20 ms ahead, with the same adaptive lead as
//    the primary mode (it grows if the play cursor catches up).
// The primary and secondary modes are left to the game's choice: with the
// lead fixed below and the system's sound threads off the game's core
// (widenAffinity), the primary mode plays cleanly. Their lead in ms is the
// immediate at 0x1CA6 ("mov [lead], 20"): soundLatencyMs.
// The crackle: the adaptive lead (0x21E4, run by every serve outside the
// emulated mode) grows the lead by the shortfall after an underrun, but also
// shrinks it: once 3000 serves (~23 s) have passed without an underrun, every
// 10th serve takes 16 samples off the lead as long as the least data left
// ahead of the play cursor over those 10 serves was over 16 samples. So
// whatever the starting lead, it's worn down to about one serve interval
// (7.8 ms) plus 16 samples, and then any late serve - a menu switch's ~30 ms
// gap, or a system mixer reading in 10 ms blocks - lets the play cursor
// overtake the data: it plays the buffer's old contents from one lap back, a
// pop, and the lead grows, only to be worn down again. "cmp [min left],16 /
// jle keep" at 0x2266 -> always jmp: the lead never shrinks, so it stays at
// soundLatencyMs and only grows after an underrun.
static void PatchEacsnd(HMODULE h) {
    static HMODULE done = nullptr;
    if (!h || h == done) return;
    done = h;
    BYTE* b = (BYTE*)h;
    BYTE* lead  = b + 0x1CA0;    // C7 05 <&lead> 14 00 00 00
    BYTE* keep  = b + 0x2266;    // 83 3D <&min left> 10 7E 56
    bool known = !IsBadReadPtr(lead, 10) && !IsBadReadPtr(keep, 9) &&
        keep[0] == 0x83 && keep[1] == 0x3D && *(DWORD*)(keep + 2) == (DWORD)b + 0xA8DC &&
        keep[6] == 0x10 && keep[7] == 0x7E && keep[8] == 0x56 &&
        lead[0] == 0xC7 && lead[1] == 0x05 && *(DWORD*)(lead + 2) == (DWORD)b + 0xA8CC &&
        *(DWORD*)(lead + 6) == 20;
    if (!known) { LogfAsync("sound: eacsnd.dll isn't the known version - left as is\n"); return; }
    static const BYTE jmp = 0xEB;
    bool ok = PatchBytes((DWORD)(keep + 7), &jmp, 1);
    DWORD ms = (DWORD)(g_cfg.soundLatencyMs < 5 ? 5 : g_cfg.soundLatencyMs > 200 ? 200 : g_cfg.soundLatencyMs);
    if (ok && ms != 20) ok = PatchBytes((DWORD)(lead + 6), &ms, 4);
    if (!ok) LogfAsync("sound: patch FAILED\n");
    else {
        g_eacBuf = (void**)(b + 0xA8A8);
        if (CaptureSeconds() > 0) g_eacMixSlot = (void**)(b + 0xA904);   // (the mix is only looked at for a capture)
        g_eacLead = (volatile LONG*)(b + 0xA8CC);
        g_eacRate = (volatile LONG*)(b + 0xA900);
        LogfAsync("sound: eacsnd.dll writes %lu ms ahead of playback (the lead only grows)\n", (unsigned long)ms);
    }
}
static bool IsEacsnd(LPCSTR name) {
    const char* b = name;
    for (const char* c = name; *c; c++) if (*c == '\\' || *c == '/' || *c == ':') b = c + 1;
    return !_stricmp(b, "eacsnd.dll") || !_stricmp(b, "eacsnd");
}

static HMODULE WINAPI HkLoadLibraryA(LPCSTR name) {
    if (name && IsEacsnd(name)) {
        HMODULE h = g_oLoadLibraryA(name);
        if (h && g_cfg.fixSound) PatchEacsnd(h);
        if (h && g_gameCore) {                       // its threads are the game's too
            static HMODULE hooked = nullptr;
            if (h != hooked && HookCreateThreadIn(h, "eacsnd.dll")) { hooked = h; LogfAsync("threads: eacsnd.dll's threads too\n"); }
        }
        return h;
    }
    if (name && IsRendererDll(name)) {
        const char* b = name;
        for (const char* q = name; *q; q++) if (*q == '\\' || *q == '/' || *q == ':') b = q + 1;
        g_softRenderer = !_strnicmp(b, "soft", 4);
        g_texBig = !_strnicmp(b, "d3d", 3);
    }
    if (name && g_driversDir[0] && !strpbrk(name, "\\/:") && IsRendererDll(name)) {
        static char seen[8][24]; static int nSeen = 0;            // log each DLL once
        bool first = true;
        for (int i = 0; i < nSeen; i++) if (!_stricmp(seen[i], name)) first = false;
        if (first && nSeen < 8) lstrcpynA(seen[nSeen++], name, 24);
        char path[MAX_PATH];
        if (snprintf(path, sizeof(path), "%s%s", g_driversDir, name) < (int)sizeof(path) &&
            GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
            // The DLL's own dependencies (glide2x.dll...) are looked for in the game
            // folder first, as before, then in the drivers folder.
            char prev[MAX_PATH]; DWORD pl = GetDllDirectoryA(sizeof(prev), prev);
            SetDllDirectoryA(g_driversDir);
            HMODULE h = g_oLoadLibraryA(path);
            DWORD err = h ? 0 : GetLastError();
            SetDllDirectoryA(pl && pl < sizeof(prev) ? prev : nullptr);
            if (first) LogfAsync("drivers: %s %s (error %lu)\n", path, h ? "loaded" : "FAILED", (unsigned long)err);
            if (h) return h;
        } else if (first && g_driversExist) {
            LogfAsync("drivers: %s not in %s - game folder\n", name, g_driversDir);
        }
    }
    return g_oLoadLibraryA(name);
}
// The game's LoadLibraryA: renderer DLLs from the drivers folder, eacsnd.dll patched.
static void InstallLoadLibraryHook() {
    if (!g_knownExe) return;
    if (g_cfg.driversFolder[0]) {
        const char* f = g_cfg.driversFolder;
        bool absolute = strchr(f, ':') || (f[0] == '\\' && f[1] == '\\');
        int n = snprintf(g_driversDir, sizeof(g_driversDir), "%s%s", absolute ? "" : g_gameDir, f);
        if (n <= 0 || n >= (int)sizeof(g_driversDir) - 1) { Logf("drivers: path too long - game folder\n"); g_driversDir[0] = 0; }
        else {
            while (n && (g_driversDir[n - 1] == '\\' || g_driversDir[n - 1] == '/')) g_driversDir[--n] = 0;
            g_driversDir[n++] = '\\'; g_driversDir[n] = 0;
        }
    }
    if (!g_driversDir[0] && !g_cfg.fixSound && !g_gameCore) { Logf("drivers: game folder\nsound: original\n"); return; }
    void** slot = nullptr;
    ForEachImport("KERNEL32.dll", [&](void** s, const char* nm) { if (!strcmp(nm, "LoadLibraryA")) slot = s; });
    if (!slot || !PatchPointer(slot, (void*)&HkLoadLibraryA, (void**)&g_oLoadLibraryA)) {
        g_driversDir[0] = 0;
        Logf("drivers / sound: patch FAILED\n"); return;
    }
    if (g_driversDir[0]) {
        DWORD a = GetFileAttributesA(g_driversDir);
        g_driversExist = a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
        Logf("drivers: %s%s\n", g_driversDir, g_driversExist ? "" : " (not there - game folder)");
    } else Logf("drivers: game folder\n");
    if (g_cfg.fixSound) Logf("sound: eacsnd.dll patched when the game loads it\n");
    else Logf("sound: original\n");
}

// ---------------------------------------------------------------------------
// Stats thread
// ---------------------------------------------------------------------------
static DWORD WINAPI StatsThread(LPVOID) {
    unsigned long ls = 0, lr = 0, le = 0, lf = 0, lb = 0, lrej = 0, lcut = 0, lmf = 0;
    for (;;) {
        int sec = g_cfg.logEverySeconds > 0 ? g_cfg.logEverySeconds : 10;
        Sleep(sec * 1000);
        LoadStatsTick();
        {
            static unsigned long lsc = 0;
            unsigned long sc = g_sndCalls;
            if (sc != lsc)
                Logf("last %ds: sound top-ups %.1f/s, longest gap %.1f ms (%ld over 15 ms), longest top-up %.1f ms, "
                     "%ld underrun(s), lead %.1f ms\n",
                     sec, (sc - lsc) / (double)sec, g_sndMaxGapUs.exchange(0) / 1000.0,
                     g_sndLateGaps.exchange(0), g_sndMaxRunUs.exchange(0) / 1000.0, g_sndUnderruns.exchange(0),
                     g_eacLead && g_eacRate && *g_eacRate > 0 ? *g_eacLead * 1000.0 / *g_eacRate : -1.0);
            lsc = sc;
        }
        unsigned long mf = g_menuFrames;
        if (mf != lmf) Logf("last %ds: menu frames %.1f/s, longest %.0f ms\n", sec, (mf - lmf) / (double)sec,
                            g_menuGapMaxUs.exchange(0) / 1000.0);
        lmf = mf;
        unsigned long s = g_steps, r = g_renders, e = g_extraRenders, f = g_tickFires,
                      b = g_blended, rj = g_rejected, c = g_cuts;
        if (s != ls || r != lr)
            Logf("last %ds: sim %.1f/s, frames %.1f/s (%.1f/s between steps), ticks %.1f/s, "
                 "blended %lu, not blended %lu, camera-cut frames %lu, avg blend %.2f, clamped %lu, "
                 "resets %lu, clock resyncs %lu, tick resyncs %lu\n",
                 sec, (s - ls) / (double)sec, (r - lr) / (double)sec, (e - le) / (double)sec,
                 (f - lf) / (double)sec, b - lb, rj - lrej, c - lcut,
                 g_alphaN ? g_alphaSum / g_alphaN : 0.0, (unsigned long)g_alphaClamped,
                 (unsigned long)g_resets, (unsigned long)g_clockSnaps, (unsigned long)g_tickResyncs);
        if (g_clampHave && s != ls)
            Logf("  clamps: first at %02d:%02d:%02d.%03d, frame up to %.1f ms past the newest step, %.1f ms before the oldest\n",
                 g_clampFirst.wHour, g_clampFirst.wMinute, g_clampFirst.wSecond, g_clampFirst.wMilliseconds,
                 (double)g_clampAheadMs, (double)g_clampBehindMs);
        g_clampHave = false; g_clampAheadMs = 0; g_clampBehindMs = 0;
        g_alphaSum = 0; g_alphaN = 0; g_alphaClamped = 0;
        ls = s; lr = r; le = e; lf = f; lb = b; lrej = rj; lcut = c;
    }
}

// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&g_logLock); g_logLockInit = true;
        InitializeCriticalSection(&g_qLock);
        InitializeCriticalSection(&g_loadLock);
        QueryPerformanceFrequency(&g_qpf);
        LoadConfig();
        OpenLog();
        g_qEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        g_knownExe = CheckExe();
        timeBeginPeriod(1);
        g_limTimer = CreateHiResTimer();
        InstallGameThreadsOneCore();
        InstallTickScheduler();
        InstallInterpolation();
        InstallMenuUnlock();
        InstallMenuAnimations();
        InstallLoading();
        InstallPortableRegistry();
        InstallLoadLibraryHook();
        InstallInstallPaths();
        InstallNoCD();
        InstallMovieFix();
        InstallDiskSpaceFix();
        InstallCarDetail();
        InstallTextureSize();
        InstallAirDrag();
        InstallCloseFix();
        InstallWidescreen();
        InstallSoftStandingsBox();
        InstallD3dFog();
        InstallSoundTiming();
        if (g_qEvent) if (HANDLE th = StartModThread(WriterThread)) CloseHandle(th);
        if (g_log) if (HANDLE th = StartModThread(StatsThread)) CloseHandle(th);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (reserved) g_logLockInit = false;
        // On process exit (reserved != 0) other threads are already gone and may
        // have left the queue lock held: don't touch it then.
        if (g_log) { if (!reserved) DrainLogQueue(); Logf("=== shutdown ===\n"); fclose(g_log); g_log = nullptr; }
    }
    return TRUE;
}
