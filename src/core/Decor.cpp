#include "Decor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "LevelString.hpp"
#include "Rng.hpp"

namespace ais {

namespace {

double clampd(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }

Col hsv(double h, double s, double v) {
    h -= std::floor(h);
    s = clampd(s, 0, 1);
    v = clampd(v, 0, 1);
    int i = int(h * 6.0);
    double f = h * 6.0 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    double r = v, g = t, b = p;
    switch (i % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = q; break;
    }
    return Col{int(r * 255.0 + 0.5), int(g * 255.0 + 0.5), int(b * 255.0 + 0.5)};
}

Col mixc(Col a, Col b, double t) {
    t = clampd(t, 0, 1);
    return Col{int(a.r + (b.r - a.r) * t + 0.5), int(a.g + (b.g - a.g) * t + 0.5), int(a.b + (b.b - a.b) * t + 0.5)};
}

struct KP {
    double vbg, vgr, dh, sat;
};
KP kindParams(SecKind k) {
    switch (k) {
        case SecKind::Intro: return {0.10, 0.22, 0.00, 0.95};
        case SecKind::Verse: return {0.17, 0.30, 0.01, 0.95};
        case SecKind::Build: return {0.26, 0.40, 0.03, 0.90};
        case SecKind::Drop: return {0.42, 0.55, 0.06, 0.80};
        case SecKind::Break: return {0.12, 0.22, -0.05, 0.85};
        case SecKind::Outro: return {0.10, 0.22, 0.00, 0.95};
    }
    return {0.17, 0.30, 0.0, 0.9};
}

struct Look {
    Col bg, ground, obj, accent, line;
    Col d2, d3, d4;
    double hue = 0;
    double sat = 0.9;
};

double baseHue(int style, Features const& F) {
    switch (style) {
        case 0: return 0.0;    // hell: red / orange
        case 1: return 0.36;   // green
        case 2: return 0.135;  // yellow
        default: return 0.02 + 0.55 * double(F.centroid);  // dark songs -> warm, bright songs -> cold
    }
}

Look lookFor(int style, Features const& F, SecKind k, int idx, float intensity) {
    KP p = kindParams(k);
    double h = baseHue(style, F) + p.dh;
    double heat = 1.0;
    if (style == 3) {
        h += 0.07 * idx;  // the palette travels round the colour wheel while the song progresses
        heat = 0.8 + 0.4 * double(intensity);
    }
    Look L;
    L.hue = h;
    L.sat = p.sat;
    L.bg = hsv(h, p.sat, clampd(p.vbg * heat, 0.03, 0.8));
    L.ground = hsv(h, p.sat * 0.95, clampd(p.vgr * heat, 0.05, 0.9));
    L.obj = hsv(h + 0.04 + (k == SecKind::Drop ? 0.04 : 0.0), style == 3 ? 0.55 : 0.70, 1.0);
    L.accent = hsv(h + 0.08, 0.90, 1.0);
    L.line = hsv(h + 0.06, 0.35, 1.0);
    L.d2 = hsv(h + 0.15, 0.80, 0.90);
    L.d3 = hsv(h - 0.10, 0.80, 0.80);
    L.d4 = hsv(h + 0.30, 0.70, 0.70);
    return L;
}

Obj plain(int id, double x, double y, std::string kv = "") {
    Obj o;
    o.id = id;
    o.x = float(x);
    o.y = float(y);
    o.kv = std::move(kv);
    return o;
}

void emitColor(std::vector<Obj>& out, double x, int chan, Col c, double dur, double opacity = 1.0) {
    out.push_back(plain(ID_TRIGGER_COLOR, x, 465.0,
                        ",7," + std::to_string(c.r) + ",8," + std::to_string(c.g) + ",9," + std::to_string(c.b) + ",10," + fmtNum(dur) + ",23," + std::to_string(chan) + ",35," +
                            fmtNum(opacity) + ",36,1"));
}

void emitShake(std::vector<Obj>& out, double x, double strength, double dur) {
    out.push_back(plain(ID_SHAKE, x, 495.0, ",10," + fmtNum(dur) + ",75," + fmtNum(strength) + ",76,0,36,1"));
}

}  // namespace

char const* decorName(int style) {
    static char const* n[] = {"Hell", "Green", "Yellow", "Music-based", "None (layout)"};
    return (style >= 0 && style < 5) ? n[style] : "?";
}

DecorStats decorate(Level& lv, Features const& F, Settings const& S, Palette& pal, uint64_t seed) {
    DecorStats st;
    int style = std::max(0, std::min(4, S.decor));
    if (style == 4 || lv.segs.empty()) {
        pal.layout = true;
        pal.bg = Col{28, 28, 34};
        pal.ground = Col{52, 52, 62};
        pal.ground2 = pal.ground;
        pal.obj = Col{255, 255, 255};
        pal.line = Col{255, 255, 255};
        pal.d1 = pal.d2 = pal.d3 = pal.d4 = Col{255, 255, 255};
        return st;
    }
    Rng rng(mix64(seed ^ 0xDEC0DEull));
    double beat = 60.0 / F.bpm;
    bool swap = S.slopeSwap;

    std::vector<Look> looks;
    for (size_t i = 0; i < lv.segs.size(); ++i) looks.push_back(lookFor(style, F, lv.segs[i].kind, int(i), lv.segs[i].intensity));
    auto segIndexAt = [&](double t) {
        for (size_t i = 0; i < lv.segs.size(); ++i)
            if (t >= lv.segs[i].t0 && t < lv.segs[i].t1) return i;
        return lv.segs.size() - 1;
    };

    Look const& L0 = looks[0];
    pal.bg = L0.bg;
    pal.ground = L0.ground;
    pal.ground2 = mixc(L0.ground, L0.bg, 0.5);
    pal.obj = L0.obj;
    pal.line = L0.line;
    pal.d1 = L0.accent;
    pal.d2 = L0.d2;
    pal.d3 = L0.d3;
    pal.d4 = L0.d4;
    pal.d4a = 0.35f;

    std::vector<Obj>& out = lv.objs;
    size_t before = out.size();

    // --- palette change at every segment boundary + a big hit on every drop -------------------------------------
    double fade = clampd(beat * 4.0, 0.8, 2.5);
    for (size_t i = 1; i < lv.segs.size(); ++i) {
        double x = lv.X(lv.segs[i].t0);
        Look const& L = looks[i];
        bool dropHit = lv.segs[i].kind == SecKind::Drop && lv.segs[i - 1].kind != SecKind::Drop;
        if (dropHit) {  // white flash that decays into the drop palette, and the screen shakes
            emitColor(out, x - 6.0, 1000, Col{255, 255, 255}, 0.0);
            emitColor(out, x - 2.0, 1001, mixc(L.ground, Col{255, 255, 255}, 0.6), 0.0);
            emitColor(out, x + 4.0, 1000, L.bg, 0.9);
            emitColor(out, x + 4.0, 1001, L.ground, 1.1);
            emitShake(out, x, 1.6, 0.55);
        } else {
            emitColor(out, x, 1000, L.bg, fade);
            emitColor(out, x, 1001, L.ground, fade);
        }
        emitColor(out, x, 1009, mixc(L.ground, L.bg, 0.5), fade);
        emitColor(out, x, 1004, L.obj, fade * 0.6);
        emitColor(out, x, 1002, L.line, fade);
        emitColor(out, x, 1, L.accent, fade, pal.d1a);
        emitColor(out, x, 2, L.d2, fade, pal.d2a);
        emitColor(out, x, 3, L.d3, fade, pal.d3a);
        emitColor(out, x, 4, L.d4, fade, pal.d4a);
        ++st.sectionChanges;
    }

    // --- the accent colours drift every bar so nothing ever sits still ---------------------------------------------
    {
        double bar = beat * 4.0;
        int bi = 1;
        for (double t = F.beatOffset + bar; t < lv.endT - 1.0; t += bar, ++bi) {
            if (t < 2.0) continue;
            size_t si = segIndexAt(t);
            Look const& L = looks[si];
            double amp = style == 3 ? 0.06 : 0.035;
            double x = lv.X(t);
            double d = bar * 0.95;
            emitColor(out, x, 1, hsv(L.hue + 0.08 + amp * std::sin(bi * 0.55), 0.90, 1.0), d, pal.d1a);
            emitColor(out, x, 2, hsv(L.hue + 0.15 + amp * std::sin(bi * 0.55 + 1.3), 0.80, 0.90), d, pal.d2a);
            emitColor(out, x, 3, hsv(L.hue - 0.10 + amp * std::sin(bi * 0.55 + 2.6), 0.80, 0.80), d, pal.d3a);
        }
    }

    // --- pulses on the beat ---------------------------------------------------------------------------------------
    double fb = clampd(beat * 0.9, 0.18, 0.45);
    double lastBass = -1, lastMid = -1;
    for (auto const& o : F.onsets) {
        if (o.t < 1.0 || o.t > lv.endT - 0.5) continue;
        double t = F.nearestBeat(o.t);
        if (std::fabs(t - o.t) > 0.045) t = o.t;
        size_t si = segIndexAt(t);
        Look const& L = looks[si];
        double x = lv.X(t);
        if (o.band == 0 && o.strength >= 0.50f && t - lastBass >= 0.22) {
            lastBass = t;
            double k = 0.40 + 0.45 * double(o.strength);
            Col fbg = mixc(L.bg, hsv(L.hue + 0.03, 0.85, 1.0), k);
            Col fgr = mixc(L.ground, hsv(L.hue + 0.03, 0.80, 1.0), k * 0.7);
            emitColor(out, x, 1000, fbg, 0.0);
            emitColor(out, x, 1001, fgr, 0.0);
            emitColor(out, x + 4.0, 1000, L.bg, fb);
            emitColor(out, x + 4.0, 1001, L.ground, fb * 1.2);
            ++st.pulses;
        } else if (o.band == 1 && o.strength >= 0.55f && t - lastMid >= 0.30) {
            lastMid = t;
            Col fob = mixc(L.obj, Col{255, 255, 255}, 0.35 + 0.35 * double(o.strength));
            emitColor(out, x, 1004, fob, 0.0);
            emitColor(out, x + 4.0, 1004, L.obj, fb * 0.9);
            ++st.pulses;
        }
    }

    // --- decoration above the playfield of grounded sections (rows 8-10: no cube can reach them) --------------------
    const double rowTop = kGroundY + kBlock * 10;
    double lastBar = -9;
    double lastEdge = -9;
    for (auto const& o : F.onsets) {
        if (o.band != 0 || o.strength < 0.60f) continue;
        size_t si = segIndexAt(o.t);
        Segment const& sg = lv.segs[si];
        if (sg.mode != Mode::Cube || o.t < sg.t0 + 0.6 || o.t > sg.t1 - 0.4 || o.t - lastBar < 0.6) continue;
        lastBar = o.t;
        double x = lv.X(o.t);
        int len = 1 + int(double(o.strength) * 2.99);
        if (style == 1) len += 1;  // vines hang lower
        for (int k = 0; k < len; ++k) out.push_back(plain(ID_BLOCK, x, rowTop - kBlock * k, style == 1 ? ",21,2" : ",21,1"));
        double tip = rowTop - kBlock * len;
        if (style == 0) {  // hell: stalactite + now and then a spinning saw
            if (len >= 2) out.push_back(plain(ID_SPIKE, x, tip, ",6,180,21,1"));
            if (o.strength > 0.8f && rng.chance(0.5)) out.push_back(plain(rng.chance(0.5) ? ID_SAW_MED : ID_SAW_SMALL, x + 45.0, rowTop - kBlock * 0.5, ",21,2"));
        } else if (style == 1) {  // green: vines end in a leaf
            out.push_back(slopeObj(x, tip, 2, swap));
            out.push_back(slopeObj(x + kBlock, tip, 3, swap));
        } else if (style == 2) {  // yellow: hanging lights, a dotted line
            out.push_back(plain(ID_BLOCK, x, tip - kBlock, ",21,2"));
            out.push_back(plain(ID_BLOCK, x, tip - kBlock * 3, ",21,3"));
        } else {  // music: pointed tips, saws on the loudest hits
            out.push_back(slopeObj(x, tip, 2, swap));
            out.push_back(slopeObj(x - kBlock, tip, 3, swap));
            if (o.strength > 0.85f) out.push_back(plain(ID_SAW_SMALL, x, tip - kBlock * 1.5, ",21,3"));
        }
        ++st.bars;
    }
    // zigzag ceiling edge in grounded drops: a row of downward triangles on the beat
    for (size_t si = 0; si < lv.segs.size(); ++si) {
        Segment const& sg = lv.segs[si];
        if (sg.mode != Mode::Cube || sg.kind != SecKind::Drop) continue;
        double bar = beat * 4.0;
        for (double t = F.nearestBeat(sg.t0 + 1.5); t < sg.t1 - 1.0; t += bar) {
            if (t - lastEdge < bar * 0.9) continue;
            lastEdge = t;
            double x = lv.X(t);
            for (int k = 0; k < 4; ++k) {
                Obj a = slopeObj(x + 2 * k * kBlock, rowTop, 3, swap);
                Obj b = slopeObj(x + (2 * k + 1) * kBlock, rowTop, 2, swap);
                a.kv += ",21,2";
                b.kv += ",21,2";
                out.push_back(a);
                out.push_back(b);
            }
            ++st.bars;
        }
    }
    (void)rng;
    st.triggers = 0;
    for (size_t i = before; i < out.size(); ++i)
        if (out[i].id == ID_TRIGGER_COLOR || out[i].id == ID_SHAKE) ++st.triggers;
    return st;
}

}  // namespace ais
