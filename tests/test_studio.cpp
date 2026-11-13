#include <chrono>
#include <cstdio>

#include "../src/core/Studio.hpp"
#include "synth.hpp"

using namespace ais;

int main(int argc, char** argv) {
    int depth = argc > 1 ? std::atoi(argv[1]) : 4;
    auto T = ais_test::makeTrack(150, 0.12, 150.0);
    Settings S;
    S.difficulty = ExtremeDemon;
    S.rank = 35;
    S.frameperfect = true;
    S.decor = 3;
    S.lengthSec = 120;
    S.depth = depth;
    RunHooks H;
    H.log = [](std::string const& s) { std::printf("  | %s\n", s.c_str()); };
    std::string lastStage;
    H.stage = [&](float f, std::string const& name) {
        if (name != lastStage) {
            std::printf("[%3.0f%%] %s\n", double(f) * 100, name.c_str());
            lastStage = name;
        }
    };
    auto t0 = std::chrono::steady_clock::now();
    RunResult R = runStudio(&T.pcm, T.seconds, S, "Synthetic Song", H);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\nresult: %s  packed %zu chars  %s  (%.0f ms, depth %d)\n", R.ok ? "OK" : R.error.c_str(), R.packed.size(), R.summary.c_str(), ms, depth);

    // no audio -> fallback
    RunHooks H2;
    RunResult R2 = runStudio(nullptr, 90, S, "No audio", H2);
    std::printf("fallback without audio: %s (%d objects)\n", R2.ok ? "OK" : R2.error.c_str(), R2.objects);

    // cancellation
    RunHooks H3;
    int calls = 0;
    H3.cancelled = [&] { return ++calls > 6; };
    RunResult R3 = runStudio(&T.pcm, T.seconds, S, "Cancel", H3);
    std::printf("cancel: %s\n", R3.ok ? "NOT CANCELLED (bad)" : R3.error.c_str());
    return (R.ok && R2.ok && !R3.ok) ? 0 : 1;
}
