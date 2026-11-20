# HPFrameFix

An unlocked frame rate with smooth interpolation, seamless widescreen and a portable install for **Need for Speed III: Hot Pursuit** on PC.

The game simulates at a fixed 64 Hz and only draws a frame after each simulation step. HPFrameFix draws at your display's refresh rate and blends motion between steps, so the game is smooth at 60, 120, 144 Hz and higher. It also adds proper widescreen and Direct3D fog, fixes the sound and the movie freeze on today's PCs, and lets the game run from any folder without an installer or the CD. It's a single file, `HPFrameFix.asi`, and it doesn't touch the game's files on disk.

It shares its engine work with [HSFrameFix](https://github.com/PlayNeth/HSFrameFix), the same mod for Need for Speed: High Stakes.

---

## Contents

- [Requirements](#requirements)
- [Installing](#installing)
- [Uninstalling](#uninstalling)
- [What it changes](#what-it-changes)
- [Settings](#settings)
- [Files the mod creates](#files-the-mod-creates)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [Credits](#credits)
- [License](#license)

---

## Requirements

- **Need for Speed III: Hot Pursuit** for PC, with this `nfs3.exe` (SHA-1 `87872783b8d09a27ae5a1b2a7de4f69b7fbb4a39`). The 3dfx Banshee patch only replaces `voodoo2a.dll`, so it's fine. The `nfs3.exe` of VEG's Modern Patch isn't supported: the mod turns itself off there.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), installed as `dinput.dll`.
- One of the game's renderers:
  - **Direct3D** (`d3da.dll`), the default;
  - **Glide** (`voodoo2a.dll`), through [gl2d9](https://github.com/PlayNeth/gl2d9);
  - the **software renderer** (`softtria.dll`).

It runs on Windows, and on Linux through Wine or Proton.

## Installing

1. Copy Ultimate ASI Loader's DLL (`dinput8.dll` from its 32-bit download) into the game's folder and rename it to **`dinput.dll`**. On Linux, also set `dinput` to native, then builtin, for the game (`WINEDLLOVERRIDES="dinput=n,b"`, or in `winecfg`).
2. Copy **`HPFrameFix.asi`** into the game's folder, or into the loader's `scripts` folder.
3. Start the game.

On the first start the mod creates `HPFrameFix.ini`, with every setting explained, next to the `.asi`. To check it's working, set `logToFile=1` in the INI and look in `HPFrameFix.log`: its first line names the mod's version and settings.

The game no longer needs its installer, its registry entries or the CD: with the defaults it runs straight from a copied folder.

## Uninstalling

Delete `HPFrameFix.asi`, and the ASI loader if nothing else uses it. You can also delete `HPFrameFix.ini` and `HPFrameFix.log` (and `HPFrameFix_registry.ini`, left by 0.1.0).

---

## What it changes

Every feature has its own setting in `HPFrameFix.ini`.

### Frame rate and timing

- **Interpolation:** frames are drawn at the display's refresh rate, with cars and cameras blended between the game's 64 steps a second. Camera cuts and looking back are shown as clean cuts.
- **Precise timing:** the game's 128 Hz timer runs from a precise clock.
- **Frame limiter:** left to v-sync by default, otherwise the display's refresh rate, or a rate you set.
- **Menus:** no 64 fps cap; their animations stay at the right speed.
- **Faster loading:** the loading bar is redrawn at most every 100 ms instead of at every step.

### Widescreen

- **Hor+ view:** the 4:3 view keeps its height and shows more at the sides. Rain and snow keep their own width, the horizon reaches the screen's edges, and the cameras at the start and finish of a race keep their proportions.
- **Every resolution listed** in the options, 32-bit modes included. The game remembers the full resolution, not just its width.
- **HUD** in 4:3 proportions, with its panels at the screen's edges. The rear-view mirror fits its frame. The game's "Heads Up Display" screen, where the HUD's positions are changed, shows the boxes where they really are on the screen and keeps their shape.
- **In-race menus:** the text keeps its 4:3 width on a wide screen.
- **Cockpit view:** the dashboard picture keeps its shape, needles included.
- **Loading screen** in 4:3 in the middle.
- **Menus at the chosen resolution:** no switch to 640×480. The menus show in 4:3 in the middle (or stretched), with the intro and demo movies scaled with them.
- **In-race menus:** the frame covers the screen, while text and buttons keep their shape; the pointer reaches the whole screen.
- **Sharp 2D on Direct3D:** pictures, menus and text land exactly on the pixels, and enlarged menus are filtered without seams or edge lines.

### Graphics

- **Renderer choice** in the INI (Direct3D, software, Voodoo, Voodoo2), and an optional folder for the renderer DLLs.
- **Direct3D fog**, as the game has it on 3dfx cards.
- **Textures up to 512×512** with the Direct3D renderer, for HD texture and car mods.
- **Car detail:** cars keep their detailed models further away, and no car is dropped to lower detail by the game's budget.

### Sound

- **No crackling or popping:** the sound driver no longer wears its buffer down until it runs dry, and the game's threads share one core while the system's sound threads keep the others. A game started on a single core, or pinned to one later, gets its other cores back.

### Fixes

- **Movie freeze:** no hang on a black screen after a movie on multi-core CPUs.
- **Closing the window** ends the game.
- **Disk space check:** no "less than 1MB free" error on today's big disks.
- **Software renderer:** the standings list in a race gets its frame back, and resolutions it can't draw are left out of the list.

### Portable

- **Registry:** the game needs no registry entries. The renderer and display adapter come from `HPFrameFix.ini` (`renderer`, `d3dDevice`); anything else is read from the Windows registry if it's there.
- **Install paths:** folders a partial install puts on the CD's drive letter are read from the game's folder when they're there.
- **No `install.win`:** a full install is assumed instead of the "files are corrupted" error.
- **No CD check.**

## Settings

`HPFrameFix.ini` explains every setting next to it, grouped into sections. The most useful ones:

| Setting | Default | |
|---|---|---|
| `interpolate` | `1` | Smooth frames at the refresh rate; `0` = the game's 64 fps. |
| `targetFPS` | `-1` | `-1` = v-sync, else the refresh rate; `0` = no limit; `N` = N fps. |
| `interpDelay` | `1.0` | Raise to `1.5` if you see hitches. |
| `menuAnimations` | `1` | The menus' drifting logos, background fade, spark, glow and text fades keep their speed at any frame rate. |
| `renderer` | `d3d` | `d3d`, `software`, `voodoo`, `voodoo2`, a renderer DLL name, or empty for the game's own setting. |
| `d3dDevice` | `-1` | Direct3D's display adapter: `0` = primary, `1` = second; `-1` = the Windows registry's, else the primary. |
| `widescreen` | `1` | Master switch for every widescreen setting. |
| `wsMenus` | `1` | Menus at the chosen resolution: `1` = 4:3 in the middle, `2` = stretched, `0` = 640×480. |
| `wsPauseMenu` | `1` | In-race menus: `1` = frame over the screen, `2` = all in 4:3 (the pointer still reaches the whole screen), `0` = stretched. |
| `d3dFog` | `1` | Fog on Direct3D. |
| `maxTextureSize` | `512` | Largest texture the game loads. Above 256 with Direct3D only. |
| `fixSound` | `1` | Sound driver fix. |
| `soundLatencyMs` | `40` | Raise it if you hear dropouts. |
| `portableRegistry` | `1` | The game's registry settings come from the mod, so no registry entries are needed. |
| `exeCheck` | `1` | Turns the mod off if the exe differs; `0` patches anyway (only for a modified exe you know is the same version). |
| `logToFile` | `0` | `1` writes `HPFrameFix.log`. |

[CHANGELOG.md](CHANGELOG.md) has the version history.

## Files the mod creates

All next to the `.asi`:

| File | |
|---|---|
| `HPFrameFix.ini` | Settings. The mod also saves your race resolution there. |
| `HPFrameFix.log` | What the mod did, with `logToFile=1`; send it with any bug report. |

## Troubleshooting

- **Nothing changed:** the ASI loader isn't loading. Check its instructions for this game, and that `HPFrameFix.asi` is in the game's folder or the loader's `scripts` folder. With `logToFile=1`, a missing `HPFrameFix.log` confirms it.
- **The log says the exe differs and the mod is off:** your `nfs3.exe` isn't the one under Requirements. Check its SHA-1. The mod leaves the game alone to be safe.
- **HUD gauges squeezed, dials stretched tall (also at 4:3):** the HUD layout was damaged by opening the game's "Heads Up Display" screen on a wide screen with a version before 0.1.4. It's saved with your settings. Open that screen once, press **Reset**, then **Done**.
- **Occasional hitches:** raise `interpDelay` to `1.5`.
- **Tearing, or the frame rate runs too high:** turn on v-sync, or set `targetFPS` to your refresh rate.
- **Dropouts in the sound:** raise `soundLatencyMs`.
- **Menus stay 640×480 with the software renderer:** expected. `wsMenusSoft=1` is an experimental way around it.
- **Something feels off:** most features can be switched off in the INI one at a time. Please report bugs with your `HPFrameFix.log`.

## Building from source

The source is a single file, `HPFrameFix.cpp`, built with MinGW-w64 (GCC 13), 32-bit:

```
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -shared -s -static-libgcc -static-libstdc++ \
  -o HPFrameFix.asi HPFrameFix.cpp -lwinmm -luser32
```

## Credits

- **SilentPatch** (CookiePLMonster's SilentPatchNFS90s), for the idea of keeping the game's threads on one core.
- **VEG**, whose Modern Patch for Need for Speed III helped locate several of the addresses HPFrameFix patches. No code from it is used.

## License

GPL-3.0. See [LICENSE](LICENSE). Need for Speed III: Hot Pursuit is © Electronic Arts; this mod contains no game files.
