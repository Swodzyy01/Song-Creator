#include "Patterns.hpp"

#include <mutex>

namespace ais {

namespace {

// name, tier, n, kind, airSec, weight
const PatDef kPats[] = {
    {"single spike", 1, 1, PK_SPIKES, kAirSec, 1.00},
    {"double spike", 1, 2, PK_SPIKES, kAirSec, 0.80},
    {"triple spike", 2, 3, PK_SPIKES, kAirSec, 0.55},
    {"platform 2", 2, 2, PK_PLAT, kAirSec + 0.07, 0.30},
    {"platform 3", 1, 3, PK_PLAT, kAirSec + 0.07, 0.45},
    {"platform 4", 1, 4, PK_PLAT, kAirSec + 0.07, 0.45},
    {"platform 5", 1, 5, PK_PLAT, kAirSec + 0.07, 0.35},
    {"pad + spike", 3, 1, PK_PAD, kPadAirSec, 0.35},
    {"pad + double", 3, 2, PK_PAD, kPadAirSec, 0.35},
    {"pad + triple", 4, 3, PK_PAD, kPadAirSec, 0.30},
    {"tower 1x2", 5, 1, PK_TOWER, kAirSec, 0.50},
    {"tower 2x2", 5, 2, PK_TOWER, kAirSec, 0.50},
    {"quad spike", 5, 4, PK_SPIKES, kAirSec, 0.45},
    {"quint spike", 5, 5, PK_SPIKES, kAirSec, 0.35},
    {"six spikes", 5, 6, PK_SPIKES, kAirSec, 0.30},
    {"seven spikes", 5, 7, PK_SPIKES, kAirSec, 0.25},
};
constexpr int kCount = int(sizeof(kPats) / sizeof(kPats[0]));

double padArc(int speedIdx) { return kSpeed[speedIdx] / kFps * (2.0 * kPadV / kGrav); }

}  // namespace

int patCount() { return kCount; }
PatDef const& patDef(int i) { return kPats[i]; }

double patHalfW(int i, int sp) {
    PatDef const& d = kPats[i];
    if (d.kind == PK_PAD) return padArc(sp) * 0.5;
    return 15.0 * d.n;
}

void emitPattern(int i, double xc, int sp, std::vector<Obj>& out) {
    PatDef const& d = kPats[i];
    auto add = [&](int id, double x, double y) {
        Obj o;
        o.id = id;
        o.x = float(x);
        o.y = float(y);
        out.push_back(o);
    };
    switch (d.kind) {
        case PK_SPIKES:
            for (int k = 0; k < d.n; ++k) add(ID_SPIKE, xc + (k - (d.n - 1) / 2.0) * kBlock, kGroundY);
            break;
        case PK_PLAT:
            for (int k = 0; k < d.n; ++k) add(ID_BLOCK, xc + (k - (d.n - 1) / 2.0) * kBlock, kGroundY);
            break;
        case PK_PAD: {
            double L = padArc(sp);
            double xp = xc - L * 0.5;
            add(ID_PAD_YELLOW, xp, kGroundY);
            double cx = xp + 0.38 * L;
            for (int k = 0; k < d.n; ++k) add(ID_SPIKE, cx + (k - (d.n - 1) / 2.0) * kBlock, kGroundY);
            break;
        }
        case PK_TOWER:
            for (int k = 0; k < d.n; ++k) {
                double x = xc + (k - (d.n - 1) / 2.0) * kBlock;
                add(ID_BLOCK, x, kGroundY);
                add(ID_BLOCK, x, kGroundY + kBlock);
            }
            break;
    }
}

int patWindow(int i, int sp) {
    static int table[kCount][5];
    static std::once_flag once;
    std::call_once(once, [] {
        for (int p = 0; p < kCount; ++p) {
            for (int s = 0; s < 5; ++s) {
                if (kPats[p].kind == PK_PAD) {
                    table[p][s] = 99;  // pads launch the player automatically, no timing window to measure
                    continue;
                }
                std::vector<Obj> objs;
                emitPattern(p, 1500.0, s, objs);
                table[p][s] = jumpWindow(objs, 1500.0, s);
            }
        }
    });
    return table[i][sp];
}

}  // namespace ais
