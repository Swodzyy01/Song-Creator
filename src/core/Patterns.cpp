#include "Patterns.hpp"

#include <algorithm>
#include <mutex>

namespace ais {

namespace {

// name, tier, n, kind, airSec, weight, family
const PatDef kPats[] = {
    {"single spike", 1, 1, PK_SPIKES, kAirSec, 1.00, PF_SPIKE},
    {"double spike", 1, 2, PK_SPIKES, kAirSec, 0.80, PF_SPIKE},
    {"triple spike", 2, 3, PK_SPIKES, kAirSec, 0.55, PF_SPIKE},
    {"platform 2", 2, 2, PK_PLAT, kAirSec + 0.07, 0.30, PF_BLOCK},
    {"platform 3", 1, 3, PK_PLAT, kAirSec + 0.07, 0.45, PF_BLOCK},
    {"platform 4", 1, 4, PK_PLAT, kAirSec + 0.07, 0.45, PF_BLOCK},
    {"platform 5", 1, 5, PK_PLAT, kAirSec + 0.07, 0.35, PF_BLOCK},
    {"block + spike", 2, 1, PK_BLOCKSPIKE, kAirSec + 0.07, 0.60, PF_BLOCK},
    {"spike to platform", 2, 3, PK_SPIKEPLAT, kAirSec + 0.12, 0.55, PF_BLOCK},
    {"stairs", 2, 4, PK_STAIRS, kAirSec + 0.35, 0.50, PF_BLOCK},
    {"platform + spike", 3, 5, PK_PLATSPIKE, 0.95, 0.45, PF_BLOCK},
    {"pad + spike", 3, 1, PK_PAD, kPadAirSec, 0.35, PF_AIR},
    {"pad + double", 3, 2, PK_PAD, kPadAirSec, 0.35, PF_AIR},
    {"pad + triple", 4, 3, PK_PAD, kPadAirSec, 0.30, PF_AIR},
    {"pad to platform", 3, 3, PK_PADPLAT, kPadAirSec + 0.20, 0.50, PF_AIR},
    {"tower 1x2", 5, 1, PK_TOWER, kAirSec, 0.50, PF_TIGHT},
    {"tower 2x2", 5, 2, PK_TOWER, kAirSec, 0.50, PF_TIGHT},
    {"quad spike", 5, 4, PK_SPIKES, kAirSec, 0.45, PF_TIGHT},
    {"quint spike", 5, 5, PK_SPIKES, kAirSec, 0.35, PF_TIGHT},
    {"six spikes", 5, 6, PK_SPIKES, kAirSec, 0.30, PF_TIGHT},
    {"seven spikes", 5, 7, PK_SPIKES, kAirSec, 0.25, PF_TIGHT},
};
constexpr int kCount = int(sizeof(kPats) / sizeof(kPats[0]));

double padArc(int speedIdx) { return kSpeed[speedIdx] / kFps * (2.0 * kPadV / kGrav); }

}  // namespace

int patCount() { return kCount; }
PatDef const& patDef(int i) { return kPats[i]; }

double patHalfW(int i, int sp) {
    PatDef const& d = kPats[i];
    switch (d.kind) {
        case PK_PAD: return padArc(sp) * 0.5;
        case PK_PADPLAT: return std::max(padArc(sp) * 0.5, padArc(sp) * 0.12 + 120.0);
        case PK_SPIKEPLAT: return 90.0;
        case PK_STAIRS: return 75.0;
        case PK_PLATSPIKE: return 75.0;
        default: return 15.0 * d.n;
    }
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
    const double row1 = kGroundY + kBlock, row2 = kGroundY + 2 * kBlock;
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
                add(ID_BLOCK, x, row1);
            }
            break;
        case PK_BLOCKSPIKE:
            add(ID_BLOCK, xc, kGroundY);
            add(ID_SPIKE, xc, row1);
            break;
        case PK_SPIKEPLAT:
            add(ID_SPIKE, xc - 45.0, kGroundY);
            for (int k = 0; k < 3; ++k) add(ID_BLOCK, xc + 15.0 + 30.0 * k, kGroundY);
            break;
        case PK_STAIRS:
            add(ID_BLOCK, xc - 45.0, kGroundY);
            add(ID_BLOCK, xc - 15.0, kGroundY);
            for (int k = 0; k < 2; ++k) {
                add(ID_BLOCK, xc + 15.0 + 30.0 * k, kGroundY);
                add(ID_BLOCK, xc + 15.0 + 30.0 * k, row1);
            }
            break;
        case PK_PLATSPIKE:
            for (int k = 0; k < 5; ++k) add(ID_BLOCK, xc + (k - 2) * kBlock, kGroundY);
            add(ID_SPIKE, xc, row1);
            break;
        case PK_PADPLAT: {
            double L = padArc(sp);
            double xp = xc - L * 0.5;
            add(ID_PAD_YELLOW, xp, kGroundY);
            double front = xp + 0.62 * L;
            for (int k = 0; k < 3; ++k) {
                add(ID_BLOCK, front + 15.0 + 30.0 * k, row2);
                add(ID_SPIKE, front + 15.0 + 30.0 * k, kGroundY);  // spikes under the platform: miss the landing and you pay
            }
            break;
        }
    }
}

int patWindow(int i, int sp) {
    static int table[kCount][5];
    static std::once_flag once;
    std::call_once(once, [] {
        for (int p = 0; p < kCount; ++p) {
            for (int s = 0; s < 5; ++s) {
                PatKind k = kPats[p].kind;
                std::vector<Obj> objs;
                emitPattern(p, 1500.0, s, objs);
                if (k == PK_PAD) {
                    table[p][s] = 99;  // pads launch the player automatically, no timing window to measure
                } else if (k == PK_STAIRS || k == PK_PLATSPIKE || k == PK_PADPLAT) {
                    // needs more than one click (or none): ask the bot whether ANY input sequence passes it
                    SimWorld w;
                    for (auto const& o : objs) w.add(o);
                    w.sortAll();
                    table[p][s] = runCube(w, 900.0, 2300.0, s).ok ? 7 : 0;
                } else {
                    table[p][s] = jumpWindow(objs, 1500.0, s);
                }
            }
        }
    });
    return table[i][sp];
}

}  // namespace ais
