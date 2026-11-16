#pragma once
// Library of grounded (cube) obstacle patterns + the "tightness" of each one, measured with the simulator.
#include <vector>

#include "Sim.hpp"

namespace ais {

enum PatKind { PK_SPIKES, PK_PLAT, PK_PAD, PK_TOWER, PK_BLOCKSPIKE, PK_SPIKEPLAT, PK_STAIRS, PK_PLATSPIKE, PK_PADPLAT };
enum PatFamily { PF_SPIKE, PF_BLOCK, PF_AIR, PF_TIGHT };

struct PatDef {
    char const* name;
    int tier;       // 1 (easiest) .. 5 (tightest)
    int n;          // spikes / blocks in the pattern
    PatKind kind;
    double airSec;  // time the player spends off the ground for this pattern
    double weight;  // base probability weight
    PatFamily family;
};

int patCount();
PatDef const& patDef(int i);  // index 0 is always the single spike (used for repairs)
double patHalfW(int i, int speedIdx);  // half footprint in units
void emitPattern(int i, double xc, int speedIdx, std::vector<Obj>& out);
// Number of 1-frame click timings that clear the pattern at this speed (measured by simulation, cached).
// Multi-click patterns (stairs, platform + spike, pad to platform) report 7 when the simulator can pass them, else 0.
int patWindow(int i, int speedIdx);

}  // namespace ais
