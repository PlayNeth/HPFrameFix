#include "common.h"
#include "stats.h"
#include "config.h"
#include "log.h"
#include "timing.h"
#include "interp.h"
#include "loading.h"
#include "sound.h"

// ---------------------------------------------------------------------------
// Stats thread
// ---------------------------------------------------------------------------
DWORD WINAPI StatsThread(LPVOID) {
    unsigned long ls = 0, lr = 0, le = 0, lf = 0, lb = 0, lrej = 0, lcut = 0, lmf = 0;
    for (;;) {
        int sec = g_cfg.logEverySeconds > 0 ? g_cfg.logEverySeconds : 10;
        Sleep(sec * 1000);
        LoadStatsTick();
        {
            static unsigned long lsc = 0;
            unsigned long sc = g_sndCalls;
            if (sc != lsc)
                Logf("last %ds: sound top-ups %.1f/s, longest gap %.1f ms (%ld over 15 ms), longest top-up %.1f ms, "
                     "%ld underrun(s), lead %.1f ms\n",
                     sec, (sc - lsc) / (double)sec, g_sndMaxGapUs.exchange(0) / 1000.0,
                     g_sndLateGaps.exchange(0), g_sndMaxRunUs.exchange(0) / 1000.0, g_sndUnderruns.exchange(0),
                     g_eacLead && g_eacRate && *g_eacRate > 0 ? *g_eacLead * 1000.0 / *g_eacRate : -1.0);
            lsc = sc;
        }
        unsigned long mf = g_menuFrames;
        if (mf != lmf) Logf("last %ds: menu frames %.1f/s, longest %.0f ms\n", sec, (mf - lmf) / (double)sec,
                            g_menuGapMaxUs.exchange(0) / 1000.0);
        lmf = mf;
        unsigned long s = g_steps, r = g_renders, e = g_extraRenders, f = g_tickFires,
                      b = g_blended, rj = g_rejected, c = g_cuts;
        if (s != ls || r != lr)
            Logf("last %ds: sim %.1f/s, frames %.1f/s (%.1f/s between steps), ticks %.1f/s, "
                 "blended %lu, not blended %lu, camera-cut frames %lu, avg blend %.2f, clamped %lu, "
                 "resets %lu, clock resyncs %lu, tick resyncs %lu\n",
                 sec, (s - ls) / (double)sec, (r - lr) / (double)sec, (e - le) / (double)sec,
                 (f - lf) / (double)sec, b - lb, rj - lrej, c - lcut,
                 g_alphaN ? g_alphaSum / g_alphaN : 0.0, (unsigned long)g_alphaClamped,
                 (unsigned long)g_resets, (unsigned long)g_clockSnaps, (unsigned long)g_tickResyncs);
        if (g_clampHave && s != ls)
            Logf("  clamps: first at %02d:%02d:%02d.%03d, frame up to %.1f ms past the newest step, %.1f ms before the oldest\n",
                 g_clampFirst.wHour, g_clampFirst.wMinute, g_clampFirst.wSecond, g_clampFirst.wMilliseconds,
                 (double)g_clampAheadMs, (double)g_clampBehindMs);
        g_clampHave = false; g_clampAheadMs = 0; g_clampBehindMs = 0;
        g_alphaSum = 0; g_alphaN = 0; g_alphaClamped = 0;
        ls = s; lr = r; le = e; lf = f; lb = b; lrej = rj; lcut = c;
    }
}
