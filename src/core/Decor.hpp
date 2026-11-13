#pragma once
// Decoration: start palette, colour triggers per section, beat pulses and hanging bars - all driven by the analysed music.
#include <string>

#include "Gen.hpp"

namespace ais {

struct Col {
    int r = 0, g = 0, b = 0;
};

struct Palette {
    Col bg, ground, ground2, obj, line, d1, d2, d3, d4;  // d1..d4 = colour channels 1..4 used by decoration objects
    float d1a = 0.60f, d2a = 0.45f, d3a = 0.35f, d4a = 0.25f;
    bool layout = false;
};

struct DecorStats {
    int triggers = 0;
    int pulses = 0;
    int bars = 0;
    int sectionChanges = 0;
};

// 0 Hell, 1 Green, 2 Yellow, 3 Music-based, 4 None (layout)
char const* decorName(int style);

// Adds trigger / decoration objects to lv.objs and fills `pal` (the colours the level starts with).
DecorStats decorate(Level& lv, Features const& F, Settings const& S, Palette& pal, uint64_t seed);

}  // namespace ais
