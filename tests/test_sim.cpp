#include <cstdio>

#include "../src/core/Patterns.hpp"
#include "../src/core/Sim.hpp"

using namespace ais;

int main() {
    int fails = 0;
    std::printf("cube jump: air time %.3f s, apex %.1f units, length @1x %.1f units\n", kAirSec, kJumpV * kJumpV / (2 * kGrav), kSpeed[1] * kAirSec);

    // flat ground must be passable, a lone spike must be passable, a wall of 2 blocks must not.
    {
        SimWorld w;
        w.sortAll();
        auto r = runCube(w, 0, 3000, 1);
        std::printf("empty floor: %s\n", r.ok ? "ok" : "FAIL");
        fails += !r.ok;
    }
    {
        SimWorld w;
        Obj o;
        o.id = ID_SPIKE;
        o.x = 1000;
        o.y = float(kGroundY);
        w.add(o);
        w.sortAll();
        auto r = runCube(w, 0, 2000, 1);
        std::printf("single spike: %s\n", r.ok ? "ok" : "FAIL");
        fails += !r.ok;
    }
    {
        SimWorld w;
        for (int row = 0; row < 3; ++row) {
            Obj o;
            o.id = ID_BLOCK;
            o.x = 1000;
            o.y = float(kGroundY + 30 * row);
            w.add(o);
        }
        w.sortAll();
        auto r = runCube(w, 0, 2000, 1);
        std::printf("3-high wall (must be impassable): %s\n", r.ok ? "FAIL (passed)" : "blocked, ok");
        fails += r.ok;
    }

    std::printf("\nclick-window (frames) per pattern and speed  [0.5x 1x 2x 3x 4x]\n");
    for (int p = 0; p < patCount(); ++p) {
        std::printf("%-14s tier %d :", patDef(p).name, patDef(p).tier);
        for (int s = 0; s < 5; ++s) std::printf(" %3d", patWindow(p, s));
        std::printf("\n");
    }

    // pads: a pad + spikes must be passable at every speed with NO click at all
    for (int p = 0; p < patCount(); ++p) {
        if (patDef(p).kind != PK_PAD) continue;
        for (int s = 0; s < 5; ++s) {
            SimWorld w;
            std::vector<Obj> objs;
            emitPattern(p, 1500, s, objs);
            for (auto const& o : objs) w.add(o);
            w.sortAll();
            auto r = runCube(w, 900, 2200, s);
            if (!r.ok) {
                std::printf("pad pattern %s unreachable at speed %d (failX %.0f)\n", patDef(p).name, s, r.failX);
                ++fails;
            }
        }
    }
    std::printf(fails ? "SIM TEST: %d FAILURES\n" : "SIM TEST: OK\n", fails);
    return fails ? 1 : 0;
}
