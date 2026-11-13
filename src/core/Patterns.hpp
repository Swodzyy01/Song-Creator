#pragma once
// Library of grounded (cube) obstacle patterns + the "tightness" of each one, measured with the simulator.
#include <vector>

#include "Sim.hpp"

namespace ais {

enum PatKind { PK_SPIKES, PK_PLAT, PK_PAD, PK_TOWER };

struct PatDef {
    char const* name;
    int tier;       // 1 (easiest) .. 5 (frame-perfect style)
    int n;          // spikes / blocks in the pattern
    PatKind kind;
    double airSec;  // time the player spends in the air for this pattern
    double weight;  // base probability weight
};

int patCount();
PatDef const& patDef(int i);
double patHalfW(int i, int speedIdx);                                  // half footprint in units
void emitPattern(int i, double xc, int speedIdx, std::vector<Obj>& out);
// Number of 1-frame click timings that clear the pattern at this speed (measured by simulation, cached).
int patWindow(int i, int speedIdx);

}  // namespace ais
