#include <chrono>
#include <cstdio>

#include "synth.hpp"

using namespace ais;

int main() {
    int fails = 0;
    double bpms[] = {100, 128, 150, 174};
    double offs[] = {0.0, 0.21};
    for (double bpm : bpms) {
        for (double off : offs) {
            auto T = ais_test::makeTrack(bpm, off, 120.0);
            auto t0 = std::chrono::steady_clock::now();
            Features F = analyse(T.pcm, 0);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            double beat = 60.0 / bpm;
            // beat-phase error: distance between detected grid and true kick grid
            double ph = std::fmod(F.beatOffset - off, beat);
            if (ph > beat / 2) ph -= beat;
            if (ph < -beat / 2) ph += beat;
            // onset timing error of bass onsets vs nearest true kick
            double errSum = 0;
            int errN = 0;
            for (auto const& o : F.onsets) {
                if (o.band != 0 || o.strength < 0.4f) continue;
                double best = 1e9;
                for (double k : T.kicks) best = std::min(best, std::fabs(k - o.t));
                if (best < 0.08) {
                    // signed
                    double sb = 1e9;
                    for (double k : T.kicks)
                        if (std::fabs(k - o.t) < std::fabs(sb)) sb = o.t - k;
                    errSum += sb;
                    ++errN;
                }
            }
            double bpmErr = std::fabs(F.bpm - bpm) / bpm * 100.0;
            std::printf("bpm %5.0f off %.2f -> est %.2f (%.2f%%) phaseErr %+.0f ms onsetBias %+.1f ms (n=%d) conf %.2f onsets %zu secs %zu  [%.0f ms]\n", bpm,
                        off, F.bpm, bpmErr, ph * 1000, errN ? errSum / errN * 1000 : 0.0, errN, F.beatConf, F.onsets.size(), F.sections.size(), ms);
            if (bpmErr > 1.0) {
                std::printf("   FAIL tempo\n");
                ++fails;
            }
            if (std::fabs(ph) > 0.030) {
                std::printf("   FAIL phase\n");
                ++fails;
            }
            if (bpm == 128 && off == 0.0) {
                for (auto const& s : F.sections) std::printf("     %6.1f - %6.1f  %-10s energy %.2f loud %.2f rise %+.2f\n", s.t0, s.t1, secName(s.kind), s.energy, s.loudness, s.rise);
                std::printf("     centroid %.2f dynamics %.2f percussive %.2f  bass/mid/high onsets %d/%d/%d\n", F.centroid, F.dynamics, F.percussive, F.bassOnsets,
                            F.midOnsets, F.highOnsets);
            }
        }
    }
    std::printf(fails ? "AUDIO TEST: %d FAILURES\n" : "AUDIO TEST: OK\n", fails);
    return fails ? 1 : 0;
}
