// What the other parts use from config.cpp.
#pragma once
#include "common.h"
#include "nightlamps.h"

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
    float projLightReach   = 1.5f;  // projected headlights: their distances x this (1 = the game's)
    float projLightBrightness = 0.75f; // projected headlights: their colour x this
    float projLightWidth   = 1.3f;  // projected headlights: their beam this much wider
    float nightLamps       = 1.0f;  // night: the lamps light what's around them, x this (0 = off)
    float nightLampReach   = 22.0f; // night: a street lamp's light reaches this far (m, game's track size)
    float nightScenery     = 1.0f;  // night: the lights found on the game's own tracks, x this (0 = off)
    float airDrag          = 0.0f;  // air drag on cars in the air, x the game's (1 = the game's, 0 = none)
    float trackScale       = 0.85f; // tracks' size, x the game's (High Stakes': 0.7692; 1 = the game's)
    float trackScaleOwn[9] = {};    // a track's own size, by its number (0 = none: trackScale's)
    float speedBlur        = 1.0f;  // Direct3D: radial blur at the screen's edges at speed (0 = off)
    float cameraShake      = 1.0f;  // the view shakes slightly at speed (0 = off)
    int   speedBlurFrom    = 90;    // km/h where speedBlur starts
    int   cameraShakeFrom  = 90;    // km/h where cameraShake starts
    int   speedEffectsFull = 320;   // km/h where both are at full strength
    float lowCamLoose      = 1.0f;  // low and roof cameras: follow the car's turns and bumps with a lag (0 = rigid, the game's)
    float lowCamSpeedFov   = 8.0f;  // low and roof cameras: degrees their view widens by at speedEffectsFull (0 = fixed)
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
    int   fixMusicMaps     = 1;     // Atlantica's techno music no longer stuck on two bars
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
extern Config g_cfg;
static const char* const kTrackName[9] = { "Hometown", "Redrock Ridge", "Atlantica", "Rocky Pass", "Country Woods",
                                           "Lost Canyons", "Aquatica", "Summit", "Empire City" };
extern char g_asiDir[MAX_PATH];
extern char g_gameDir[MAX_PATH];
void LoadConfig();
