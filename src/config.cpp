#include "common.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

Config g_cfg;
// The tracks by number (TRK000..TRK008, in the menus' order): their names in ini
// keys (trackScaleHometown, ...) and in the log
static const char* const kTrackKey[9]  = { "Hometown", "RedrockRidge", "Atlantica", "RockyPass", "CountryWoods",
                                           "LostCanyons", "Aquatica", "Summit", "EmpireCity" };
static int TrackKey(const char* s) {
    for (int t = 0; t < 9; t++) if (!_stricmp(s, kTrackKey[t])) return t;
    return -1;
}
char g_asiDir[MAX_PATH] = {};
char g_gameDir[MAX_PATH] = {};

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
    "; Night races with projected headlights (the game's graphics options): how far they\n"
    "; reach (x the game's: 10 m before they start to fade and 40 m to nothing for your\n"
    "; car), how bright their light is, and how wide the beam is. 1 = the game's\n"
    "projLightReach=1.5\n"
    "projLightBrightness=0.75\n"
    "projLightWidth=1.3\n"
    "\n"
    "; Night races: the lamps along the track light the road and what's around them,\n"
    "; where the game leaves the night pitch black around its glowing lamps (the light\n"
    "; is added to the track's colours as it loads; the headlights work as before).\n"
    "; nightLamps: how bright (0 = off: the game's night). nightLampReach: how far a\n"
    "; street lamp's light reaches, in metres at the game's track size\n"
    "nightLamps=1\n"
    "nightLampReach=22\n"
    "\n"
    "; With nightLamps, on the game's own tracks: the buildings' windows, lit signs,\n"
    "; tunnel lights and the like give light too, the cities get street lighting and a\n"
    "; lit sky's glow, and moving things such as Atlantica's boat are lit. How bright\n"
    "; (0 = off)\n"
    "nightScenery=1\n"
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
    "; The tracks' size, times the game's. Everything on them shrinks with them (the\n"
    "; road, the scenery, its objects, the trackside cameras); the cars stay as they\n"
    "; are, so the same speed covers more of the track. 1 = the game's size. High\n"
    "; Stakes shrank this game's tracks by exactly 0.7692 when it brought them over;\n"
    "; the narrowest roads (stretches of Rocky Pass and Summit, 8 m wide at the\n"
    "; game's size) then only just fit three cars side by side. At 0.85 there's room\n"
    "; between them\n"
    "trackScale=0.85\n"
    "\n"
    "; To give a track a size of its own, add a line for it, such as\n"
    "; trackScaleHometown=0.7. The tracks: Hometown, RedrockRidge, Atlantica,\n"
    "; RockyPass, CountryWoods, LostCanyons, Aquatica, Summit, EmpireCity\n"
    "\n"
    "\n"
    "[Speed effects]\n"
    "\n"
    "; Direct3D: the picture blurs outwards towards the screen's edges as you go faster,\n"
    "; while the middle stays sharp. The number is its strength: 0 = off, 2 = twice as\n"
    "; strong. The HUD and the mirror stay sharp; in the cockpit view the dashboard\n"
    "; is blurred along with the road\n"
    "speedBlur=1.0\n"
    "\n"
    "; The view shakes slightly as you go faster (every renderer). The number is its\n"
    "; strength: 0 = off, 2 = twice as strong\n"
    "cameraShake=1.0\n"
    "\n"
    "; Speeds in km/h where the blur and the shake start, and where both are at full\n"
    "; strength\n"
    "speedBlurFrom=90\n"
    "cameraShakeFrom=90\n"
    "speedEffectsFull=320\n"
    "\n"
    "\n"
    "[Cameras]\n"
    "\n"
    "; The low and roof cameras are fixed to the car: every bump and twitch goes\n"
    "; straight into the picture. 1 = they follow the car's turns, pitch and roll with a\n"
    "; lag, swinging round the car in turns and riding out bumps; 2 = looser still.\n"
    "; 0 = the game's\n"
    "lowCamLoose=1.0\n"
    "\n"
    "; The low and roof cameras' view widens with speed, by up to this many degrees at\n"
    "; speedEffectsFull. Needs wsFixFOV. 0 = the game's fixed view\n"
    "lowCamSpeedFov=8\n"
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
    "; Atlantica's (and Aquatica's) techno music: its middle part is only two bars,\n"
    "; which pointed back at each other, so at middle intensity they played over and\n"
    "; over. 1 = after them the music goes on to the next part, as it does on the\n"
    "; other tracks. 0 = the game's\n"
    "fixMusicMaps=1\n"
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

void LoadConfig() {
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
        else if (!_stricmp(key, "fixMusicMaps"))         g_cfg.fixMusicMaps = atoi(val);
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
        else if (!_stricmp(key, "projLightReach"))       g_cfg.projLightReach = (float)atof(val);
        else if (!_stricmp(key, "projLightBrightness"))  g_cfg.projLightBrightness = (float)atof(val);
        else if (!_stricmp(key, "projLightWidth"))       g_cfg.projLightWidth = (float)atof(val);
        else if (!_stricmp(key, "nightLamps"))           g_cfg.nightLamps = (float)atof(val);
        else if (!_stricmp(key, "nightLampReach"))       g_cfg.nightLampReach = (float)atof(val);
        else if (!_stricmp(key, "nightScenery"))         g_cfg.nightScenery = (float)atof(val);
        else if (!_stricmp(key, "airDrag"))              g_cfg.airDrag = (float)atof(val);
        else if (!_stricmp(key, "trackScale"))           g_cfg.trackScale = (float)atof(val);
        else if (!_strnicmp(key, "trackScale", 10) && TrackKey(key + 10) >= 0)
                                                         g_cfg.trackScaleOwn[TrackKey(key + 10)] = (float)atof(val);
        else if (!_stricmp(key, "speedBlur"))            g_cfg.speedBlur = (float)atof(val);
        else if (!_stricmp(key, "cameraShake"))          g_cfg.cameraShake = (float)atof(val);
        else if (!_stricmp(key, "speedBlurFrom"))        g_cfg.speedBlurFrom = atoi(val);
        else if (!_stricmp(key, "cameraShakeFrom"))      g_cfg.cameraShakeFrom = atoi(val);
        else if (!_stricmp(key, "lowCamLoose"))          g_cfg.lowCamLoose = (float)atof(val);
        else if (!_stricmp(key, "lowCamSpeedFov"))       g_cfg.lowCamSpeedFov = (float)atof(val);
        else if (!_stricmp(key, "speedEffectsFull"))     g_cfg.speedEffectsFull = atoi(val);
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
