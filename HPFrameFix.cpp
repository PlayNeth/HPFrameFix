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

#include "src/common.h"
#include "src/config.h"
#include "src/log.h"
#include "src/helpers.h"
#include "src/timing.h"
#include "src/interp.h"
#include "src/loading.h"
#include "src/paths.h"
#include "src/sound.h"
#include "src/movie.h"
#include "src/menuanim.h"
#include "src/detail.h"
#include "src/drag.h"
#include "src/trackscale.h"
#include "src/speedfx.h"
#include "src/cameras.h"
#include "src/headlights.h"
#include "src/nightlamps.h"
#include "src/window.h"
#include "src/fog.h"
#include "src/hud.h"
#include "src/menus.h"
#include "src/diskspace.h"
#include "src/music.h"
#include "src/nocd.h"
#include "src/portable.h"
#include "src/threads.h"
#include "src/drivers.h"
#include "src/stats.h"

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
        InstallMusicMapFix();
        InstallDiskSpaceFix();
        InstallCarDetail();
        InstallTextureSize();
        InstallAirDrag();
        InstallTrackScale();
        InstallSpeedEffects();
        InstallLowCam();
        InstallProjLights();
        InstallNightLamps();
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
