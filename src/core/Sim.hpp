#pragma once
// A small physics model of Geometry Dash used to *verify* generated levels:
//  - runCube():     finds out whether ANY sequence of clicks can get a cube through a stretch of level
//  - jumpWindow():  how many 1-frame click timings clear a pattern (tightness = difficulty)
//  - checkFlight(): interval-propagation feasibility for ship / ufo / wave corridors
// It is an approximation of the real engine (60 fps stepping, simplified hitboxes), good enough to catch
// impossible spacing and to calibrate difficulty, but the real game remains the final judge.
#include <string>
#include <vector>

namespace ais {

constexpr double kBlock = 30.0;
constexpr double kGroundY = 105.0;  // centre y of the bottom block row
constexpr double kFloorY = 90.0;    // floor surface
constexpr double kPortalY = 150.0;  // centre y of ground-level portals
constexpr double kFps = 60.0;
constexpr double kSpeed[5] = {251.16, 311.58, 387.42, 468.0, 576.0};  // units / second (0.5x 1x 2x 3x 4x)

constexpr double kJumpV = 11.180032;
constexpr double kGrav = 0.958199;
constexpr double kPadV = 16.0;
constexpr double kAirSec = 2.0 * kJumpV / kGrav / kFps;  // ~0.389 s for a normal cube jump
constexpr double kPadAirSec = 2.0 * kPadV / kGrav / kFps;

enum ObjId {
    ID_BLOCK = 1,
    ID_SPIKE = 8,
    ID_PORTAL_CUBE = 12,
    ID_PORTAL_SHIP = 13,
    ID_PAD_YELLOW = 35,
    ID_PORTAL_BALL = 47,
    ID_PORTAL_UFO = 111,
    ID_SPEED_05 = 200,
    ID_SPEED_1 = 201,
    ID_SPEED_2 = 202,
    ID_SPEED_3 = 203,
    ID_PORTAL_WAVE = 660,
    ID_TRIGGER_COLOR = 899,
    ID_SPEED_4 = 1334,
};

struct Obj {
    int id = 0;
    float x = 0, y = 0;
    std::string kv;  // extra ",key,value" pairs, already formatted
};

struct SimWorld {
    struct P {
        float x, y;
    };
    std::vector<P> blocks, spikes, pads;
    void clear();
    void add(Obj const& o);
    void sortAll();
};

struct CubeRun {
    bool ok = false;
    double failX = 0;
    int peakStates = 0;
};
// Can a cube that starts grounded at x0 reach x1? (explores every click/no-click choice per frame)
CubeRun runCube(SimWorld const& w, double x0, double x1, int speedIdx, size_t maxStates = 6000);

// Number of distinct single-click frame timings that get through an isolated pattern centred at xc.
int jumpWindow(std::vector<Obj> const& pattern, double xc, int speedIdx);

struct Column {
    int b = 0;   // first free row
    int t = 10;  // first filled row above (free rows are [b, t))
};
struct FlightCheck {
    bool ok = true;
    int failCol = -1;
    double minFree = 99;
};
// slopeRowsPerCol: how many rows per 30-unit column the vehicle can climb/descend.
// margin: half-height of the vehicle in rows. minFree: smallest allowed opening in rows.
FlightCheck checkFlight(std::vector<Column> const& cols, double slopeRowsPerCol, double margin, double minFree);

}  // namespace ais
