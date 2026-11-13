#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

#include "../src/core/Gen.hpp"
#include "synth.hpp"

using namespace ais;

static void ascii(Level const& L, size_t si, double x0, double x1) {
    int W = int((x1 - x0) / 15.0);
    std::vector<std::string> g(12, std::string(size_t(W), ' '));
    for (auto const& o : L.objs) {
        if (o.x < x0 || o.x >= x1) continue;
        int cx = int((o.x - x0) / 15.0);
        int row = int(std::lround((o.y - kGroundY) / kBlock));
        if (row < 0 || row > 10) continue;
        char ch = '?';
        if (o.id == ID_BLOCK) ch = '#';
        else if (o.id == ID_SPIKE) ch = (o.kv.find(",6,180") != std::string::npos) ? 'v' : '^';
        else if (o.id == ID_PAD_YELLOW) ch = '_';
        else ch = 'O';
        g[size_t(row)][size_t(std::min(W - 1, cx))] = ch;
        if (o.id == ID_BLOCK) g[size_t(row)][size_t(std::min(W - 1, cx + 1))] = '#';
    }
    std::printf("   segment %zu  x %.0f..%.0f  (1 char = 15 units, row 0 at the bottom)\n", si + 1, x0, x1);
    for (int r = 10; r >= 0; --r) std::printf("   |%s|\n", g[size_t(r)].c_str());
}

int main(int argc, char**) {
    bool show = argc > 1;
    auto T = ais_test::makeTrack(128, 0.0, 130.0);
    Features F = analyse(T.pcm, 0);
    int fails = 0;

    struct Case {
        int diff, rank;
        bool up, fp;
        char const* name;
    };
    Case cases[] = {
        {Easy, 0, false, false, "Easy"},
        {Normal, 0, false, false, "Normal"},
        {Hard, 0, false, false, "Hard"},
        {Harder, 0, false, false, "Harder"},
        {Insane, 0, false, false, "Insane"},
        {EasyDemon, 0, false, false, "Easy Demon"},
        {MediumDemon, 0, false, false, "Medium Demon"},
        {HardDemon, 0, false, false, "Hard Demon"},
        {InsaneDemon, 0, false, false, "Insane Demon"},
        {ExtremeDemon, 1200, false, false, "Extreme Top 1200"},
        {ExtremeDemon, 250, false, true, "Extreme Top 250 +FP"},
        {ExtremeDemon, 35, false, true, "Extreme Top 35 +FP"},
        {ExtremeDemon, 1, false, true, "Extreme Top 1 +FP"},
        {ExtremeDemon, 0, true, true, "Upcoming +FP"},
    };
    for (auto const& c : cases) {
        Settings S;
        S.difficulty = c.diff;
        S.rank = c.rank;
        S.upcoming = c.up;
        S.frameperfect = c.fp;
        S.depth = 3;
        Profile P = makeProfile(S);
        Hooks H;
        auto t0 = std::chrono::steady_clock::now();
        Level L = generateLevel(F, P, S, 120.0, 12345, H);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        // ---- independent re-verification of the assembled level --------------------------------------
        int bad = 0;
        std::set<std::tuple<int, int, int>> seen;
        int dup = 0;
        for (auto const& o : L.objs)
            if (!seen.insert({o.id, int(std::lround(o.x)), int(std::lround(o.y))}).second) ++dup;
        for (size_t si = 0; si < L.segs.size(); ++si) {
            auto const& sg = L.segs[si];
            auto const& R = L.res[si];
            if (sg.mode == Mode::Cube) {
                SimWorld w;
                for (auto const& o : L.objs)
                    if (o.x > R.xs - 400 && o.x < R.xe + 400) w.add(o);  // everything near the segment, whatever segment it came from
                w.sortAll();
                // flight corridors next to the cube segment would show up as blocks; only run where the stretch is the cube segment
                auto run = runCube(w, R.xs, R.xe, sg.speed);
                if (!run.ok) {
                    ++bad;
                    std::printf("   !! cube segment %zu fails at x=%.0f\n", si + 1, run.failX);
                }
                for (auto const& e : R.events)
                    if (e.x < L.X(sg.t0) || e.x > L.X(sg.t1)) {
                        ++bad;
                        std::printf("   !! event outside segment %zu\n", si + 1);
                    }
            } else if (!R.verified) {
                ++bad;
                std::printf("   !! flight segment %zu not verified\n", si + 1);
            }
        }
        std::printf("%-22s d=%.2f  %5zu objs  %3zu segs  events %3d (<=4f: %2d, min %2d f)  flight %d/%d  repairs %2d  dup %d  [%.0f ms]  %s\n", c.name, P.d, L.objs.size(), L.segs.size(),
                    L.totalEvents, L.tightEvents, L.minWindowUsed, L.flightOk, L.flightSegs, L.repairs, dup, ms, bad ? "FAIL" : "ok");
        fails += bad + dup;
        if (show && std::string(c.name) == "Insane") {
            for (size_t si = 0; si < L.segs.size() && si < 3; ++si) {
                auto const& sg = L.segs[si];
                double xa = L.X(sg.t0) + 600, xb = std::min(xa + 1800.0, L.X(sg.t1));
                if (xb > xa + 300) ascii(L, si, xa, xb);
            }
        }
    }
    std::printf(fails ? "GEN TEST: %d PROBLEMS\n" : "GEN TEST: OK\n", fails);
    return fails ? 1 : 0;
}
