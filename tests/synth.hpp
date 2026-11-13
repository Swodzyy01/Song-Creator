#pragma once
// Builds fake "songs" with a known tempo / known kick times so the analyser can be verified.
#include <cmath>
#include <cstdint>
#include <vector>

#include "../src/core/Audio.hpp"
#include "../src/core/Rng.hpp"

namespace ais_test {

struct Track {
    ais::PCM pcm;
    double bpm = 120, offset = 0, seconds = 0;
    std::vector<double> kicks;
};

inline Track makeTrack(double bpm, double offset, double seconds, int sr = 44100, uint64_t seed = 7) {
    Track T;
    T.bpm = bpm;
    T.offset = offset;
    T.seconds = seconds;
    T.pcm.sampleRate = sr;
    size_t n = size_t(seconds * sr);
    T.pcm.mono.assign(n, 0.f);
    ais::Rng rng(seed);
    double beat = 60.0 / bpm;
    auto at = [&](size_t i) -> float& { return T.pcm.mono[i]; };

    struct Sec {
        double a, b;
        bool kick, snare, hats, bass, pad;
        float gain;
        bool ramp;
    };
    Sec S[] = {
        {0.00, 0.12, true, false, false, false, false, 0.35f, false},   // intro
        {0.12, 0.30, true, true, true, false, false, 0.60f, true},     // build (ramps up)
        {0.30, 0.55, true, true, true, true, false, 1.00f, false},      // drop
        {0.55, 0.68, false, false, false, false, true, 0.30f, false},   // breakdown
        {0.68, 0.92, true, true, true, true, false, 1.00f, false},      // drop 2
        {0.92, 1.00, true, false, true, false, false, 0.40f, false},    // outro
    };
    auto secAt = [&](double t) -> Sec const& {
        double f = t / seconds;
        for (auto const& s : S)
            if (f >= s.a && f < s.b) return s;
        return S[5];
    };
    const double twoPi = 6.283185307179586;
    int bi = 0;
    for (double t = offset; t < seconds - 0.3; t += beat, ++bi) {
        Sec s = secAt(t);
        if (s.ramp) s.gain *= float(0.5 + 1.0 * ((t / seconds - s.a) / (s.b - s.a)));
        if (s.kick) {
            T.kicks.push_back(t);
            size_t i0 = size_t(t * sr);
            for (size_t k = 0; k < size_t(0.30 * sr) && i0 + k < n; ++k) {
                double tt = double(k) / sr;
                double f = 50.0 + 100.0 * std::exp(-tt / 0.03);
                static double ph = 0;
                ph += twoPi * f / sr;
                at(i0 + k) += float(0.9 * s.gain * std::exp(-tt / 0.10) * std::sin(ph));
            }
        }
        if (s.snare && (bi % 2 == 1)) {
            size_t i0 = size_t(t * sr);
            for (size_t k = 0; k < size_t(0.18 * sr) && i0 + k < n; ++k) {
                double tt = double(k) / sr;
                double noise = rng.uni(-1, 1);
                at(i0 + k) += float(0.5 * s.gain * std::exp(-tt / 0.05) * (0.7 * noise + 0.3 * std::sin(twoPi * 190 * tt)));
            }
        }
        if (s.hats) {
            for (int h = 0; h < 2; ++h) {
                size_t i0 = size_t((t + h * beat * 0.5) * sr);
                float prev = 0;
                for (size_t k = 0; k < size_t(0.05 * sr) && i0 + k < n; ++k) {
                    double tt = double(k) / sr;
                    float nz = float(rng.uni(-1, 1));
                    at(i0 + k) += float(0.22 * s.gain * std::exp(-tt / 0.012)) * (nz - prev);
                    prev = nz;
                }
            }
        }
    }
    // sustained layers
    for (size_t i = 0; i < n; ++i) {
        double t = double(i) / sr;
        Sec const& s = secAt(t);
        if (s.bass) {
            double bt = std::fmod(t - offset, beat * 2.0);
            at(i) += float(0.28 * std::sin(twoPi * 55.0 * t) * (0.6 + 0.4 * std::exp(-bt / 0.4)));
        }
        if (s.pad) at(i) += float(0.10 * (std::sin(twoPi * 220 * t) + std::sin(twoPi * 277.2 * t) + std::sin(twoPi * 329.6 * t)));
    }
    float mx = 0;
    for (float v : T.pcm.mono) mx = std::max(mx, std::fabs(v));
    if (mx > 0.95f)
        for (auto& v : T.pcm.mono) v *= 0.95f / mx;
    return T;
}

}  // namespace ais_test
