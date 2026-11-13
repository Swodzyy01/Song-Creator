#pragma once
// What the user asked for (Settings) and what that means in numbers (Profile).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace ais {

enum Diff { Easy = 0, Normal, Hard, Harder, Insane, EasyDemon, MediumDemon, HardDemon, InsaneDemon, ExtremeDemon, DiffCount };

inline char const* diffName(int d) {
    static char const* n[] = {"Easy", "Normal", "Hard", "Harder", "Insane", "Easy Demon", "Medium Demon", "Hard Demon", "Insane Demon", "Extreme Demon"};
    return (d >= 0 && d < DiffCount) ? n[d] : "?";
}

struct Settings {
    int difficulty = Insane;
    int rank = 0;               // Extreme Demon: Demon-List position 1..1500 (0 = not given -> treated as 1000)
    bool upcoming = false;      // "Upcoming" tier: harder than the current Top 1
    bool frameperfect = false;  // only honoured for Top 250 and above (and Upcoming)
    int decor = 0;              // 0 Hell, 1 Green, 2 Yellow, 3 Music-based, 4 None (layout)
    double lengthSec = 90.0;    // how much of the song the level covers
    int depth = 3;              // thinking depth 1..5
    uint64_t seed = 0;          // 0 = derive from song + settings
    double syncOffsetSec = 0.0; // + delays the obstacles, - makes them earlier
};

inline bool frameperfectAllowed(Settings const& s) {
    return s.difficulty == ExtremeDemon && (s.upcoming || (s.rank >= 1 && s.rank <= 250));
}

struct Profile {
    double d = 0.5;  // 0..1 overall difficulty
    std::string name;
    double f = 0;  // 0..1 position inside Extreme Demon (0 = Top 1500, 1 = Top 1)
    double aps = 2.0;       // target obstacle events per second at full intensity
    double airSlack = 0.3;  // extra spacing between consecutive jumps (fraction of one jump)
    int minWindow = 6;      // narrowest allowed click window in frames (measured by the simulator)
    int tierCap = 3;        // highest pattern tier allowed
    double wCube = 1, wShip = 0, wUfo = 0, wWave = 0;
    int speedMin = 1, speedMax = 1;  // indices into kSpeed
    double shipGap = 6, ufoGap = 6, waveGap = 4;
    double slope = 0.5;  // fraction of the vehicle's climb rate used by corridors
    bool fp = false;
    double fpChance = 0;
    int rank = 0;
    bool upcoming = false;
};

namespace detail {
struct Anchor {
    double d, aps, slack;
    int win, tier;
    double wc, ws, wu, ww;
    int smin, smax;
    double ship, ufo, wave, slope;
};
// d     aps   slack win tier  cube  ship  ufo   wave  smin smax  ship ufo  wave slope
inline Anchor const* anchors() {
    static Anchor const A[] = {
        {0.05, 0.75, 1.20, 14, 1, 0.80, 0.20, 0.00, 0.00, 1, 1, 7.0, 7.0, 6.0, 0.40},   // Easy
        {0.15, 1.00, 0.95, 12, 1, 0.72, 0.23, 0.05, 0.00, 1, 1, 6.5, 6.5, 5.5, 0.45},   // Normal
        {0.28, 1.30, 0.70, 10, 2, 0.62, 0.23, 0.10, 0.05, 1, 2, 6.0, 6.0, 5.0, 0.50},   // Hard
        {0.40, 1.65, 0.50, 8, 2, 0.54, 0.23, 0.13, 0.10, 1, 2, 5.5, 5.5, 4.5, 0.55},    // Harder
        {0.52, 2.05, 0.35, 6, 3, 0.46, 0.24, 0.15, 0.15, 1, 3, 5.0, 5.5, 4.0, 0.62},    // Insane
        {0.60, 2.40, 0.25, 5, 3, 0.42, 0.23, 0.17, 0.18, 1, 3, 4.5, 5.0, 3.6, 0.68},    // Easy Demon
        {0.68, 2.80, 0.16, 4, 4, 0.38, 0.23, 0.18, 0.21, 2, 3, 4.2, 4.8, 3.3, 0.74},    // Medium Demon
        {0.75, 3.20, 0.10, 3, 4, 0.35, 0.22, 0.19, 0.24, 2, 4, 4.0, 4.5, 3.0, 0.80},    // Hard Demon
        {0.82, 3.60, 0.05, 3, 4, 0.32, 0.22, 0.20, 0.26, 2, 4, 3.7, 4.3, 2.8, 0.86},    // Insane Demon
        {0.88, 4.00, 0.03, 2, 5, 0.30, 0.22, 0.20, 0.28, 2, 4, 3.4, 4.0, 2.6, 0.90},    // Extreme Demon, Top 1500
        {1.00, 5.40, 0.00, 1, 5, 0.26, 0.22, 0.22, 0.30, 3, 4, 3.0, 3.6, 2.0, 0.96},    // Extreme Demon, Top 1 / Upcoming
    };
    return A;
}
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
}  // namespace detail

// Position inside the Extreme Demon tier: log scale, Top 1500 -> 0, Top 1 -> 1.
inline double extremeFraction(int rank) {
    rank = std::max(1, std::min(1500, rank));
    return std::log(1500.0 / double(rank)) / std::log(1500.0);
}

inline Profile makeProfile(Settings const& s) {
    using namespace detail;
    Profile P;
    Anchor const* A = anchors();
    int di = std::max(0, std::min(int(DiffCount) - 1, s.difficulty));
    Anchor a = A[di];
    P.rank = 0;
    P.upcoming = false;
    if (di == ExtremeDemon) {
        int rank = s.rank > 0 ? s.rank : 1000;
        double f = s.upcoming ? 1.0 : extremeFraction(rank);
        Anchor lo = A[ExtremeDemon], hi = A[ExtremeDemon + 1];
        a.d = lerp(lo.d, hi.d, f);
        a.aps = lerp(lo.aps, hi.aps, f);
        a.slack = lerp(lo.slack, hi.slack, f);
        a.win = int(std::lround(lerp(lo.win, hi.win, f)));
        a.wc = lerp(lo.wc, hi.wc, f);
        a.ws = lerp(lo.ws, hi.ws, f);
        a.wu = lerp(lo.wu, hi.wu, f);
        a.ww = lerp(lo.ww, hi.ww, f);
        a.smin = int(std::lround(lerp(lo.smin, hi.smin, f)));
        a.ship = lerp(lo.ship, hi.ship, f);
        a.ufo = lerp(lo.ufo, hi.ufo, f);
        a.wave = lerp(lo.wave, hi.wave, f);
        a.slope = lerp(lo.slope, hi.slope, f);
        P.f = f;
        P.rank = s.upcoming ? 0 : rank;
        P.upcoming = s.upcoming;
        if (s.upcoming) {
            a.ship -= 0.2;
            a.wave -= 0.2;
            a.aps += 0.6;
        }
        P.name = s.upcoming ? "Extreme Demon (Upcoming)" : "Extreme Demon (Top " + std::to_string(rank) + ")";
    } else {
        P.name = diffName(di);
    }
    P.d = a.d;
    P.aps = a.aps;
    P.airSlack = a.slack;
    P.minWindow = a.win;
    P.tierCap = a.tier;
    P.wCube = a.wc;
    P.wShip = a.ws;
    P.wUfo = a.wu;
    P.wWave = a.ww;
    P.speedMin = a.smin;
    P.speedMax = a.smax;
    P.shipGap = a.ship;
    P.ufoGap = a.ufo;
    P.waveGap = a.wave;
    P.slope = a.slope;
    if (s.frameperfect && frameperfectAllowed(s)) {
        P.fp = true;
        P.fpChance = 0.18 + 0.50 * P.f;
        P.minWindow = 1;
        P.airSlack = 0.0;
    }
    return P;
}

}  // namespace ais
