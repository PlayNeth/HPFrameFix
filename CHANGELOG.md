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

## 0.1.6-plus

The first build of the plus branch: 0.1.6, plus gameplay changes.

- **No speed lost over jumps** (`airDrag`, `[Gameplay]` section). While a car is in the air the game slows it down with a drag of its own, so every jump cost a lot of speed. With `airDrag=0`, the default, every car (yours, the other racers, the police) keeps its speed through the air. `1` is the game's drag; values in between keep part of it.
- **Speed effects** (`[Speed effects]` section). Both follow the speed of the car the camera follows, starting at `speedBlurFrom` and `cameraShakeFrom` (both 90 km/h), and reaching full strength at `speedEffectsFull` (320 km/h).
  - **Speed blur** (`speedBlur`, Direct3D renderer): the 3D picture is blurred outwards towards the screen's edges, while the middle stays sharp. The HUD and the rear-view mirror are drawn after it and stay sharp; in the cockpit view the dashboard is blurred along with the road. Off in split screen.
  - **Camera shake** (`cameraShake`, every renderer): the 3D view shakes by a few pixels. It stands still while the game is paused.
- **Projected headlights** (section `[Graphics]`, with the game's projected headlight option at night). The game's beam was a narrow, bright spot that faded out within 40 m. It now reaches 1.5 times as far (`projLightReach`), is 1.3 times as wide (`projLightWidth`), and is a quarter less bright (`projLightBrightness`). Other cars' headlights change the same way.
- **A looser low camera** (`[Cameras]` section). The game places the low chase camera against the car with exactly the car's rotation, so every bump and twitch goes straight into the picture.
  - **`lowCamLoose`:** the camera now follows the car's turns, pitch and roll with a lag, held within a few degrees: it swings round the car in turns and rides out bumps, and its height trails the car's a little. `2` is looser still, `0` is the game's rigid camera.
  - **`lowCamSpeedFov`:** the low camera's view widens with speed, by up to 8 degrees at `speedEffectsFull`. It needs `wsFixFOV`.

## 0.1.6

- **Arrow keys in the in-race menus.** With `wsPauseMenu=1` on a wide screen, moving along the bottom row with the arrow keys left the pointer between the middle button and the side ones, over nothing. It now lands on the button the game moved to.
- **Menu text fades keep their speed** (`menuAnimations`). The text fading in and out in the menus took one step per frame, so above 64 fps it ran too fast. It now takes the same time at any frame rate.
- **No dark rectangle at the right of the sky on a wide screen.** The game draws the horizon as a strip in front of the camera that slides along as the camera turns, and on a wider screen its right end sometimes stopped short of the screen's edge. At night and on snow tracks, where the horizon glows over the scenery, a dark rectangle showed there. With the Hor+ view (`wsFixFOV`) the strip now continues past both edges.
- **No stretched picture at the start and finish of a race.** The cameras that show the car before the start and after the finish set their own zoom, and on a wide screen their picture came out stretched sideways. With `wsFixFOV` they now keep the right proportions.

## 0.1.5

- **HD cars with the software renderer.** `maxTextureSize` was meant to stay within what the renderer can draw, but that check never worked, so 512 textures were loaded with every renderer and the software renderer drew HD cars as stripes. Textures above 256 are now loaded with the Direct3D renderer only; the others keep the game's 256.
- **Pointer with `wsPauseMenu=2`.** The pointer in the in-race menus was held inside the 4:3 middle block. It now reaches the whole screen, as with `wsPauseMenu=1`.

## 0.1.4

- **HUD no longer squeezed by the custom-position screen.** With `wsHud` on a wide screen, the game's "Heads Up Display" screen took the narrowed boxes back into the HUD layout, where they were narrowed again on every visit, and the layout was saved: gauges in boxes too narrow, dials stretched tall, on 4:3 screens as well. The positions are now taken back through the same rule first, so a box ends up exactly where it was dragged. A layout that was already damaged has to be reset once: open that screen, press **Reset**, then **Done**.
- **The custom-position screen covers the whole screen.** The HUD pieces to drag are shown where they really are, not inside the 4:3 middle block, and the pointer lines up with them.
- **In-race menu text at its 4:3 width.** With `wsPauseMenu` and `wsHud` on, the pause menu's letters were narrowed twice on a wide screen (to 0.75 of their width at 16:9). Text and buttons now come out as on a 4:3 screen.

## 0.1.3

- **Sharp dashboard with `alternatePixelCenter`.** In the cockpit view the dashboard picture (pieces, needles, wheel) is no longer moved by the half pixel. The game already gives its pieces texture coordinates moved by half a texel, so the extra move blurred it. This also works with `widescreen=0` and `d3dFog=0`.

## 0.1.2

- **Menu animations keep their speed** at any frame rate (`menuAnimations`, on by default). The logos drifting behind the menus, the cross-fade of the menu frame's background, and the spark running along the arc to the ball at the top left took one step per frame, so above 64 fps they ran too fast. They now take 64 steps a second, as under the game's own cap.
- **A process pinned to one core after the mod has loaded** (a wrapper's or launcher's single-core option) gets every core back, with the game's threads kept on that one core (`widenAffinity`). Before, `widenAffinity` only acted when the mod loaded.

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
