#pragma once
// Tiny deterministic RNG (xorshift64*). Same seed -> same level, on every platform.
#include <cstdint>

namespace ais {

struct Rng {
    uint64_t s;

    explicit Rng(uint64_t seed = 88172645463325252ull) : s(seed ? seed : 88172645463325252ull) {
        for (int i = 0; i < 4; ++i) next();
    }
    uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ull;
    }
    double uni() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
    double uni(double a, double b) { return a + (b - a) * uni(); }
    int range(int lo, int hi) {  // inclusive
        if (hi <= lo) return lo;
        return lo + int(uni() * double(hi - lo + 1));
    }
    bool chance(double p) { return uni() < p; }
};

inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

}  // namespace ais
