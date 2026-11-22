#include "common.h"
#include "paths.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "helpers.h"

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

extern "C" void InstallReadStub();
asm(".text\n"
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

void InstallInstallPaths() {
    if (!g_knownExe || (!g_cfg.portableInstallPaths && !g_cfg.installWinFallback)) {
        Logf("install.win: as written\n"); return;
    }
    bool ok = PatchRel32(hp::callReadInstall, 0xE8, (void*)&InstallReadStub);
    if (!ok) { Logf("install.win: patch FAILED\n"); return; }
    Logf("install.win:%s%s\n",
         g_cfg.portableInstallPaths ? " drive-letter folders read from the game folder when present;" : "",
         g_cfg.installWinFallback ? " built-in full install if the file is missing" : "");
}
