#include "Sim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ais {

namespace {

constexpr double kTerm = -15.0;
constexpr double kHalf = 15.0;       // solid half size (block and player)
constexpr double kInner = 6.0;       // player half size against hazards
constexpr double kSpikeHalfW = 5.0;  // spike hazard half width
constexpr double kLandTol = 7.0;     // how far a falling cube may be below a block top and still land on it

struct S {
    float y = float(kGroundY);
    float vy = 0.f;
    bool ground = true;
    int pad = -1;
};

uint64_t keyOf(S const& s) {
    uint64_t y = uint64_t(std::llround(double(s.y) * 20.0)) & 0xFFFFF;
    uint64_t v = uint64_t(std::llround(double(s.vy) * 20.0) + 1024) & 0xFFF;
    return (y << 33) | (v << 21) | (uint64_t(s.ground) << 20) | uint64_t((s.pad + 1) & 0xFFFFF);
}

template <class F>
void forNear(std::vector<SimWorld::P> const& v, double x, double r, F&& f) {
    auto it = std::lower_bound(v.begin(), v.end(), float(x - r), [](SimWorld::P const& p, float val) { return p.x < val; });
    for (; it != v.end() && double(it->x) <= x + r; ++it) f(size_t(it - v.begin()), *it);
}

// One 60 fps step. Returns false if the cube dies.
bool stepFrame(SimWorld const& w, S const& s, double px, double vx, bool press, S& out) {
    double nx = px + vx;
    double vy = s.vy;
    bool ground = s.ground;
    int pad = s.pad;
    double y0 = double(s.y), ny = y0;
    bool jumped = false;
    if (ground && press) {
        vy = kJumpV;
        ground = false;
        jumped = true;
    }
    if (!ground) {
        vy -= kGrav;
        if (vy < kTerm) vy = kTerm;
        ny = y0 + vy;
    } else {
        vy = 0;
    }
    bool landedNow = false;
    if (ny <= kGroundY) {
        ny = kGroundY;
        if (vy <= 0) {
            vy = 0;
            ground = true;
            landedNow = true;
        }
    }
    bool dead = false;
    forNear(w.blocks, nx, 2 * kHalf, [&](size_t, SimWorld::P const& b) {
        double dx = std::fabs(double(b.x) - nx), dy = std::fabs(double(b.y) - ny);
        if (dx < 2 * kHalf - 1e-3 && dy < 2 * kHalf - 1e-3) {
            double top = double(b.y) + kHalf;
            if (vy <= 0 && (y0 - kHalf) >= top - kLandTol) {
                ny = top + kHalf;
                vy = 0;
                ground = true;
                landedNow = true;
            } else {
                dead = true;
            }
        }
    });
    if (dead) return false;
    forNear(w.spikes, nx, kSpikeHalfW + kInner + 1, [&](size_t, SimWorld::P const& sp) {
        if (std::fabs(double(sp.x) - nx) < kSpikeHalfW + kInner && ny - kInner < double(sp.y) - 1 && ny + kInner > double(sp.y) - kHalf) dead = true;
    });
    if (dead) return false;
    forNear(w.pads, nx, 2 * kHalf, [&](size_t i, SimWorld::P const& p) {
        if (int(i) == pad) return;
        if (std::fabs(double(p.x) - nx) < 2 * kHalf - 1 && ny - kHalf < double(p.y) - 7 && ny + kHalf > double(p.y) - kHalf) {
            vy = kPadV;
            ground = false;
            landedNow = false;
            pad = int(i);
        }
    });
    if (ground && !landedNow && !jumped) {
        // still standing: is there something under us at the new x?
        bool supported = std::fabs(ny - kGroundY) < 1e-3;
        if (!supported) {
            forNear(w.blocks, nx, 2 * kHalf, [&](size_t, SimWorld::P const& b) {
                if (std::fabs(double(b.x) - nx) < 2 * kHalf - 1e-3 && std::fabs((double(b.y) + kHalf) - (ny - kHalf)) < 0.01) supported = true;
            });
        }
        if (!supported) ground = false;
    }
    if (!ground && pad >= 0 && vy < 0) pad = -1;  // allow the next pad once we are descending
    out.y = float(ny);
    out.vy = float(vy);
    out.ground = ground;
    out.pad = pad;
    return true;
}

}  // namespace

void SimWorld::clear() {
    blocks.clear();
    spikes.clear();
    pads.clear();
}

void SimWorld::add(Obj const& o) {
    if (o.id == ID_BLOCK) blocks.push_back({o.x, o.y});
    else if (o.id == ID_SPIKE) {
        // only upright spikes are hazards the cube can meet at ground level in our patterns;
        // rotated spikes (ceiling decoration, corridor teeth) are handled by the flight checker
        if (o.kv.find(",6,180") == std::string::npos) spikes.push_back({o.x, o.y});
    } else if (o.id == ID_PAD_YELLOW) pads.push_back({o.x, o.y});
}

void SimWorld::sortAll() {
    auto cmp = [](P const& a, P const& b) { return a.x < b.x; };
    std::sort(blocks.begin(), blocks.end(), cmp);
    std::sort(spikes.begin(), spikes.end(), cmp);
    std::sort(pads.begin(), pads.end(), cmp);
}

CubeRun runCube(SimWorld const& w, double x0, double x1, int sp, size_t maxStates) {
    CubeRun R;
    double vx = kSpeed[sp] / kFps;
    int frames = int((x1 - x0) / vx) + 1;
    std::vector<std::pair<uint64_t, S>> nxt;
    std::vector<S> cur{S{}};
    for (int f = 0; f < frames; ++f) {
        double px = x0 + f * vx;
        nxt.clear();
        for (auto const& s : cur) {
            int branches = s.ground ? 2 : 1;
            for (int b = 0; b < branches; ++b) {
                S o;
                if (stepFrame(w, s, px, vx, b == 1, o)) nxt.emplace_back(keyOf(o), o);
            }
        }
        if (nxt.empty()) {
            R.ok = false;
            R.failX = px + vx;
            return R;
        }
        std::sort(nxt.begin(), nxt.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        nxt.erase(std::unique(nxt.begin(), nxt.end(), [](auto const& a, auto const& b) { return a.first == b.first; }), nxt.end());
        cur.clear();
        for (auto const& p : nxt) cur.push_back(p.second);
        if (cur.size() > maxStates) cur.resize(maxStates);
        R.peakStates = std::max(R.peakStates, int(cur.size()));
    }
    R.ok = true;
    R.failX = x1;
    return R;
}

int jumpWindow(std::vector<Obj> const& pat, double xc, int sp) {
    SimWorld w;
    for (auto const& o : pat) w.add(o);
    w.sortAll();
    double vx = kSpeed[sp] / kFps;
    double x0 = xc - 520, x1 = xc + 420;
    int frames = int((x1 - x0) / vx) + 1;
    int lastPress = int((xc - 20 - x0) / vx);
    int ok = 0;
    for (int press = 0; press <= lastPress; ++press) {
        S s;
        bool alive = true;
        for (int f = 0; f < frames && alive; ++f) {
            S o;
            if (!stepFrame(w, s, x0 + f * vx, vx, f == press, o)) alive = false;
            else s = o;
        }
        if (alive) ++ok;
    }
    return ok;
}

FlightCheck checkFlight(std::vector<Column> const& cols, double slope, double margin, double minFree) {
    FlightCheck R;
    if (cols.empty()) return R;
    // player centre range in "row coordinates" (row r centre = r; free rows [b,t) span b-0.5 .. t-0.5)
    auto lo = [&](Column const& c) { return double(c.b) - 0.5 + margin; };
    auto hi = [&](Column const& c) { return double(c.t) - 0.5 - margin; };
    double a = std::max(lo(cols[0]), 0.0), b = std::min(hi(cols[0]), 2.0);  // we enter from the ground, near the portal
    if (a > b) {
        R.ok = false;
        R.failCol = 0;
        return R;
    }
    for (size_t i = 0; i < cols.size(); ++i) {
        double free = double(cols[i].t - cols[i].b);
        R.minFree = std::min(R.minFree, free);
        if (free + 1e-9 < minFree) {
            R.ok = false;
            R.failCol = int(i);
            return R;
        }
        if (i > 0) {
            a -= slope;
            b += slope;
        }
        a = std::max(a, lo(cols[i]));
        b = std::min(b, hi(cols[i]));
        if (a > b + 1e-9) {
            R.ok = false;
            R.failCol = int(i);
            return R;
        }
    }
    // we have to be able to reach the exit portal (rows 0..3 -> centre 0.2..2.8)
    if (b < 0.2 - 1e-9 + 0.0 || a > 2.8) {
        R.ok = false;
        R.failCol = int(cols.size()) - 1;
    }
    return R;
}

}  // namespace ais
