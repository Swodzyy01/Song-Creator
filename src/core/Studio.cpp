#include "Studio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "Decor.hpp"
#include "Gen.hpp"
#include "LevelString.hpp"
#include "Rng.hpp"

namespace ais {

namespace {

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
std::string fmt(char const* f, ...) {
    char buf[400];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

uint64_t hashStr(std::string const& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

}  // namespace

RunResult runStudio(PCM const* pcm, double songSeconds, Settings S, std::string const& title, RunHooks const& H) {
    RunResult R;
    auto log = [&](std::string const& s) {
        if (H.log) H.log(s);
    };
    auto stage = [&](float f, std::string const& name) {
        if (H.stage) H.stage(f, name);
    };
    auto cancelled = [&] { return H.cancelled && H.cancelled(); };
    auto fail = [&](std::string const& why) {
        R.ok = false;
        R.error = why;
        return R;
    };

    double length = std::max(15.0, std::min(S.lengthSec, std::max(15.0, songSeconds)));

    // ---- 1. listen ---------------------------------------------------------------------------------
    stage(0.02f, "Listening to the song");
    Features F;
    bool haveAudio = pcm && pcm->mono.size() > size_t(pcm->sampleRate) * 6;
    if (haveAudio) {
        log(fmt("listening to %.0f s of audio (%.0f Hz mono)", std::min(length, pcm->duration()), double(pcm->sampleRate)));
        F = analyse(*pcm, length + 1.0, [&](float p) { stage(0.02f + 0.26f * p, "Listening to the song"); }, H.cancelled);
        if (cancelled()) return fail("cancelled");
        length = std::min(length, std::max(15.0, F.duration - 0.2));
    } else {
        log("no audio samples available -> falling back to a generic 120 BPM grid (obstacles will not follow the real music)");
        F = syntheticFeatures(length, 120.0);
    }

    // ---- 2. understand ------------------------------------------------------------------------------
    stage(0.30f, "Understanding the structure");
    if (F.analysed) {
        log(fmt("tempo %.1f BPM (confidence %.0f%%), first beat at %.2f s", F.bpm, double(F.beatConf) * 100.0, F.beatOffset));
        log(fmt("%.0f onsets found (bass %.0f, mid %.0f, high %.0f)", double(F.onsets.size()), double(F.bassOnsets), double(F.midOnsets), double(F.highOnsets)));
        log(fmt("character: brightness %.2f, dynamic range %.2f, percussiveness %.2f", double(F.centroid), double(F.dynamics), double(F.percussive)));
    }
    for (size_t i = 0; i < F.sections.size() && F.sections[i].t0 < length; ++i) {
        auto const& s = F.sections[i];
        log(fmt("  section %.0f: ", double(i + 1)) + mmss(s.t0) + "-" + mmss(std::min(s.t1, length)) + "  " + secName(s.kind) + fmt("  (energy %.2f)", double(s.energy)));
    }
    if (cancelled()) return fail("cancelled");

    // ---- 3. choose the approach ----------------------------------------------------------------------
    stage(0.38f, "Choosing the approach");
    if (S.frameperfect && !frameperfectAllowed(S)) {
        log("frame perfect needs Extreme Demon Top 250 or harder -> switched off");
        S.frameperfect = false;
    }
    Profile P = makeProfile(S);
    log("target difficulty: " + P.name + fmt("  (d=%.2f)", P.d));
    log(fmt("reference profile: ~%.1f obstacles/s at full intensity, narrowest click window allowed %.0f frame(s), pattern tier <= %.0f", P.aps, double(P.minWindow), double(P.tierCap)));
    {
        static char const* spd[5] = {"0.5x", "1x", "2x", "3x", "4x"};
        log(std::string("speeds ") + spd[P.speedMin] + " .. " + spd[P.speedMax] + fmt(", corridor openings from %.1f rows (ship) / %.1f (ufo) / %.1f (wave)", P.shipGap, P.ufoGap, P.waveGap));
    }
    if (P.fp) log(fmt("frame perfect ON: %.0f%% of obstacles are taken from the narrowest windows the simulator found", P.fpChance * 100.0));
    log("plan: obstacles sit on the kicks (bass hits get priority), corridors turn on the beat grid, every section gets its own mode, speed and palette");
    uint64_t seed = S.seed ? S.seed : mix64(hashStr(title) ^ (uint64_t(S.difficulty) << 7) ^ (uint64_t(S.rank) << 13) ^ uint64_t(F.bpm * 1000.0));
    log(fmt("seed %.0f", double(seed % 1000000007ull)));
    if (cancelled()) return fail("cancelled");

    // ---- 4. compose + simulate -----------------------------------------------------------------------
    stage(0.42f, "Composing and testing variants");
    Hooks gh;
    gh.log = H.log;
    gh.cancelled = H.cancelled;
    gh.progress = [&](float p) { stage(0.42f + 0.40f * p, "Composing and testing variants"); };
    log("planning segments:");
    Level L = generateLevel(F, P, S, length, seed, gh);
    if (cancelled()) return fail("cancelled");
    if (L.segs.empty()) return fail("could not plan the level");

    // ---- 5. verify -------------------------------------------------------------------------------------
    stage(0.84f, "Verifying with the physics simulation");
    log(fmt("verification: %.0f cube obstacles simulated frame by frame, %.0f repairs, %.0f/%.0f flight corridors flyable", double(L.totalEvents), double(L.repairs),
            double(L.flightOk), double(L.flightSegs)));
    if (L.totalEvents > 0 && L.minWindowUsed < 99) log(fmt("narrowest click window in the level: %.0f frame(s), %.0f obstacles at 4 frames or less", double(L.minWindowUsed), double(L.tightEvents)));
    if (L.flightSegs && L.flightOk < L.flightSegs) log("warning: some corridors could not be proven flyable - check them in the editor");

    // ---- 6. decorate -----------------------------------------------------------------------------------
    stage(0.90f, "Decorating");
    Palette pal;
    DecorStats ds = decorate(L, F, S, pal, seed);
    log(std::string("decoration: ") + decorName(S.decor) + fmt(" - %.0f colour triggers, %.0f beat pulses, %.0f section changes, %.0f hanging bars", double(ds.triggers),
                                                                double(ds.pulses), double(ds.sectionChanges), double(ds.bars)));

    // ---- 7. pack ---------------------------------------------------------------------------------------
    stage(0.95f, "Packing the level");
    std::string raw = buildLevelString(L, pal);
    R.packed = gzipBase64(raw);
    R.objects = int(L.objs.size());
    R.triggers = ds.triggers;
    R.seconds = length;
    R.bpm = F.bpm;
    double tm[4] = {0, 0, 0, 0};
    for (auto const& s : L.segs) tm[int(s.mode)] += s.t1 - s.t0;
    R.summary = fmt("%.0f objects, %.0f%% cube / %.0f%% ship / %.0f%% ufo / %.0f%% wave", double(R.objects), tm[0] / length * 100, tm[1] / length * 100, tm[2] / length * 100,
                    tm[3] / length * 100);
    log("done: " + mmss(length) + " level, " + R.summary);
    stage(1.0f, "Done");
    R.ok = true;
    return R;
}

}  // namespace ais
