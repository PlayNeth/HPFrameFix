#include "common.h"
#include "portable.h"
#include "config.h"
#include "log.h"
#include "helpers.h"
#include "nightlamps.h"

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

void InstallPortableRegistry() {
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
