# HPFrameFix changelog

HPFrameFix is an ASI plugin for **Need for Speed III: Hot Pursuit** (`nfs3.exe`):
- frame-rate unlock with interpolation;
- precise game timing;
- unlocked menus;
- faster loading;
- seamless widescreen, menus included;
- Direct3D fog;
- sound, movie and startup fixes;
- a portable install.

It works with the `nfs3.exe` whose SHA-1 is `87872783b8d09a27ae5a1b2a7de4f69b7fbb4a39`. On any other exe, the mod leaves the game alone.

---

## 0.1.1

- **No `HPFrameFix_registry.ini` any more.** The game's registry values are answered without a file of their own, as HSFrameFix does: the renderer from `renderer=`, the display adapter from the new `d3dDevice=` (`-1` = not set here), and any other value from the Windows registry if it's there. A file left by 0.1.0 is still read, ahead of the Windows registry, and can be deleted.
- With `renderer=` empty, a renderer set in the Windows registry is passed to the game as it is.

## 0.1.0: first release

### Frame rate and timing

- **Interpolation:** frames at the display's refresh rate, with cars and cameras blended between the game's 64 steps a second. Camera switches and looking back are drawn as clean cuts. The cockpit camera's head movement no longer shakes.
- **Precise timing:** the game's 128 Hz timer runs from a precise clock.
- **Frame limiter:** `targetFPS` (default: v-sync, else the refresh rate).
- **Menus:** no 64 fps cap (`unlockMenus`, `menuTargetFPS`, `menuMaxFPS`).
- **Faster loading:** `speedUpLoading`, `loadScreenRenderMs`.

### Widescreen

- **Hor+ 3D view** (`wsFixFOV`). Rain and snow keep the game's own width of view (`wsWeather`).
- **Every resolution listed**, 32-bit included (`wsUnlockResolutions`, `ws32bit`). The full race resolution is remembered in the INI (`wsRaceWidth`, `wsRaceHeight`), since the game saves only the width.
- **HUD** in 4:3 proportions at the screen's edges, with the rear-view mirror fitting its frame (`wsHud`).
- **Cockpit view:** the dashboard picture keeps its shape, needles included (`wsCockpit`).
- **Loading screen** in 4:3 in the middle (`wsLoading`).
- **Menus at the chosen resolution** (`wsMenus`, `wsMenuResolution`), with the intro and demo movies scaled with them (`wsMovies`).
- **In-race menus:** the frame over the whole screen, with text and buttons in 4:3 proportions and the pointer across the whole screen (`wsPauseMenu`).
- **Direct3D:**
  - every vertex half a pixel up and left, so 2D lands exactly on the pixels (`alternatePixelCenter`);
  - enlarged menus filtered (`wsMenuFilter`);
  - no edge lines beside pictures (`menuTexelInset`).

### Graphics

- **Renderer choice** in the INI (`renderer`), and a folder for the renderer DLLs (`driversFolder`).
- **Direct3D fog**, with Glide's fog curve (`d3dFog`).
- **Textures up to 512×512** (`maxTextureSize`).
- **Car detail:** `carLodScale`, `carDrawDistance`, `fullDetailCars`.

### Sound

- **Crackling and popping fixed** (`fixSound`, `soundLatencyMs`): the sound driver no longer wears its lead down until it runs dry.
- **Threads:**
  - the game's threads share one core (`gameThreadsOneCore`), while the system's sound threads keep the others;
  - a process started on one core gets all of them back (`widenAffinity`).

### Fixes

- **Movie freeze** on multi-core CPUs fixed (`fixMovieFreeze`).
- **Closing the window** ends the game (`closeExits`).
- **No false "less than 1MB free" error** on big disks (`noDiskSpaceCheck`).
- **Software renderer:**
  - the standings list keeps its frame (`softStandingsBox`);
  - only modes it can draw are listed;
  - experimental menus at the chosen resolution (`wsMenusSoft`, off by default).

### Portable

- **Registry settings** in `HPFrameFix_registry.ini` (`portableRegistry`).
- **Install paths:** drive-letter install paths read from the game's folder (`portableInstallPaths`).
- **No `install.win` needed:** a built-in full install is used when it's missing (`installWinFallback`, `language`).
- **No CD check** (`noCDCheck`).
