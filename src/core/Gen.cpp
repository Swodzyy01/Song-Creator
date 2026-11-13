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

// ---------------------------------------------------------------------------------------------
// planning
// ---------------------------------------------------------------------------------------------

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
        if (D > 38.0) {
            int pieces = int(std::ceil(D / 28.0));
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
    int prevSpeed = 1;
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
            w[int(prevMode)] *= 0.35;
            w[0] *= 1.4;
        }
        double sum = w[0] + w[1] + w[2] + w[3];
        double r = rng.uni() * sum;
        int pick = 0;
        for (int k = 0; k < 4; ++k) {
            if (r < w[k]) {
                pick = k;
                break;
            }
            r -= w[k];
            pick = k;
        }
        s.mode = Mode(pick);

        int smin = P.speedMin, smax = P.speedMax;
        double u = clampd(double(s.intensity) * 1.15 - 0.10 + rng.uni(-0.12, 0.12), 0.0, 1.0);
        int sp = smin + int(std::lround(u * (smax - smin)));
        if (s.kind == SecKind::Intro || s.kind == SecKind::Outro || s.kind == SecKind::Break) sp = std::max(smin, sp - 1);
        if (s.kind == SecKind::Drop && rng.chance(0.5)) sp = std::min(smax, sp + 1);
        if (i > 0 && std::abs(sp - prevSpeed) > 1 && s.kind != SecKind::Drop) sp = prevSpeed + (sp > prevSpeed ? 1 : -1);
        if (i == 0) sp = std::min(sp, 2);
        s.speed = int(clampd(sp, 0, 4));
        prevMode = s.mode;
        prevSpeed = s.speed;
        segs.push_back(s);
    }
    if (log) {
        for (size_t i = 0; i < segs.size(); ++i) {
            char b[200];
            std::snprintf(b, sizeof b, "  segment %zu/%zu  %s-%s  %-9s intensity %.2f  ->  %s @ %s", i + 1, segs.size(), mmss(segs[i].t0).c_str(), mmss(segs[i].t1).c_str(),
                          secName(segs[i].kind), double(segs[i].intensity), modeName(segs[i].mode), speedName(segs[i].speed));
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
    for (size_t k = 0; k < chosen.size(); ++k) {
        Hit const& h = hits[size_t(chosen[k])];
        double tC = h.t + lead;
        double dtPrev = R.events.empty() ? 1e9 : tC - R.events.back().tCenter;
        double dtNext = (k + 1 < chosen.size()) ? (hits[size_t(chosen[k + 1])].t + lead) - tC : 1e9;
        std::vector<int> cand;
        for (int p = 0; p < patCount(); ++p) {
            if (patDef(p).tier > P.tierCap) continue;
            if (patWindow(p, sp) < P.minWindow) continue;
            if (!R.events.empty() && minDt(R.events.back().pat, p, sp, P) > dtPrev) continue;
            if (minDt(p, 0, sp, P) > dtNext) continue;
            cand.push_back(p);
        }
        if (cand.empty()) continue;
        int pick = -1;
        if (used.size() >= 4 && rng.chance(0.30)) {  // repeat the motif from one bar ago
            int m = used[used.size() - 4];
            if (contains(cand, m)) pick = m;
        }
        if (pick < 0 && P.fp && rng.chance(P.fpChance)) {  // frame-perfect style: take the narrowest windows on offer
            std::vector<int> tight;
            for (int p : cand)
                if (patWindow(p, sp) <= 4 && patDef(p).kind != PK_PAD) tight.push_back(p);
            if (!tight.empty()) pick = tight[size_t(rng.range(0, int(tight.size()) - 1))];
        }
        if (pick < 0) {
            std::vector<double> w;
            double sum = 0;
            for (int p : cand) {
                PatDef const& d = patDef(p);
                bool big = d.n >= 3 || d.kind == PK_TOWER || d.kind == PK_PAD;
                double wt = d.weight * (big ? 0.5 + 0.9 * double(h.s) : 1.3 - 0.6 * double(h.s));
                if (!used.empty() && used.back() == p) wt *= 0.35;
                if (used.size() >= 2 && used[used.size() - 2] == p) wt *= 0.6;
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
        for (int q : cnt)
            if (q) {
                double p = double(q) / ne;
                H -= p * std::log(p);
            }
        R.variety = ne >= 3 ? clampd(H / std::log(6.0), 0.0, 1.0) : 0.5;
    }
    R.score = 0.40 * R.rhythm + 0.25 * R.coverage + 0.20 * R.density + 0.15 * R.variety;
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
            return false;  // failure with no obstacle nearby: something is wrong with the entry; give up on this candidate
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

SegmentResult genFlight(Ctx const& c, size_t si, Rng& rng) {
    SegmentResult R;
    auto const& segs = *c.segs;
    Segment const& sg = segs[si];
    Features const& F = *c.F;
    Profile const& P = *c.P;
    Mode m = sg.mode;
    int sp = sg.speed;
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
    int minRows = m == Mode::Ship ? 3 : (m == Mode::Ufo ? 4 : 2);
    int minOverlap = m == Mode::Ufo ? 3 : (m == Mode::Ship ? 2 : (baseGap >= 3.0 ? 2 : 1));
    double cap = flightCap(m, sp);
    double design = std::min(P.slope * cap, cap * 0.95);
    double margin = m == Mode::Wave ? 0.25 : 0.5;

    double beat = 60.0 / F.bpm;
    double kps = clampd(P.aps * 0.55 * (0.6 + 0.8 * double(sg.intensity)), 0.25, 3.2);
    int stride = std::max(1, int(std::lround((1.0 / beat) / kps)));
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
        for (double t : kt) {
            double col = (c.X(t) - xa) / kBlock;
            if (col < 6 || col > n - 9) continue;
            float s = float(clampd(std::max({F.envAt(t - 0.05), F.envAt(t), F.envAt(t + 0.05)}), 0.0, 1.0));
            double I = double(F.intensityAt(t));
            double g = clampd(baseGap + gapBonus + 1.3 * (1.0 - I) - 0.6 * (double(s) - 0.5), double(minRows), 8.0);
            double lo = g / 2 - 0.5, hi = 9.5 - g / 2;
            int dir = rng.chance(0.8) ? -dirPrev : (rng.chance(0.5) ? 1 : -1);
            double amp = (0.35 + 0.65 * double(s)) * (hi - lo) * (0.5 + 0.5 * I) * ampF;
            double target = clampd(cPrev + dir * amp, lo, hi);
            K.push_back({col, target, g, s});
            cPrev = target;
            dirPrev = dir;
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

    // teeth on the walls for the harder tiers (aligned with strong hits)
    std::vector<Obj> spikes;
    if (P.tierCap >= 3 && ok) {
        double pSpike = 0.10 + 0.10 * (P.tierCap - 3);
        std::set<int> spikeCols;
        auto ov = [](Column const& a, Column const& b) { return std::min(a.t, b.t) - std::max(a.b, b.b); };
        struct Placed {
            int col;
            Obj o;
            Column before;
        };
        std::vector<Placed> placed;
        for (auto const& o : F.onsets) {
            if (o.t < sg.t0 + 1.5 || o.t > sg.t1 - 1.5 || o.strength < 0.55f) continue;
            if (!rng.chance(pSpike)) continue;
            int i = int(std::lround((c.X(o.t) - xa - 15.0) / kBlock));
            if (i < 8 || i > n - 10) continue;
            if (spikeCols.count(i - 1) || spikeCols.count(i) || spikeCols.count(i + 1)) continue;
            Column cl = cols[size_t(i)];
            if (cl.t - cl.b < minRows + 1) continue;
            bool bottom = rng.chance(0.5);
            Column nc = cl;
            if (bottom) nc.b += 1;
            else nc.t -= 1;
            if (ov(cols[size_t(i - 1)], nc) < minOverlap || ov(nc, cols[size_t(i + 1)]) < minOverlap) continue;
            Obj sp2;
            sp2.id = ID_SPIKE;
            sp2.x = float(xa + 15.0 + kBlock * i);
            if (bottom) {
                sp2.y = float(kGroundY + kBlock * cl.b);
            } else {
                sp2.y = float(kGroundY + kBlock * (cl.t - 1));
                sp2.kv = ",6,180";
            }
            placed.push_back({i, sp2, cl});
            cols[size_t(i)] = nc;
            spikeCols.insert(i);
        }
        // every spike must keep the corridor flyable; drop the ones around a failure
        for (int guard = 0; guard < 200; ++guard) {
            FlightCheck chk = checkFlight(cols, cap, margin, double(minRows));
            if (chk.ok) break;
            int best = -1, bd = 1 << 30;
            for (size_t q = 0; q < placed.size(); ++q) {
                int d = std::abs(placed[q].col - chk.failCol);
                if (d < bd) {
                    bd = d;
                    best = int(q);
                }
            }
            if (best < 0) break;
            cols[size_t(placed[size_t(best)].col)] = placed[size_t(best)].before;
            placed.erase(placed.begin() + best);
        }
        for (auto const& p : placed) spikes.push_back(p.o);
    }

    // --- objects ------------------------------------------------------------------------------
    for (int i = 0; i < n; ++i) {
        double x = xa + 15.0 + kBlock * i;
        for (int r = 0; r < wallB[size_t(i)]; ++r) {
            Obj o;
            o.id = ID_BLOCK;
            o.x = float(x);
            o.y = float(kGroundY + kBlock * r);
            R.flightObjs.push_back(o);
        }
        for (int r = wallT[size_t(i)]; r <= 10; ++r) {
            Obj o;
            o.id = ID_BLOCK;
            o.x = float(x);
            o.y = float(kGroundY + kBlock * r);
            R.flightObjs.push_back(o);
        }
    }
    for (auto const& s : spikes) R.flightObjs.push_back(s);
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
        R.variety = clampd(std::sqrt(var / double(centers.size())) / 2.0, 0.0, 1.0);
    } else {
        R.variety = 0.3;
    }
    R.coverage = R.rhythm;
    R.score = 0.50 * R.rhythm + 0.30 * R.density + 0.20 * R.variety;
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
    Ctx ctx{&F, &P, &L.segs, &L.map, L.shift};

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
        char b[260];
        if (cube) {
            std::snprintf(b, sizeof b, "  segment %zu  %s @ %s: %d variants, simulated %d -> %zu obstacles, score %.2f (rhythm %.2f, coverage %.2f, density %.2f, variety %.2f)%s", si + 1,
                          modeName(sg.mode), speedName(sg.speed), K, tryN, best.events.size(), best.score, best.rhythm, best.coverage, best.density, best.variety,
                          best.repairs ? (", " + std::to_string(best.repairs) + " repairs").c_str() : "");
        } else {
            std::snprintf(b, sizeof b, "  segment %zu  %s @ %s: %d variants -> %d direction changes, narrowest opening %d rows, %s, score %.2f", si + 1, modeName(sg.mode),
                          speedName(sg.speed), K, best.knots, [&] {
                              int mn = 99;
                              for (auto const& cl : best.cols) mn = std::min(mn, cl.t - cl.b);
                              return mn == 99 ? 0 : mn;
                          }(),
                          best.verified ? "flyable" : "NOT verified", best.score);
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
        }
        L.repairs += R.repairs;
    }
    L.endT = length;
    L.endX = L.X(length) + 100.0;
    if (H.progress) H.progress(1.0);
    return L;
}

}  // namespace ais
