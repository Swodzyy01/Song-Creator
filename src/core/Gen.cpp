#include "Gen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace ais {

namespace {

double clampd(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }

bool contains(std::vector<int> const& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// Minimum time between the centres of two consecutive jump patterns so that the cube can land and jump again.
double minDt(int a, int b, int sp, Profile const& P) {
    double v = kSpeed[sp];
    double slackSec = P.airSlack * kAirSec;
    double landMin = 16.0 + 44.0 * clampd(P.airSlack / 1.2, 0.0, 1.0);
    double t1 = (patDef(a).airSec + patDef(b).airSec) * 0.5 + slackSec;
    double t2 = (patHalfW(a, sp) + patHalfW(b, sp) + landMin) / v;
    return std::max(t1, t2);
}

double flightCap(Mode m, int sp) {  // rows per 30-unit column the vehicle can climb or descend
    double r = kSpeed[1] / kSpeed[sp];
    switch (m) {
        case Mode::Ship: return 1.20 * r;
        case Mode::Ufo: return 0.55 * r;
        default: return 1.0;  // the wave always moves at 45 degrees
    }
}

struct Ctx {
    Features const* F;
    Profile const* P;
    Settings const* S;
    std::vector<Segment> const* segs;
    SpeedMap const* map;
    double shift;
    double X(double t) const { return map->x(t + shift); }
};

SecKind kindAt(Features const& F, double t) {
    for (auto const& s : F.sections)
        if (t >= s.t0 && t < s.t1) return s.kind;
    return F.sections.empty() ? SecKind::Verse : F.sections.back().kind;
}

double kindDensity(SecKind k) {
    switch (k) {
        case SecKind::Intro: return 0.60;
        case SecKind::Verse: return 0.95;
        case SecKind::Build: return 1.00;
        case SecKind::Drop: return 1.15;
        case SecKind::Break: return 0.55;
        case SecKind::Outro: return 0.60;
    }
    return 1.0;
}

int pickWeighted(Rng& rng, double const* w, int n) {
    double sum = 0;
    for (int i = 0; i < n; ++i) sum += w[i];
    if (sum <= 0) return 0;
    double r = rng.uni() * sum;
    for (int i = 0; i < n; ++i) {
        if (r < w[i]) return i;
        r -= w[i];
    }
    return n - 1;
}

// ---------------------------------------------------------------------------------------------
// planning
// ---------------------------------------------------------------------------------------------

int pickStyle(Rng& rng, bool flight, SecKind k, int tierCap, int prev) {
    double w[4];
    bool hard = tierCap >= 4;
    if (!flight) {
        switch (k) {
            case SecKind::Drop: w[0] = 0.30, w[1] = 0.15, w[2] = 0.30, w[3] = hard ? 0.25 : 0.0; break;
            case SecKind::Build: w[0] = 0.30, w[1] = 0.30, w[2] = 0.30, w[3] = hard ? 0.10 : 0.0; break;
            case SecKind::Verse: w[0] = 0.30, w[1] = 0.40, w[2] = 0.20, w[3] = hard ? 0.10 : 0.0; break;
            case SecKind::Break: w[0] = 0.20, w[1] = 0.60, w[2] = 0.20, w[3] = 0.0; break;
            default: w[0] = 0.50, w[1] = 0.40, w[2] = 0.10, w[3] = 0.0; break;
        }
    } else {
        switch (k) {
            case SecKind::Drop: w[0] = 0.15, w[1] = 0.35, w[2] = 0.15, w[3] = 0.35; break;
            case SecKind::Build: w[0] = 0.20, w[1] = 0.30, w[2] = 0.30, w[3] = 0.20; break;
            case SecKind::Verse: w[0] = 0.40, w[1] = 0.20, w[2] = 0.25, w[3] = 0.15; break;
            case SecKind::Break: w[0] = 0.65, w[1] = 0.05, w[2] = 0.25, w[3] = 0.05; break;
            default: w[0] = 0.70, w[1] = 0.00, w[2] = 0.30, w[3] = 0.00; break;
        }
    }
    int pick = pickWeighted(rng, w, 4);
    if (pick == prev) pick = pickWeighted(rng, w, 4);  // one re-roll: avoid the same flavour twice in a row
    return pick;
}

std::vector<Segment> planSegments(Features const& F, Profile const& P, double length, Rng& rng, LogFn const& log) {
    double beat = 60.0 / F.bpm;
    auto snapBar = [&](double t) {
        double bar = beat * 4.0;
        double k = std::round((t - F.beatOffset) / bar);
        return std::max(0.0, F.beatOffset + k * bar);
    };
    std::vector<double> e{0.0};
    for (auto const& s : F.sections)
        if (s.t0 > 6.0 && s.t0 < length - 6.0) e.push_back(snapBar(s.t0));
    e.push_back(length);
    std::sort(e.begin(), e.end());
    // split long stretches so the level keeps changing
    std::vector<double> e2;
    for (size_t i = 0; i + 1 < e.size(); ++i) {
        double a = e[i], b = e[i + 1], D = b - a;
        e2.push_back(a);
        if (D > 34.0) {
            int pieces = int(std::ceil(D / 24.0));
            for (int p = 1; p < pieces; ++p) e2.push_back(snapBar(a + D * p / pieces));
        }
    }
    e2.push_back(length);
    std::sort(e2.begin(), e2.end());
    std::vector<double> cl{0.0};
    for (size_t i = 1; i + 1 < e2.size(); ++i)
        if (e2[i] - cl.back() >= 5.0 && length - e2[i] >= 5.0) cl.push_back(e2[i]);
    cl.push_back(length);

    std::vector<Segment> segs;
    Mode prevMode = Mode::Cube;
    int prevSpeed = 1, prevCubeStyle = -1, prevFlightStyle = -1;
    for (size_t i = 0; i + 1 < cl.size(); ++i) {
        Segment s;
        s.t0 = cl[i];
        s.t1 = cl[i + 1];
        s.kind = kindAt(F, 0.5 * (s.t0 + s.t1));
        double acc = 0;
        int cnt = 0;
        for (double t = s.t0; t < s.t1; t += 0.25, ++cnt) acc += double(F.intensityAt(t));
        s.intensity = cnt ? float(acc / cnt) : 0.5f;
        double dur = s.t1 - s.t0;

        double w[4] = {P.wCube, P.wShip, P.wUfo, P.wWave};
        switch (s.kind) {
            case SecKind::Break:
                w[0] *= 0.8, w[1] *= 1.8, w[2] *= 1.4, w[3] *= 0.6;
                break;
            case SecKind::Drop:
                w[0] *= 0.8, w[1] *= 1.1, w[2] *= 1.2, w[3] *= 1.5;
                break;
            case SecKind::Build:
                w[1] *= 1.2;
                break;
            default:
                break;
        }
        if (i == 0 || dur < 6.0 || s.kind == SecKind::Intro || s.kind == SecKind::Outro) w[1] = w[2] = w[3] = 0.0;
        if (prevMode != Mode::Cube) {
            w[int(prevMode)] = 0.0;  // never the same vehicle twice in a row
            w[0] *= 1.4;
        }
        s.mode = Mode(pickWeighted(rng, w, 4));

        // speed: follows the energy of the section, but even the hardest levels breathe
        double kindBias = 0.0;
        switch (s.kind) {
            case SecKind::Intro:
            case SecKind::Outro: kindBias = -0.30; break;
            case SecKind::Break: kindBias = -0.25; break;
            case SecKind::Build: kindBias = 0.08; break;
            case SecKind::Drop: kindBias = 0.18; break;
            default: break;
        }
        if (s.kind == SecKind::Drop) kindBias = 0.12;
        double tgt = clampd(0.22 + 0.45 * double(s.intensity) + kindBias + rng.uni(-0.20, 0.20), 0.0, 1.0);
        int smin = P.speedMin, smax = P.speedMax;
        int sp = smin + int(std::lround(tgt * (smax - smin)));
        if (i > 0 && std::abs(sp - prevSpeed) > 1 && s.kind != SecKind::Drop) sp = prevSpeed + (sp > prevSpeed ? 1 : -1);
        if (i == 0) sp = std::max(1, std::min(sp, 2));
        s.speed = int(clampd(sp, 0, 4));

        bool flight = s.mode != Mode::Cube;
        s.style = pickStyle(rng, flight, s.kind, P.tierCap, flight ? prevFlightStyle : prevCubeStyle);
        (flight ? prevFlightStyle : prevCubeStyle) = s.style;
        prevMode = s.mode;
        prevSpeed = s.speed;
        segs.push_back(s);
    }
    if (log) {
        for (size_t i = 0; i < segs.size(); ++i) {
            char b[220];
            std::snprintf(b, sizeof b, "  segment %zu/%zu  %s-%s  %-9s intensity %.2f  ->  %s @ %s, %s", i + 1, segs.size(), mmss(segs[i].t0).c_str(), mmss(segs[i].t1).c_str(),
                          secName(segs[i].kind), double(segs[i].intensity), modeName(segs[i].mode), speedName(segs[i].speed), styleName(segs[i].mode, segs[i].style));
            log(b);
        }
    }
    return segs;
}

SpeedMap buildSpeedMap(std::vector<Segment> const& segs, double shift) {
    SpeedMap M;
    M.k.push_back({0.0, 0.0, kSpeed[1]});
    if (!segs.empty() && segs[0].speed != 1) {
        double xp = 150.0;
        M.k.push_back({xp / kSpeed[1], xp, kSpeed[segs[0].speed]});
    }
    for (size_t i = 1; i < segs.size(); ++i) {
        double tau = segs[i].t0 + shift;
        M.k.push_back({tau, M.x(tau), kSpeed[segs[i].speed]});
    }
    return M;
}

// ---------------------------------------------------------------------------------------------
// cube segments
// ---------------------------------------------------------------------------------------------

struct Hit {
    double t;
    float s;
    int band;
};

const double kFam[4][4] = {{1.8, 0.7, 0.8, 0.8}, {0.6, 1.9, 0.7, 0.6}, {0.7, 0.9, 2.0, 0.8}, {0.9, 0.8, 0.9, 2.2}};

double bandBoost(int band, PatFamily f) {
    if (band == 0) return f == PF_SPIKE ? 1.3 : (f == PF_AIR ? 1.2 : 1.0);
    if (band == 1) return f == PF_BLOCK ? 1.4 : 1.0;
    return f == PF_SPIKE ? 1.1 : 1.0;
}

SegmentResult genCube(Ctx const& c, size_t si, Rng& rng) {
    SegmentResult R;
    auto const& segs = *c.segs;
    Segment const& sg = segs[si];
    Features const& F = *c.F;
    Profile const& P = *c.P;
    int sp = sg.speed;
    bool first = si == 0, last = si + 1 == segs.size();
    Mode prevM = first ? Mode::Cube : segs[si - 1].mode, nextM = last ? Mode::Cube : segs[si + 1].mode;
    double entry = first ? 3.2 : (prevM != Mode::Cube ? 1.1 : 0.8);
    double exitG = last ? 0.6 : (nextM != Mode::Cube ? 1.0 : 0.5);
    double lead = 0.17;  // the click happens on the beat, the obstacle is a bit further on
    double ta = sg.t0 + entry, tb = sg.t1 - exitG;
    double dur = tb - ta;
    R.xs = c.X(ta) - 250.0;
    R.xe = c.X(tb) + 260.0;
    if (dur < 1.2) {
        R.verified = true;
        return R;
    }
    double beat = 60.0 / F.bpm;

    std::vector<Hit> hits;
    for (auto const& o : F.onsets) {
        if (o.t < ta - lead || o.t > tb - lead) continue;
        double t = o.t, nb = F.nearestBeat(t);
        if (std::fabs(nb - t) < 0.045) t = nb;
        hits.push_back({t, o.strength, o.band});
    }
    std::sort(hits.begin(), hits.end(), [](Hit const& a, Hit const& b) { return a.t < b.t; });
    {  // music too sparse here? fall back on the beat grid
        std::vector<Hit> extra;
        for (double t = F.nearestBeat(ta - lead); t <= tb - lead; t += beat) {
            if (t < ta - lead) continue;
            bool near = false;
            for (auto const& h : hits)
                if (std::fabs(h.t - t) < 0.09) {
                    near = true;
                    break;
                }
            if (!near) extra.push_back({t, 0.22f, 0});
        }
        hits.insert(hits.end(), extra.begin(), extra.end());
        std::sort(hits.begin(), hits.end(), [](Hit const& a, Hit const& b) { return a.t < b.t; });
        std::vector<Hit> ded;
        for (auto const& h : hits) {
            if (!ded.empty() && h.t - ded.back().t < 0.06) {
                if (h.s > ded.back().s) ded.back() = h;
            } else {
                ded.push_back(h);
            }
        }
        hits.swap(ded);
    }

    double densF = clampd(0.45 + 0.95 * double(sg.intensity), 0.4, 1.45) * kindDensity(sg.kind);
    double minBase = kAirSec * (1.0 + P.airSlack);
    double target = std::min(P.aps * densF, 0.92 / minBase);  // a cube cannot jump more often than once per jump
    int N = std::max(0, int(std::lround(target * dur)));

    std::vector<std::pair<double, int>> order;
    for (size_t i = 0; i < hits.size(); ++i) {
        double pri = std::pow(double(hits[i].s), 1.4) * (0.7 + 0.6 * rng.uni()) + (hits[i].band == 0 ? 0.12 : (hits[i].band == 1 ? 0.05 : 0.0));
        order.emplace_back(pri, int(i));
    }
    std::sort(order.begin(), order.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
    std::vector<int> chosen;
    std::vector<double> chosenT;
    for (auto const& pr : order) {
        if (int(chosen.size()) >= N) break;
        double t = hits[size_t(pr.second)].t;
        bool ok = true;
        for (double u : chosenT)
            if (std::fabs(u - t) < minBase) {
                ok = false;
                break;
            }
        if (ok) {
            chosen.push_back(pr.second);
            chosenT.push_back(t);
        }
    }
    std::sort(chosen.begin(), chosen.end(), [&](int a, int b) { return hits[size_t(a)].t < hits[size_t(b)].t; });

    std::vector<int> used;
    double hardUntil = -1;  // after a tight pattern the player gets easy ones for a moment
    int fpUsed = 0, fpMax = int(std::ceil(dur / 12.0));
    int style = std::max(0, std::min(3, sg.style));
    for (size_t k = 0; k < chosen.size(); ++k) {
        Hit const& h = hits[size_t(chosen[k])];
        double tC = h.t + lead;
        double dtPrev = R.events.empty() ? 1e9 : tC - R.events.back().tCenter;
        double dtNext = (k + 1 < chosen.size()) ? (hits[size_t(chosen[k + 1])].t + lead) - tC : 1e9;
        bool easyOnly = tC < hardUntil;
        std::vector<int> cand;
        for (int p = 0; p < patCount(); ++p) {
            if (patDef(p).tier > P.tierCap) continue;
            int win = patWindow(p, sp);
            if (win < P.minWindow) continue;
            if (easyOnly && win < 10) continue;
            if (used.size() >= 2 && used.back() == p && used[used.size() - 2] == p) continue;  // never three in a row
            if (!R.events.empty() && minDt(R.events.back().pat, p, sp, P) > dtPrev) continue;
            if (minDt(p, 0, sp, P) > dtNext) continue;
            cand.push_back(p);
        }
        if (cand.empty()) continue;
        int pick = -1;
        if (used.size() >= 4 && rng.chance(0.12)) {  // now and then bring back the motif from a bar ago
            int m = used[used.size() - 4];
            if (contains(cand, m)) pick = m;
        }
        if (pick < 0 && P.fp && fpUsed < fpMax && (sg.kind == SecKind::Drop || sg.kind == SecKind::Build) && dtPrev >= 1.1 && rng.chance(P.fpChance)) {
            std::vector<int> tight;  // frame-perfect style: narrow, but never narrower than minWindow, and only a few per segment
            for (int p : cand) {
                int win = patWindow(p, sp);
                if (win <= 6 && patDef(p).kind != PK_PAD) tight.push_back(p);
            }
            if (!tight.empty()) {
                pick = tight[size_t(rng.range(0, int(tight.size()) - 1))];
                ++fpUsed;
            }
        }
        if (pick < 0) {
            std::vector<double> w;
            double sum = 0;
            for (int p : cand) {
                PatDef const& d = patDef(p);
                bool big = d.n >= 3 || d.kind == PK_TOWER || d.kind == PK_PAD || d.kind == PK_STAIRS || d.kind == PK_PLATSPIKE || d.kind == PK_PADPLAT;
                double wt = d.weight * (big ? 0.5 + 0.9 * double(h.s) : 1.3 - 0.6 * double(h.s));
                wt *= kFam[style][d.family] * bandBoost(h.band, d.family);
                if (!used.empty() && used.back() == p) wt *= 0.25;
                if (used.size() >= 2 && used[used.size() - 2] == p) wt *= 0.5;
                if (used.size() >= 3 && used[used.size() - 3] == p) wt *= 0.7;
                w.push_back(wt);
                sum += wt;
            }
            double r = rng.uni() * sum;
            pick = cand.back();
            for (size_t q = 0; q < cand.size(); ++q) {
                if (r < w[q]) {
                    pick = cand[q];
                    break;
                }
                r -= w[q];
            }
        }
        int pw = patWindow(pick, sp);
        if (pw <= 6 && patDef(pick).kind != PK_PAD) hardUntil = tC + 1.0;
        EventRec e;
        e.tHit = h.t;
        e.tCenter = tC;
        e.x = c.X(tC);
        e.pat = pick;
        e.strength = h.s;
        std::vector<Obj> objs;
        emitPattern(pick, e.x, sp, objs);
        R.events.push_back(e);
        R.evObjs.push_back(std::move(objs));
        used.push_back(pick);
    }

    // --- scoring ---------------------------------------------------------------------------
    int ne = int(R.events.size());
    double evPerSec = ne / dur;
    double rs = 0;
    for (auto const& e : R.events) rs += double(e.strength);
    R.rhythm = ne ? rs / ne : 0.0;
    std::vector<float> strengths;
    for (auto const& h : hits) strengths.push_back(h.s);
    float thr = 0.f;
    if (!strengths.empty()) {
        std::sort(strengths.begin(), strengths.end());
        thr = strengths[size_t(double(strengths.size() - 1) * 0.70)];
    }
    int strong = 0, covered = 0;
    for (auto const& h : hits) {
        if (h.s < thr) continue;
        ++strong;
        for (auto const& e : R.events)
            if (std::fabs(e.tHit - h.t) < 0.03) {
                ++covered;
                break;
            }
    }
    R.coverage = strong ? double(covered) / strong : 0.0;
    R.density = 1.0 - clampd(std::fabs(evPerSec - target) / std::max(target, 0.4), 0.0, 1.0);
    {
        std::vector<int> cnt(size_t(patCount()), 0);
        for (auto const& e : R.events) ++cnt[size_t(e.pat)];
        double H = 0;
        int distinct = 0;
        for (int q : cnt)
            if (q) {
                double p = double(q) / ne;
                H -= p * std::log(p);
                ++distinct;
            }
        // variety counts for more than before: the old generator was criticised for repeating itself
        R.variety = ne >= 3 ? clampd(0.6 * H / std::log(8.0) + 0.4 * std::min(1.0, distinct / 6.0), 0.0, 1.0) : 0.5;
    }
    R.score = 0.34 * R.rhythm + 0.22 * R.coverage + 0.16 * R.density + 0.28 * R.variety;
    return R;
}

// Ask the simulator whether *any* click sequence gets through; fix the spot where it fails and ask again.
bool verifyCube(Ctx const& c, size_t si, SegmentResult& R) {
    int sp = (*c.segs)[si].speed;
    for (int iter = 0; iter < 80; ++iter) {
        SimWorld w;
        for (auto const& v : R.evObjs)
            for (auto const& o : v) w.add(o);
        w.sortAll();
        CubeRun run = runCube(w, R.xs, R.xe, sp);
        if (run.ok) {
            R.verified = true;
            return true;
        }
        int idx = -1;
        double best = 1e18;
        for (size_t i = 0; i < R.events.size(); ++i) {
            if (R.events[i].x - patHalfW(R.events[i].pat, sp) > run.failX + 40.0) continue;  // later events cannot be guilty
            double d = std::fabs(R.events[i].x - run.failX);
            if (d < best) {
                best = d;
                idx = int(i);
            }
        }
        if (idx < 0) {
            R.verified = false;
            return false;
        }
        ++R.repairs;
        if (R.events[size_t(idx)].pat != 0) {  // downgrade to a single spike first
            std::vector<Obj> objs;
            emitPattern(0, R.events[size_t(idx)].x, sp, objs);
            R.evObjs[size_t(idx)] = std::move(objs);
            R.events[size_t(idx)].pat = 0;
        } else {
            R.events.erase(R.events.begin() + idx);
            R.evObjs.erase(R.evObjs.begin() + idx);
        }
    }
    R.verified = false;
    return false;
}

// ---------------------------------------------------------------------------------------------
// ship / ufo / wave corridors
// ---------------------------------------------------------------------------------------------

struct Knot {
    double col, c, g;
    float s;
};

void fixSlopes(std::vector<Knot>& K, double design) {
    for (size_t i = 1; i < K.size(); ++i) {
        double m = design * (K[i].col - K[i - 1].col) * 0.9;
        if (std::fabs(K[i].c - K[i - 1].c) > m) K[i].c = K[i - 1].c + (K[i].c > K[i - 1].c ? m : -m);
    }
    for (size_t i = K.size() - 1; i-- > 0;) {
        double m = design * (K[i + 1].col - K[i].col) * 0.9;
        if (std::fabs(K[i].c - K[i + 1].c) > m) K[i].c = K[i + 1].c + (K[i].c > K[i + 1].c ? m : -m);
    }
}

struct Feature {
    int c0 = 0, c1 = 0;
    std::vector<std::pair<int, Column>> before;
    std::vector<Obj> objs;
};

SegmentResult genFlight(Ctx const& c, size_t si, Rng& rng) {
    SegmentResult R;
    auto const& segs = *c.segs;
    Segment const& sg = segs[si];
    Features const& F = *c.F;
    Profile const& P = *c.P;
    Mode m = sg.mode;
    int sp = sg.speed;
    int sub = sg.style & 3;  // 0 flow (follows the melody), 1 zigzag, 2 stairs, 3 pinch
    bool last = si + 1 == segs.size();
    double xa = c.X(sg.t0) + 90.0;
    double xb = c.X(sg.t1) + (last ? 60.0 : 100.0);
    int n = int((xb - xa) / kBlock);
    R.colX0 = xa;
    if (n < 16) {
        R.verified = true;
        return R;
    }
    double baseGap = m == Mode::Ship ? P.shipGap : (m == Mode::Ufo ? P.ufoGap : P.waveGap);
    int minRows = m == Mode::Ufo ? 4 : 3;
    int minOverlap = m == Mode::Ufo ? 3 : 2;
    double cap = flightCap(m, sp);
    double design = std::min(P.slope * cap, cap * 0.95);
    double margin = m == Mode::Wave ? 0.25 : 0.5;

    double beat = 60.0 / F.bpm;
    double kps = clampd(P.aps * 0.55 * (0.6 + 0.8 * double(sg.intensity)), 0.25, 3.2);
    if (sub == 1) kps = clampd(std::max(kps, 1.5), 0.25, 3.2);
    if (sub == 2) kps *= 0.7;
    int stride = std::max(1, int(std::lround((1.0 / beat) / kps)));
    if (sub == 3) stride = std::min(stride, 2);
    std::vector<double> kt;
    {
        double t = F.nearestBeat(sg.t0 + 0.9);
        while (t < sg.t0 + 0.9) t += beat;
        int bi = 0;
        for (; t < sg.t1 - 0.9; t += beat, ++bi)
            if (bi % stride == 0) kt.push_back(t);
    }
    if (kt.size() < 2) {
        kt.clear();
        kt.push_back(0.5 * (sg.t0 + sg.t1));
    }
    bool inv = rng.chance(0.5);  // melody up = corridor up, or the other way round

    std::vector<int> wallB, wallT;
    std::vector<Column> cols;
    double ampF = 1.0, gapBonus = 0.0;
    bool ok = false;
    int knotCount = 0;
    std::vector<double> centers;
    double rhythmAcc = 0;
    for (int attempt = 0; attempt < 7 && !ok; ++attempt) {
        std::vector<Knot> K;
        K.push_back({0, 1.5, 4.0, 0.f});
        K.push_back({3, 1.5, 4.0, 0.f});
        double cPrev = 1.5;
        int dirPrev = 1;
        centers.clear();
        rhythmAcc = 0;
        knotCount = 0;
        size_t q = 0;
        for (double t : kt) {
            double col = (c.X(t) - xa) / kBlock;
            ++q;
            if (col < 6 || col > n - 9) continue;
            float s = float(clampd(std::max({F.envAt(t - 0.05), F.envAt(t), F.envAt(t + 0.05)}), 0.0, 1.0));
            double I = double(F.intensityAt(t));
            double g = baseGap + gapBonus + 1.3 * (1.0 - I) - 0.6 * (double(s) - 0.5);
            if (sub == 3 && s > 0.6f) g -= 1.6;  // pinch on the strong beats
            g = clampd(g, double(minRows), 8.0);
            double lo = g / 2 - 0.5, hi = 9.5 - g / 2;
            double target;
            if (sub == 0 || sub == 3) {
                double pc = double(F.pitchAt(t));
                if (inv) pc = 1.0 - pc;
                double melody = lo + (hi - lo) * pc;
                if (rng.chance(0.8)) {
                    target = cPrev + (melody - cPrev) * clampd(0.55 + 0.45 * ampF, 0.3, 1.0);
                } else {
                    int dir = -dirPrev;
                    target = cPrev + dir * (0.35 + 0.65 * double(s)) * (hi - lo) * 0.6 * ampF;
                    dirPrev = dir;
                }
            } else if (sub == 1) {
                int dir = -dirPrev;
                double amp = (hi - lo) * (0.22 + 0.22 * double(s)) * ampF;
                target = cPrev + dir * amp;
                dirPrev = dir;
            } else {  // stairs: hold, jump, hold, jump ...
                if (q % 2 == 0) {
                    target = cPrev;
                } else {
                    double pc = double(F.pitchAt(t));
                    if (inv) pc = 1.0 - pc;
                    target = lo + (hi - lo) * clampd(pc + rng.uni(-0.25, 0.25), 0.0, 1.0);
                }
            }
            target = clampd(target, lo, hi);
            K.push_back({col, target, g, s});
            cPrev = target;
            centers.push_back(target);
            rhythmAcc += double(s);
            ++knotCount;
        }
        K.push_back({double(n - 5), 1.5, 4.0, 0.f});
        K.push_back({double(n), 1.5, 4.0, 0.f});
        fixSlopes(K, design);

        auto interpK = [&](double col, double& cc, double& gg) {
            size_t j = 0;
            while (j + 2 < K.size() && K[j + 1].col < col) ++j;
            double a = K[j].col, b = K[j + 1].col;
            double u = b > a ? clampd((col - a) / (b - a), 0.0, 1.0) : 0.0;
            if (sub == 2) u = u < 0.35 ? 0.0 : (u > 0.65 ? 1.0 : (u - 0.35) / 0.30);  // steps instead of ramps
            cc = K[j].c + (K[j + 1].c - K[j].c) * u;
            gg = K[j].g + (K[j + 1].g - K[j].g) * u;
        };
        wallB.assign(size_t(n), 0);
        wallT.assign(size_t(n), 10);
        cols.assign(size_t(n), Column{});
        for (int i = 0; i < n; ++i) {
            double cc, gg;
            interpK(i + 0.5, cc, gg);
            int gi = std::max(minRows, int(std::lround(gg)));
            if (i < 3 || i >= n - 4) {
                gi = 4;
                cc = 1.5;
            }
            int b = int(std::lround(cc - (gi - 1) / 2.0));
            b = std::max(0, std::min(10 - gi, b));
            int t = b + gi;
            if (i > 0) {
                int bp = wallB[size_t(i - 1)], tp = wallT[size_t(i - 1)];
                b = std::max(bp - 1, std::min(bp + 1, b));
                t = std::max(tp - 1, std::min(tp + 1, t));
                if (t - b < minRows) {
                    if (t < 10) t = std::min(10, b + minRows);
                    if (t - b < minRows) b = std::max(0, t - minRows);
                }
                for (int guard = 0; guard < 6 && std::min(t, tp) - std::max(b, bp) < minOverlap; ++guard) {
                    if (b > bp) --b;
                    else if (t < tp) ++t;
                    else break;
                }
            }
            wallB[size_t(i)] = b;
            wallT[size_t(i)] = t;
            cols[size_t(i)] = Column{b, t};
        }
        ok = checkFlight(cols, cap, margin, double(minRows)).ok;
        if (!ok) {
            ampF *= 0.72;
            gapBonus += 0.35;
        }
    }

    // ---- extras: spikes on the walls, teeth rows, floating obstacles (more of them on harder tiers) ----------
    std::vector<Feature> feats;
    std::set<int> occupied;  // columns touched by a feature (+/- 2) so extras never pile up
    auto ov = [](Column const& a, Column const& b) { return std::min(a.t, b.t) - std::max(a.b, b.b); };
    if (P.tierCap >= 2 && ok) {
        double pf = 0.08 + 0.07 * (P.tierCap - 1);
        double lastT = -9;
        for (auto const& o : F.onsets) {
            if (o.t < sg.t0 + 1.8 || o.t > sg.t1 - 1.8 || o.strength < 0.5f || o.t - lastT < 0.9) continue;
            if (!rng.chance(pf)) continue;
            int i = int(std::lround((c.X(o.t) - xa - 15.0) / kBlock));
            if (i < 8 || i > n - 12) continue;
            bool busy = false;
            for (int d = -2; d <= 2; ++d)
                if (occupied.count(i + d)) busy = true;
            if (busy) continue;
            double r = rng.uni();
            int kind = r < 0.38 ? 0 : (r < 0.65 ? 1 : 2);  // 0 spike, 1 teeth, 2 floating obstacle
            Feature f;
            bool applied = false;
            if (kind == 0) {
                Column cl = cols[size_t(i)];
                if (cl.t - cl.b >= minRows + 1) {
                    bool bottom = rng.chance(0.5);
                    Column nc = cl;
                    if (bottom) nc.b += 1;
                    else nc.t -= 1;
                    if (ov(cols[size_t(i - 1)], nc) >= minOverlap && ov(nc, cols[size_t(i + 1)]) >= minOverlap) {
                        Obj sp2;
                        sp2.id = ID_SPIKE;
                        sp2.x = float(xa + 15.0 + kBlock * i);
                        if (bottom) {
                            sp2.y = float(kGroundY + kBlock * cl.b);
                        } else {
                            sp2.y = float(kGroundY + kBlock * (cl.t - 1));
                            sp2.kv = ",6,180";
                        }
                        f.c0 = f.c1 = i;
                        f.before.push_back({i, cl});
                        f.objs.push_back(sp2);
                        cols[size_t(i)] = nc;
                        applied = true;
                    }
                }
            } else if (kind == 1) {
                int len = rng.range(3, 5);
                bool bottom = rng.chance(0.5);
                bool flat = i + len < n - 6;
                for (int k = 0; flat && k < len; ++k) {
                    int x = i + k;
                    if (bottom && wallB[size_t(x)] != wallB[size_t(i)]) flat = false;
                    if (!bottom && wallT[size_t(x)] != wallT[size_t(i)]) flat = false;
                    if (cols[size_t(x)].t - cols[size_t(x)].b < minRows + 1) flat = false;
                }
                if (flat) {
                    std::vector<Column> nc;
                    for (int k = 0; k < len; ++k) {
                        Column cl = cols[size_t(i + k)];
                        if (bottom) cl.b += 1;
                        else cl.t -= 1;
                        nc.push_back(cl);
                    }
                    bool good = ov(cols[size_t(i - 1)], nc.front()) >= minOverlap && ov(nc.back(), cols[size_t(i + len)]) >= minOverlap;
                    if (good) {
                        for (int k = 0; k < len; ++k) {
                            Column cl = cols[size_t(i + k)];
                            Obj sp2;
                            sp2.id = ID_SPIKE;
                            sp2.x = float(xa + 15.0 + kBlock * (i + k));
                            if (bottom) {
                                sp2.y = float(kGroundY + kBlock * cl.b);
                            } else {
                                sp2.y = float(kGroundY + kBlock * (cl.t - 1));
                                sp2.kv = ",6,180";
                            }
                            f.objs.push_back(sp2);
                            f.before.push_back({i + k, cl});
                            cols[size_t(i + k)] = nc[size_t(k)];
                        }
                        f.c0 = i;
                        f.c1 = i + len - 1;
                        applied = true;
                    }
                }
            } else {
                int w = rng.range(1, 2), h = rng.range(1, 2);
                bool room = i + w < n - 8;
                for (int k = 0; room && k < w; ++k)
                    if (cols[size_t(i + k)].t - cols[size_t(i + k)].b < minRows + h + 1) room = false;
                if (room) {
                    // the obstacle sits on one side, detached by one row; the opening stays on the other side
                    bool lowerLane = rng.chance(0.5);
                    std::vector<Column> nc;
                    for (int k = 0; k < w; ++k) {
                        Column cl = cols[size_t(i + k)];
                        Column nn = cl;
                        if (lowerLane) nn.t = cl.t - h - 1;  // lane below, obstacle high: blocks at rows [t-h-1, t-1)
                        else nn.b = cl.b + 1 + h;            // lane above, obstacle low: blocks at rows [b+1, b+1+h)
                        nc.push_back(nn);
                    }
                    bool good = ov(cols[size_t(i - 1)], nc.front()) >= minOverlap && ov(nc.back(), cols[size_t(i + w)]) >= minOverlap;
                    for (auto const& nn : nc)
                        if (nn.t - nn.b < minRows) good = false;
                    if (good) {
                        for (int k = 0; k < w; ++k) {
                            Column cl = cols[size_t(i + k)];
                            int rr = lowerLane ? cl.t - h - 1 : cl.b + 1;
                            for (int hh = 0; hh < h; ++hh) {
                                Obj b;
                                b.id = ID_BLOCK;
                                b.x = float(xa + 15.0 + kBlock * (i + k));
                                b.y = float(kGroundY + kBlock * (rr + hh));
                                f.objs.push_back(b);
                            }
                            f.before.push_back({i + k, cl});
                            cols[size_t(i + k)] = nc[size_t(k)];
                        }
                        f.c0 = i;
                        f.c1 = i + w - 1;
                        applied = true;
                    }
                }
            }
            if (applied) {
                lastT = o.t;
                for (int d = f.c0 - 2; d <= f.c1 + 2; ++d) occupied.insert(d);
                feats.push_back(std::move(f));
            }
        }
        // every extra must keep the corridor flyable; remove the ones around a failure
        for (int guard = 0; guard < 400; ++guard) {
            FlightCheck chk = checkFlight(cols, cap, margin, double(minRows));
            if (chk.ok) break;
            int best = -1, bd = 1 << 30;
            for (size_t q = 0; q < feats.size(); ++q) {
                int d = std::abs((feats[q].c0 + feats[q].c1) / 2 - chk.failCol);
                if (d < bd) {
                    bd = d;
                    best = int(q);
                }
            }
            if (best < 0) break;
            for (auto const& pr : feats[size_t(best)].before) cols[size_t(pr.first)] = pr.second;
            for (int d = feats[size_t(best)].c0 - 2; d <= feats[size_t(best)].c1 + 2; ++d) occupied.erase(d);
            feats.erase(feats.begin() + best);
        }
    }

    // ---- slopes on the 1-row steps of the walls (smooth diagonals instead of stairs) -------------------------
    std::vector<Obj> slopes;
    if (c.S->useSlopes && ok) {
        bool swap = c.S->slopeSwap;
        std::set<int> featCols;
        for (auto const& f : feats)
            for (int d = f.c0 - 1; d <= f.c1 + 1; ++d) featCols.insert(d);
        for (int i = 4; i < n - 5; ++i) {
            if (featCols.count(i) || featCols.count(i - 1)) continue;
            if (cols[size_t(i)].t - cols[size_t(i)].b < minRows + 1 || cols[size_t(i - 1)].t - cols[size_t(i - 1)].b < minRows + 1) continue;
            double xl = xa + 15.0 + kBlock * (i - 1), xr = xa + 15.0 + kBlock * i;
            int db = wallB[size_t(i)] - wallB[size_t(i - 1)];
            int dt = wallT[size_t(i)] - wallT[size_t(i - 1)];
            if (db == 1) slopes.push_back(slopeObj(xl, kGroundY + kBlock * wallB[size_t(i - 1)], 0, swap));       // bottom wall rises
            else if (db == -1) slopes.push_back(slopeObj(xr, kGroundY + kBlock * wallB[size_t(i)], 1, swap));   // bottom wall falls
            if (dt == 1) slopes.push_back(slopeObj(xr, kGroundY + kBlock * wallT[size_t(i - 1)], 3, swap));      // ceiling rises
            else if (dt == -1) slopes.push_back(slopeObj(xl, kGroundY + kBlock * wallT[size_t(i)], 2, swap));    // ceiling falls
        }
    }

    // --- objects ------------------------------------------------------------------------------------------
    const bool deco = c.S->decor != 4;
    for (int i = 0; i < n; ++i) {
        double x = xa + 15.0 + kBlock * i;
        for (int r = 0; r < wallB[size_t(i)]; ++r) {
            Obj o;
            o.id = ID_BLOCK;
            o.x = float(x);
            o.y = float(kGroundY + kBlock * r);
            if (deco) o.kv = (r == wallB[size_t(i)] - 1) ? ",21,1" : ",21,4";  // bright edge row, dim inside
            R.flightObjs.push_back(o);
        }
        for (int r = wallT[size_t(i)]; r <= 10; ++r) {
            Obj o;
            o.id = ID_BLOCK;
            o.x = float(x);
            o.y = float(kGroundY + kBlock * r);
            if (deco) o.kv = (r == wallT[size_t(i)]) ? ",21,1" : ",21,4";
            R.flightObjs.push_back(o);
        }
    }
    for (auto const& f : feats)
        for (auto const& o : f.objs) R.flightObjs.push_back(o);
    for (auto const& o : slopes) R.flightObjs.push_back(o);
    R.slopes = int(slopes.size());
    R.features = int(feats.size());
    R.cols = cols;
    R.knots = knotCount;
    R.verified = ok && checkFlight(cols, cap, margin, double(minRows)).ok;

    double dur = sg.t1 - sg.t0;
    R.rhythm = knotCount ? rhythmAcc / knotCount : 0.0;
    R.density = 1.0 - clampd(std::fabs(knotCount / dur - kps) / std::max(kps, 0.3), 0.0, 1.0);
    if (centers.size() >= 2) {
        double mean = 0, var = 0;
        for (double v : centers) mean += v;
        mean /= double(centers.size());
        for (double v : centers) var += (v - mean) * (v - mean);
        R.variety = clampd(std::sqrt(var / double(centers.size())) / 2.0 + 0.05 * double(feats.size()), 0.0, 1.0);
    } else {
        R.variety = 0.3;
    }
    R.coverage = R.rhythm;
    R.score = 0.45 * R.rhythm + 0.25 * R.density + 0.30 * R.variety;
    return R;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// public helpers
// ---------------------------------------------------------------------------------------------

char const* modeName(Mode m) {
    switch (m) {
        case Mode::Cube: return "Cube";
        case Mode::Ship: return "Ship";
        case Mode::Ufo: return "UFO";
        case Mode::Wave: return "Wave";
    }
    return "?";
}

char const* styleName(Mode m, int style) {
    static char const* cube[4] = {"spike rhythm", "block runs", "pads and air", "tight timing"};
    static char const* fly[4] = {"melody flow", "zigzag", "stairs", "pinch gates"};
    style = std::max(0, std::min(3, style));
    return m == Mode::Cube ? cube[style] : fly[style];
}

Obj slopeObj(double x, double y, int kind, bool swap) {
    // base shape is assumed to be "lower-right solid" (kind 0); the other three are flips of it
    bool fx = (kind == 1 || kind == 3), fy = (kind == 2 || kind == 3);
    if (swap) fx = !fx;
    Obj o;
    o.id = ID_SLOPE;
    o.x = float(x);
    o.y = float(y);
    if (fx) o.kv += ",4,1";
    if (fy) o.kv += ",5,1";
    return o;
}

int modePortalId(Mode m) {
    switch (m) {
        case Mode::Cube: return ID_PORTAL_CUBE;
        case Mode::Ship: return ID_PORTAL_SHIP;
        case Mode::Ufo: return ID_PORTAL_UFO;
        case Mode::Wave: return ID_PORTAL_WAVE;
    }
    return ID_PORTAL_CUBE;
}

int speedPortalId(int s) {
    static const int ids[5] = {ID_SPEED_05, ID_SPEED_1, ID_SPEED_2, ID_SPEED_3, ID_SPEED_4};
    return ids[std::max(0, std::min(4, s))];
}

char const* speedName(int s) {
    static char const* n[5] = {"0.5x", "1x", "2x", "3x", "4x"};
    return n[std::max(0, std::min(4, s))];
}

std::string mmss(double t) {
    int s = int(t + 0.5);
    char b[24];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

double SpeedMap::x(double tau) const {
    if (k.empty()) return tau * kSpeed[1];
    size_t j = 0;
    while (j + 1 < k.size() && k[j + 1].t <= tau) ++j;
    return k[j].x + (tau - k[j].t) * k[j].v;
}

// ---------------------------------------------------------------------------------------------
// orchestration
// ---------------------------------------------------------------------------------------------

Level generateLevel(Features const& F, Profile const& P, Settings const& S, double length, uint64_t seed, Hooks const& H) {
    Level L;
    L.shift = S.syncOffsetSec;
    auto log = [&](std::string const& s) {
        if (H.log) H.log(s);
    };
    auto cancelled = [&] { return H.cancelled && H.cancelled(); };
    Rng planRng(mix64(seed ^ 0xA11CEull));
    L.segs = planSegments(F, P, length, planRng, H.log);
    L.map = buildSpeedMap(L.segs, L.shift);
    Ctx ctx{&F, &P, &S, &L.segs, &L.map, L.shift};

    static const int kVariants[6] = {8, 8, 40, 200, 700, 3000};
    static const int kVerifyTop[6] = {2, 2, 4, 8, 20, 60};
    int depth = std::max(1, std::min(5, S.depth));
    int K = kVariants[depth], topM = kVerifyTop[depth];
    L.res.resize(L.segs.size());
    log("composing: " + std::to_string(K) + " variants per segment, the " + std::to_string(topM) + " best get simulated");

    for (size_t si = 0; si < L.segs.size(); ++si) {
        if (cancelled()) return L;
        bool cube = L.segs[si].mode == Mode::Cube;
        // keep only the `topM` best-scoring variants in memory (a flight variant holds thousands of objects)
        std::vector<SegmentResult> cands;
        for (int k = 0; k < K; ++k) {
            Rng r(mix64(seed ^ (uint64_t(si) * 7919ull + uint64_t(k) * 104729ull + 17ull)));
            SegmentResult R = cube ? genCube(ctx, si, r) : genFlight(ctx, si, r);
            if (int(cands.size()) < topM) {
                cands.push_back(std::move(R));
            } else {
                size_t wi = 0;
                for (size_t q = 1; q < cands.size(); ++q)
                    if (cands[q].score < cands[wi].score) wi = q;
                if (R.score > cands[wi].score) cands[wi] = std::move(R);
            }
            if (H.progress && (k & 7) == 0) H.progress((double(si) + double(k) / K) / double(L.segs.size()));
            if ((k & 15) == 15 && cancelled()) return L;
        }
        std::stable_sort(cands.begin(), cands.end(), [](SegmentResult const& a, SegmentResult const& b) { return a.score > b.score; });
        int tryN = int(cands.size());
        int bestI = -1;
        double bestAdj = -1e9;
        for (int q = 0; q < tryN; ++q) {
            SegmentResult& R = cands[size_t(q)];
            if (cube) verifyCube(ctx, si, R);
            double adj = R.score - 0.03 * R.repairs - (R.verified ? 0.0 : 5.0);
            if (adj > bestAdj) {
                bestAdj = adj;
                bestI = q;
            }
        }
        if (bestI < 0) bestI = 0;
        SegmentResult best = std::move(cands[size_t(bestI)]);
        best.variants = K;
        Segment const& sg = L.segs[si];
        char b[300];
        if (cube) {
            std::string rep = best.repairs ? ", " + std::to_string(best.repairs) + " repairs" : "";
            std::snprintf(b, sizeof b, "  segment %zu  %s @ %s (%s): %d variants, simulated %d -> %zu obstacles, score %.2f (rhythm %.2f, variety %.2f)%s", si + 1, modeName(sg.mode),
                          speedName(sg.speed), styleName(sg.mode, sg.style), K, tryN, best.events.size(), best.score, best.rhythm, best.variety, rep.c_str());
        } else {
            int mn = 99;
            for (auto const& cl : best.cols) mn = std::min(mn, cl.t - cl.b);
            std::snprintf(b, sizeof b, "  segment %zu  %s @ %s (%s): %d variants -> %d turns, %d slopes, %d extras, narrowest opening %d rows, %s, score %.2f", si + 1, modeName(sg.mode),
                          speedName(sg.speed), styleName(sg.mode, sg.style), K, best.knots, best.slopes, best.features, mn == 99 ? 0 : mn, best.verified ? "flyable" : "NOT verified",
                          best.score);
        }
        log(b);
        L.res[si] = std::move(best);
    }

    // --- assemble --------------------------------------------------------------------------------
    auto portal = [&](int id, double x) {
        Obj o;
        o.id = id;
        o.x = float(x);
        o.y = float(kPortalY);
        L.objs.push_back(o);
    };
    std::set<int> patternsUsed;
    for (size_t si = 0; si < L.segs.size(); ++si) {
        Segment const& sg = L.segs[si];
        if (si == 0) {
            if (sg.speed != 1) portal(speedPortalId(sg.speed), 150.0);
        } else {
            Segment const& pv = L.segs[si - 1];
            double x0 = L.X(sg.t0);
            if (sg.speed != pv.speed) portal(speedPortalId(sg.speed), x0);
            if (sg.mode != pv.mode) portal(modePortalId(sg.mode), x0 + 75.0);
        }
        SegmentResult const& R = L.res[si];
        if (sg.mode == Mode::Cube) {
            for (auto const& v : R.evObjs)
                for (auto const& o : v) L.objs.push_back(o);
            for (auto const& e : R.events) {
                ++L.totalEvents;
                patternsUsed.insert(e.pat);
                if (patDef(e.pat).kind != PK_PAD) {
                    int win = patWindow(e.pat, sg.speed);
                    L.minWindowUsed = std::min(L.minWindowUsed, win);
                    if (win <= 4) ++L.tightEvents;
                }
            }
        } else {
            ++L.flightSegs;
            if (R.verified) ++L.flightOk;
            for (auto const& o : R.flightObjs) L.objs.push_back(o);
            L.slopes += R.slopes;
            L.features += R.features;
        }
        L.repairs += R.repairs;
    }
    L.distinctPatterns = int(patternsUsed.size());
    L.endT = length;
    L.endX = L.X(length) + 100.0;
    if (H.progress) H.progress(1.0);
    return L;
}

}  // namespace ais
