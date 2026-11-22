// ---------------------------------------------------------------------------
// Game addresses (nfs3.exe)
// ---------------------------------------------------------------------------
#pragma once
#include "common.h"
#include "nightlamps.h"

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
        { 0x41072E, "\xE8\x8D\x07\x0D\x00", 5 },        // music: the .map loaded (call 0x4E0EC0)
        { 0x410908, "\xE8\xB3\x05\x0D\x00", 5 },        // music: the .map loaded, the race's copy
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
        { 0x419C3A, "\xE8\x81\x72\x0C\x00", 5 },        // track: .frd loaded (call 0x4E0EC0)
        { 0x41DD24, "\xE8\xB7\x36\x01\x00", 5 },        // track: .col copied (call 0x4313E0)
        { 0x41E322, "\xE8\x99\x2B\x0C\x00", 5 },        // track: simhts.dat loaded (call 0x4E0EC0)
        { 0x41E1C5, "\xE8\x16\x2E\x0C\x00", 5 },        // track: .ccm loaded (call 0x4E0FE0)
        { 0x40E035, "\xE8\x86\x2E\x0D\x00", 5 },        // track: the AI's speeds loaded (call 0x4E0EC0)
        { 0x41D8A5, "\x8B\x15\xAC\xD4\x6F\x00", 6 },  // track: its number, in its files' paths (mov edx, [0x6FD4AC])
        { 0x41A108, "\xE8\x43\xF6\xFF\xFF", 5 },        // track: darkened for the night as it loads (call 0x419750)
        { 0x55E3E8, "\xFF\xFF\xFF\xB9\x00\x00\x00\x00\x00\x00\x90\x40", 12 },  // lamp glows: type 1's colour, blink, size
        { 0x53C8B4, "\x00\x00\x00\x3D", 4 },        // 0.03125
        { 0x4BC3F2, "\xE8\x09\x67\xFC\xFF", 5 },        // main view: call the HUD 0x482B00
        { 0x4DBEE0, "\x5D\x5E\x5A\x59\x5B\xC3", 6 },  // ViewSetup: its tail (pop ebp..ebx / ret)
        { 0x482CA3, "\xE8\x98\x65\x00\x00\x83\x3D\x10\x3D\x7A\x00\x00", 12 },
        { 0x42D267, "\xC7\x80\xC8\x06\x00\x00\x00\x00\x20\x41\xC7\x80\xCC\x06\x00\x00\x00\x00\x20\x42"
                    "\xC7\x80\xD0\x06\x00\x00\xB0\xB0\xB0\xB0", 30 },  // player's projected light: 10 m, 40 m, colour
        { 0x42D2DA, "\xC7\x80\xC8\x06\x00\x00\x00\x00\xA0\x41\xC7\x80\xCC\x06\x00\x00\x00\x00\x70\x42"
                    "\xC7\x80\xD0\x06\x00\x00\xE0\xE0\xE0\xE0", 30 },  // other cars': 20 m, 60 m, colour
        { 0x41A603, "\xD9\x5D\xEC\x8B\x45\xEC\xA9\xFF\xFF\xFF\x7F", 11 },  // light space z stored, then tested
        { 0x4264A6, "\x89\xC8\xE8\xF3\xDE\xFF\xFF", 7 },  // camera update, types 4 and 6 (low): call 0x4243A0
        { 0x422F67, "\xC7\x80\x10\x01\x00\x00\x00\x00\x00\x00", 10 },  // type 4 setup: cam+0x110 = 0  // HUD: cockpit picture, then cmp [0x7A3D10],0
        { 0x412EB5, "\xD8\x99\xF4\x04\x00\x00", 6 },  // speedometer: fcomp [ecx+0x4F4] (car speed, m/s)
        { 0x4BF2A0, "\xD9\x1D\xA4\x00\x56\x00\xD9\x1D\xA8\x00\x56\x00", 12 },  // projection centre x, y
    };
}
