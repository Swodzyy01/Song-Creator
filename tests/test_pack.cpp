#include <cstdio>
#include <fstream>

#include "../src/core/Decor.hpp"
#include "../src/core/LevelString.hpp"
#include "synth.hpp"

using namespace ais;

static void dump(char const* path, std::string const& s) {
    std::ofstream f(path, std::ios::binary);
    f << s;
}

int main() {
    // edge cases for the deflate writer
    dump("/tmp/edge_0.raw", "");
    dump("/tmp/edge_0.b64", gzipBase64(""));
    dump("/tmp/edge_1.raw", "a");
    dump("/tmp/edge_1.b64", gzipBase64("a"));
    std::string rep(1000, 'z');
    dump("/tmp/edge_2.raw", rep);
    dump("/tmp/edge_2.b64", gzipBase64(rep));
    ais::Rng rng(5);
    std::string rnd;
    for (int i = 0; i < 70000; ++i) rnd.push_back(char(rng.range(0, 255)));
    dump("/tmp/edge_3.raw", rnd);
    dump("/tmp/edge_3.b64", gzipBase64(rnd));

    // a real level
    auto T = ais_test::makeTrack(128, 0.0, 130.0);
    Features F = analyse(T.pcm, 0);
    Settings S;
    S.difficulty = ExtremeDemon;
    S.rank = 35;
    S.frameperfect = true;
    S.decor = 0;
    S.depth = 3;
    Profile P = makeProfile(S);
    Hooks H;
    Level L = generateLevel(F, P, S, 120.0, 99, H);
    Palette pal;
    DecorStats ds = decorate(L, F, S, pal, 99);
    std::string raw = buildLevelString(L, pal);
    std::string packed = gzipBase64(raw);
    dump("/tmp/level.raw", raw);
    dump("/tmp/level.b64", packed);
    std::printf("objects %zu, triggers %d, pulses %d, bars %d, raw %zu bytes -> packed %zu bytes (%.1fx)\n", L.objs.size(), ds.triggers, ds.pulses, ds.bars, raw.size(),
                packed.size(), double(raw.size()) / double(packed.size()));
    std::printf("header: %.200s ...\n", raw.c_str());
    return 0;
}
