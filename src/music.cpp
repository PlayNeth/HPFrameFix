#include "common.h"
#include "music.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

// ---------------------------------------------------------------------------
// Interactive music maps. Each track's music (gamedata\audio\pc\<track>rock /
// tech) is a .mus of short pieces, a bar or so each, and a .map (PathFinder,
// "PFDx") of which piece plays after which: a 12-byte header (+5 the first
// piece, +6 how many, +7 events, +11 sections), then a node of 28 bytes per
// piece (+0 its section, +1 how many rules, then rules of 3 bytes: from, to,
// next piece - the next one for a control value 0..100 in from..to), then the
// events and each piece's place in the .mus. The game loads it to [0x570A50]
// (0x41072E, and 0x410908 for the race's copy of the music). The techno songs
// come in parts of three sections each, for low, middle and high intensity;
// in ATLATECH.MAP (Atlantica, and Aquatica) the first part's middle section
// is only two pieces, 32 and 33, whose middle rules point at each other, and
// every piece of the first part goes there at middle intensity: those two
// bars played over and over for as long as it lasted (the other songs' middle
// sections run 6 to 16 bars). Piece 33's middle rule now goes on to 50, the
// next part's middle section (8 bars), where the map's own events go too.
// Only that file: checked by its header, size and that node, byte for byte.
// ---------------------------------------------------------------------------
static void MusicMapFix(BYTE* p) {
    if (!p) return;
    static const BYTE kHeader[12] = { 'P', 'F', 'D', 'x', 0, 185, 188, 16, 0, 0, 0, 27 };
    static const BYTE kNode33[13] = { 3, 3, 0xFF, 0xFF, 0, 28, 25, 29, 72, 32, 73, 100, 2 };
    const int kSize = 6460, kAt = 12 + 28 * 33;
    if (Rd<int>(0x5643F8) != kSize || memcmp(p, kHeader, 12) || memcmp(p + kAt, kNode33, 13)) return;
    p[kAt + 9] = 50;
    static bool logged = false;
    if (!logged) { logged = true; LogfAsync("music: ATLATECH.MAP - after its two middle bars the music goes on to the next part\n"); }
}
extern "C" void __cdecl MusicMapLoaded(BYTE* p) { MusicMapFix(p); }
extern "C" { DWORD g_fnLoadFileMm = 0x4E0EC0; }
extern "C" void MusicMapStub();
__asm__(
    // a music map loaded (eax = its buffer, 0 if it failed)
    ".globl _MusicMapStub\n"
    "_MusicMapStub:\n"
    "    call *_g_fnLoadFileMm\n"
    "    pushal\n"
    "    pushl %eax\n"
    "    call _MusicMapLoaded\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    ret\n"
);
void InstallMusicMapFix() {
    if (!g_knownExe || !g_cfg.fixMusicMaps) return;
    bool ok = PatchRel32(0x41072E, 0xE8, (void*)&MusicMapStub) && PatchRel32(0x410908, 0xE8, (void*)&MusicMapStub);
    Logf("music: Atlantica's techno no longer stuck on two bars: %s\n", ok ? "on" : "patch FAILED");
}
