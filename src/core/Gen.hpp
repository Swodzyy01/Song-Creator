#pragma once
// Level generator: plan -> compose many variants per segment -> simulate/verify -> pick the best.
#include <functional>
#include <string>
#include <vector>

#include "Audio.hpp"
#include "Patterns.hpp"
#include "Profile.hpp"
#include "Rng.hpp"
#include "Sim.hpp"

namespace ais {

enum class Mode { Cube = 0, Ship, Ufo, Wave };
char const* modeName(Mode m);
int modePortalId(Mode m);
int speedPortalId(int speedIdx);
char const* speedName(int speedIdx);
std::string mmss(double seconds);
char const* styleName(Mode m, int style);
// 45 degree slope. kind: 0 = lower-right solid, 1 = lower-left solid, 2 = upper-right solid, 3 = upper-left solid.
Obj slopeObj(double x, double y, int kind, bool swap);

struct Segment {
    double t0 = 0, t1 = 0;
    Mode mode = Mode::Cube;
    int speed = 1;
    SecKind kind = SecKind::Verse;
    float intensity = 0.5f;
    int style = 0;  // cube: 0 spikes 1 blocks 2 air 3 tight | flight: 0 flow 1 zigzag 2 stairs 3 pinch
};

// level time (seconds since the player started moving) -> x position
struct SpeedMap {
    struct K {
        double t, x, v;
    };
    std::vector<K> k;
    double x(double tau) const;
};

struct EventRec {
    double tHit = 0;     // the musical moment this obstacle belongs to
    double tCenter = 0;  // where the obstacle sits (click lands on tHit)
    double x = 0;
    int pat = 0;
    float strength = 0;
};

struct SegmentResult {
    // cube
    std::vector<EventRec> events;
    std::vector<std::vector<Obj>> evObjs;
    double xs = 0, xe = 0;  // verification range
    // flight
    std::vector<Obj> flightObjs;
    std::vector<Column> cols;
    double colX0 = 0;
    int knots = 0;
    int slopes = 0;
    int features = 0;  // wall spikes, teeth rows, floating obstacles
    // shared
    double score = 0, rhythm = 0, coverage = 0, density = 0, variety = 0;
    int repairs = 0;
    bool verified = false;
    int variants = 0;
};

struct Level {
    std::vector<Segment> segs;
    std::vector<SegmentResult> res;
    SpeedMap map;
    double shift = 0;
    std::vector<Obj> objs;  // gameplay objects, portals included
    double endT = 0, endX = 0;
    int tightEvents = 0;      // cube obstacles with a click window <= 4 frames (frame-perfect style)
    int minWindowUsed = 99;   // narrowest click window (frames) among all cube obstacles
    int totalEvents = 0;
    int repairs = 0;
    int flightSegs = 0, flightOk = 0;
    int slopes = 0, features = 0;
    int distinctPatterns = 0;
    double X(double t) const { return map.x(t + shift); }
};

using LogFn = std::function<void(std::string const&)>;
struct Hooks {
    LogFn log;
    ProgressFn progress;  // 0..1 over the generation stage
    CancelFn cancelled;
};

Level generateLevel(Features const& F, Profile const& P, Settings const& S, double length, uint64_t seed, Hooks const& H);

}  // namespace ais
