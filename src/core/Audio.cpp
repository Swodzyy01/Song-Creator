#include "Audio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace ais {

namespace {

constexpr int kN = 1024;  // STFT window
constexpr int kH = 512;   // STFT hop
constexpr double kPi = 3.14159265358979323846;
// Measured with the synthetic click tracks in tests/: how late (s) the flux peak lands vs the true onset.
constexpr double kOnsetBias = 0.0003;

float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

float percentile(std::vector<float> v, double p) {
    if (v.empty()) return 0.f;
    size_t k = size_t(std::min(1.0, std::max(0.0, p)) * double(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + long(k), v.end());
    return v[k];
}

std::vector<float> movAvg(std::vector<float> const& x, int r) {
    size_t n = x.size();
    std::vector<double> ps(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) ps[i + 1] = ps[i] + double(x[i]);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        size_t a = i > size_t(r) ? i - size_t(r) : 0;
        size_t b = std::min(n, i + size_t(r) + 1);
        out[i] = float((ps[b] - ps[a]) / double(b - a));
    }
    return out;
}

struct FFT {
    std::vector<float> c, s;
    std::vector<int> rev;
    FFT() : c(kN / 2), s(kN / 2), rev(kN) {
        for (int i = 0; i < kN / 2; ++i) {
            c[size_t(i)] = float(std::cos(2 * kPi * i / kN));
            s[size_t(i)] = float(-std::sin(2 * kPi * i / kN));
        }
        int bits = 0;
        while ((1 << bits) < kN) ++bits;
        for (int i = 0; i < kN; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[size_t(i)] = r;
        }
    }
    void run(float* re, float* im) const {
        for (int i = 0; i < kN; ++i) {
            int j = rev[size_t(i)];
            if (j > i) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        for (int len = 2; len <= kN; len <<= 1) {
            int half = len >> 1, step = kN / len;
            for (int i = 0; i < kN; i += len) {
                for (int k = 0; k < half; ++k) {
                    float wr = c[size_t(k * step)], wi = s[size_t(k * step)];
                    int a = i + k, b = a + half;
                    float tr = re[b] * wr - im[b] * wi;
                    float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }
};

struct Frames {
    std::vector<float> fb, fm, fh;  // spectral flux per band
    std::vector<float> eb, em, eh;  // band energy
    std::vector<float> rms, cent;
    int sr = 22050;
    size_t count() const { return rms.size(); }
};

// Mono, decimated to ~22 kHz, at most maxSec long.
std::vector<float> prepare(PCM const& pcm, double maxSec, int& outSr) {
    size_t limit = pcm.mono.size();
    if (maxSec > 0) limit = std::min(limit, size_t(maxSec * pcm.sampleRate));
    int factor = pcm.sampleRate >= 32000 ? std::max(1, int(std::lround(pcm.sampleRate / 22050.0))) : 1;
    outSr = pcm.sampleRate / factor;
    size_t n = limit / size_t(factor);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        double a = 0;
        for (int k = 0; k < factor; ++k) a += double(pcm.mono[i * size_t(factor) + size_t(k)]);
        out[i] = float(a / factor);
    }
    return out;
}

Frames stft(std::vector<float> const& x, int sr, ProgressFn const& prog, CancelFn const& cancelled) {
    FFT fft;
    Frames F;
    F.sr = sr;
    int n = int(x.size());
    int frames = n >= kN ? (n - kN) / kH + 1 : 0;
    std::vector<float> win(kN);
    for (int i = 0; i < kN; ++i) win[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * kPi * i / (kN - 1)));
    double binHz = double(sr) / kN;
    int b0 = std::max(1, int(30.0 / binHz)), b1 = int(150.0 / binHz) + 1, b2 = int(2000.0 / binHz) + 1;
    int b3 = std::min(kN / 2, int(10000.0 / binHz) + 1);
    std::vector<float> re(kN), im(kN), prevL(kN / 2, 0.f);
    for (auto* v : {&F.fb, &F.fm, &F.fh, &F.eb, &F.em, &F.eh, &F.rms, &F.cent}) v->reserve(size_t(frames));
    for (int f = 0; f < frames; ++f) {
        if (cancelled && (f & 255) == 0 && cancelled()) break;
        float const* p = &x[size_t(f) * kH];
        double sq = 0;
        for (int i = 0; i < kN; ++i) {
            re[size_t(i)] = p[i] * win[size_t(i)];
            im[size_t(i)] = 0.f;
            sq += double(p[i]) * double(p[i]);
        }
        fft.run(re.data(), im.data());
        double e[3] = {0, 0, 0}, fl[3] = {0, 0, 0};
        double sumMag = 0, sumKMag = 0;
        for (int k = 1; k < kN / 2; ++k) {
            float mag = std::sqrt(re[size_t(k)] * re[size_t(k)] + im[size_t(k)] * im[size_t(k)]) / (kN / 4.0f);
            float l = std::log1p(100.f * mag);
            float d = l - prevL[size_t(k)];
            prevL[size_t(k)] = l;
            sumMag += double(mag);
            sumKMag += double(mag) * k;
            int band = (k >= b0 && k < b1) ? 0 : (k >= b1 && k < b2) ? 1 : (k >= b2 && k < b3) ? 2 : -1;
            if (band < 0) continue;
            e[band] += double(mag) * double(mag);
            if (d > 0) fl[band] += double(d);
        }
        F.fb.push_back(float(fl[0]));
        F.fm.push_back(float(fl[1]));
        F.fh.push_back(float(fl[2]));
        F.eb.push_back(float(e[0]));
        F.em.push_back(float(e[1]));
        F.eh.push_back(float(e[2]));
        F.rms.push_back(float(std::sqrt(sq / kN)));
        F.cent.push_back(sumMag > 1e-9 ? float(sumKMag / sumMag * binHz) : 0.f);
        if (prog && (f & 255) == 0) prog(float(f) / float(std::max(1, frames)));
    }
    return F;
}

double prior(double bpm) {
    double z = std::log2(bpm / 125.0) / 0.65;
    return std::exp(-0.5 * z * z);
}

float interp(std::vector<float> const& v, double pos) {
    if (pos < 0 || pos >= double(v.size() - 1)) return 0.f;
    size_t i = size_t(pos);
    float f = float(pos - double(i));
    return v[i] * (1.f - f) + v[i + 1] * f;
}

struct Tempo {
    double bpm = 120, offset = 0;
    float conf = 0;
};

Tempo estimateTempo(std::vector<float> const& env, double hop, double frameT0) {
    Tempo T;
    size_t n = env.size();
    if (n < 200) return T;
    // onset novelty: smooth, remove slow trend, half-wave rectify
    std::vector<float> es(n);
    for (size_t i = 0; i < n; ++i) {
        float a = i > 0 ? env[i - 1] : env[i];
        float b = i + 1 < n ? env[i + 1] : env[i];
        es[i] = 0.25f * a + 0.5f * env[i] + 0.25f * b;
    }
    auto ma = movAvg(es, 16);
    std::vector<float> ed(n);
    double meanEd = 0;
    for (size_t i = 0; i < n; ++i) {
        ed[i] = std::max(0.f, es[i] - ma[i]);
        meanEd += double(ed[i]);
    }
    meanEd /= double(n);
    // gaussian smoothing so the comb is tolerant of a few ms of jitter
    std::vector<float> edc(n, 0.f);
    {
        float k[5] = {0.06f, 0.24f, 0.40f, 0.24f, 0.06f};
        for (size_t i = 0; i < n; ++i) {
            float a = 0;
            for (int d = -2; d <= 2; ++d) {
                long j = long(i) + d;
                if (j >= 0 && j < long(n)) a += k[d + 2] * ed[size_t(j)];
            }
            edc[i] = a;
        }
    }
    int lmin = std::max(2, int(std::floor(60.0 / (190.0 * hop))));
    int lmax = std::min(int(n / 3), int(std::ceil(60.0 / (62.0 * hop))));
    if (lmax <= lmin + 2) return T;
    std::vector<double> ac(size_t(lmax + 2), 0.0);
    for (int lag = lmin - 1; lag <= lmax + 1; ++lag) {
        double a = 0;
        for (size_t i = 0; i + size_t(lag) < n; ++i) a += double(ed[i]) * double(ed[i + size_t(lag)]);
        ac[size_t(lag)] = a / double(n - size_t(lag));
    }
    struct Cand {
        int lag;
        double w;
    };
    std::vector<Cand> cands;
    for (int lag = lmin; lag <= lmax; ++lag) {
        double a = ac[size_t(lag)];
        if (a > ac[size_t(lag - 1)] && a >= ac[size_t(lag + 1)]) cands.push_back({lag, a * (0.5 + 0.5 * prior(60.0 / (lag * hop)))});
    }
    std::sort(cands.begin(), cands.end(), [](Cand const& a, Cand const& b) { return a.w > b.w; });
    if (cands.size() > 6) cands.resize(6);
    if (cands.empty()) return T;

    double bestFinal = -1;
    for (auto const& c : cands) {
        double bpm0 = 60.0 / (c.lag * hop);
        double bestScore = -1, bestBpm = bpm0, bestPhase = 0;
        for (double bpm = bpm0 * 0.955; bpm <= bpm0 * 1.045; bpm += 0.05) {
            double p = 60.0 / bpm;
            double dphase = hop / 3.0;
            for (double ph = 0; ph < p; ph += dphase) {
                double sum = 0;
                int cnt = 0;
                for (double t = ph; ; t += p) {
                    double pos = (t - frameT0) / hop;
                    if (pos >= double(n - 2)) break;
                    if (pos >= 0) {
                        sum += double(interp(edc, pos));
                        ++cnt;
                    }
                }
                if (cnt < 4) continue;
                double sc = sum / cnt;
                if (sc > bestScore) {
                    bestScore = sc;
                    bestBpm = bpm;
                    bestPhase = ph;
                }
            }
        }
        double fin = bestScore * (0.70 + 0.30 * prior(bestBpm));
        if (fin > bestFinal) {
            bestFinal = fin;
            T.bpm = bestBpm;
            T.offset = bestPhase;
            double ratio = bestScore / std::max(1e-6, meanEd);
            T.conf = clampf(float(1.0 - std::exp(-(ratio - 1.0) / 1.5)), 0.f, 1.f);
        }
    }
    // Octave check: if the half-way points between our beats are almost as strong as the beats
    // themselves, the real pulse is twice as fast (e.g. 174 BPM drum & bass detected as 87).
    if (T.bpm < 100.0) {
        double p = 60.0 / T.bpm, on = 0, off = 0;
        int cnt = 0;
        for (double t = T.offset; ; t += p) {
            double pos = (t - frameT0) / hop, pos2 = (t + p * 0.5 - frameT0) / hop;
            if (pos2 >= double(n - 2)) break;
            if (pos < 0) continue;
            on += double(interp(edc, pos));
            off += double(interp(edc, pos2));
            ++cnt;
        }
        if (cnt > 8 && off >= 0.70 * on) T.bpm *= 2.0;
    }
    return T;
}

SecKind labelSection(size_t i, size_t count, float eN, float slope, float nextE, float eHere) {
    if (i == 0) return eN < 0.62f ? SecKind::Intro : SecKind::Verse;
    if (i + 1 == count) return eN < 0.55f ? SecKind::Outro : SecKind::Drop;
    if (eN >= 0.68f) return SecKind::Drop;
    if (slope > 0.14f || (nextE > eHere + 0.25f && slope > 0.05f)) return SecKind::Build;
    if (eN <= 0.42f) return SecKind::Break;
    return SecKind::Verse;
}

}  // namespace

char const* secName(SecKind k) {
    switch (k) {
        case SecKind::Intro: return "Intro";
        case SecKind::Verse: return "Verse";
        case SecKind::Build: return "Build-up";
        case SecKind::Drop: return "Drop";
        case SecKind::Break: return "Breakdown";
        case SecKind::Outro: return "Outro";
    }
    return "?";
}

float Features::envAt(double t) const {
    if (env.empty()) return 0.f;
    return interp(env, (t - frameT0) / hop);
}

float Features::intensityAt(double t) const {
    if (loud.empty()) return 0.5f;
    double pos = (t - frameT0) / hop;
    if (pos <= 0) return loud.front();
    if (pos >= double(loud.size() - 1)) return loud.back();
    return interp(loud, pos);
}

double Features::nearestBeat(double t) const {
    if (beats.empty() || bpm <= 0) return t;
    double p = 60.0 / bpm;
    double k = std::round((t - beatOffset) / p);
    return beatOffset + k * p;
}

float Features::sectionEnergyAt(double t) const {
    for (auto const& s : sections)
        if (t >= s.t0 && t < s.t1) return s.energy;
    return sections.empty() ? 0.5f : sections.back().energy;
}

Features analyse(PCM const& pcm, double maxSeconds, ProgressFn const& progress, CancelFn const& cancelled) {
    int sr = 22050;
    auto x = prepare(pcm, maxSeconds, sr);
    double dur = double(x.size()) / sr;
    if (dur < 4.0) return syntheticFeatures(std::max(dur, 8.0));

    auto rep = [&](float f) {
        if (progress) progress(f);
    };
    auto fr = stft(x, sr, [&](float f) { rep(0.6f * f); }, cancelled);
    size_t n = fr.count();
    Features F;
    F.duration = dur;
    F.hop = double(kH) / sr;
    F.frameT0 = double(kN / 2) / sr;
    if (n < 100) return syntheticFeatures(dur);
    rep(0.62f);

    // --- onset envelope -----------------------------------------------------
    float pb = std::max(1e-4f, percentile(fr.fb, 0.95)), pm = std::max(1e-4f, percentile(fr.fm, 0.95));
    float ph = std::max(1e-4f, percentile(fr.fh, 0.95));
    std::vector<float> nb(n), nm(n), nh(n);
    F.env.resize(n);
    for (size_t i = 0; i < n; ++i) {
        nb[i] = std::min(1.5f, fr.fb[i] / pb);
        nm[i] = std::min(1.5f, fr.fm[i] / pm);
        nh[i] = std::min(1.5f, fr.fh[i] / ph);
        F.env[i] = 0.50f * nb[i] + 0.32f * nm[i] + 0.18f * nh[i];
    }
    float p98 = std::max(1e-4f, percentile(F.env, 0.98));
    for (auto& v : F.env) v = clampf(v / p98, 0.f, 1.5f);

    // --- loudness ------------------------------------------------------------
    std::vector<float> pw(n), db(n);
    for (size_t i = 0; i < n; ++i) pw[i] = fr.rms[i] * fr.rms[i];
    auto pws = movAvg(pw, int(0.5 / F.hop));  // average POWER, then dB (log-averaging under-rates percussive parts)
    std::vector<float> dbs(n);
    for (size_t i = 0; i < n; ++i) {
        db[i] = 10.f * std::log10(pw[i] + 1e-10f);
        dbs[i] = 10.f * std::log10(pws[i] + 1e-10f);
    }
    float lo = percentile(dbs, 0.05), hi = percentile(dbs, 0.95);
    if (hi - lo < 6.f) hi = lo + 6.f;
    F.loud.resize(n);
    for (size_t i = 0; i < n; ++i) F.loud[i] = clampf((dbs[i] - lo) / (hi - lo), 0.f, 1.f);
    F.dynamics = clampf((percentile(dbs, 0.90) - percentile(dbs, 0.10)) / 24.f, 0.f, 1.f);

    // --- onsets ----------------------------------------------------------------
    auto lm = movAvg(F.env, 12);
    for (size_t i = 2; i + 3 < n; ++i) {
        float v = F.env[i];
        if (!(v >= F.env[i - 1] && v > F.env[i + 1] && v >= F.env[i - 2] && v >= F.env[i + 2])) continue;
        float thr = lm[i] * 1.25f + 0.07f;
        if (v <= thr) continue;
        Onset o;
        o.t = F.frameT0 + double(i) * F.hop - kOnsetBias;
        o.strength = clampf((v - 0.5f * lm[i]) / 0.9f, 0.05f, 1.f);
        float sb = nb[i], sm = nm[i] * 0.85f, sh = nh[i] * 0.70f;
        o.band = (sb >= sm && sb >= sh) ? 0 : (sm >= sh ? 1 : 2);
        if (o.band == 0) ++F.bassOnsets;
        else if (o.band == 1) ++F.midOnsets;
        else ++F.highOnsets;
        if (o.t >= 0) F.onsets.push_back(o);
    }
    F.percussive = clampf(float(double(F.onsets.size()) / dur / 5.0), 0.f, 1.f);
    rep(0.72f);
    if (cancelled && cancelled()) return F;

    // --- tempo + beat grid --------------------------------------------------------
    Tempo T = estimateTempo(F.env, F.hop, F.frameT0);
    F.bpm = T.bpm;
    F.beatOffset = T.offset - kOnsetBias;
    while (F.beatOffset < 0) F.beatOffset += 60.0 / F.bpm;
    F.beatConf = T.conf;
    for (double t = F.beatOffset; t < dur; t += 60.0 / F.bpm) F.beats.push_back(t);
    rep(0.82f);

    // --- sections ---------------------------------------------------------------
    int fw = std::max(1, int(0.5 / F.hop + 0.5));
    size_t nW = n / size_t(fw);
    if (nW < 16) {
        Section s;
        s.t0 = 0;
        s.t1 = dur;
        s.energy = 0.6f;
        s.kind = SecKind::Verse;
        F.sections.push_back(s);
    } else {
        std::vector<std::array<float, 4>> V(nW);
        std::vector<float> wl(nW);
        double centSum = 0, centW = 0;
        for (size_t j = 0; j < nW; ++j) {
            double r = 0, eb = 0, em = 0, eh = 0, c = 0;
            for (int k = 0; k < fw; ++k) {
                size_t i = j * size_t(fw) + size_t(k);
                r += double(pw[i]);
                eb += double(fr.eb[i]);
                em += double(fr.em[i]);
                eh += double(fr.eh[i]);
                c += double(fr.cent[i]);
            }
            double tot = eb + em + eh + 1e-12;
            float wdb = 10.f * std::log10(float(r / fw) + 1e-10f);
            V[j] = {wdb, float(eb / tot), float(eh / tot), float(c / fw)};
            wl[j] = wdb;
            centSum += c / fw * F.loud[std::min(n - 1, j * size_t(fw))];
            centW += F.loud[std::min(n - 1, j * size_t(fw))];
        }
        F.centroid = clampf(float((centW > 0 ? centSum / centW : 2000.0) / 3500.0), 0.f, 1.f);
        // z-score every dimension
        for (int d = 0; d < 4; ++d) {
            double m = 0, s2 = 0;
            for (auto const& v : V) m += double(v[size_t(d)]);
            m /= double(nW);
            for (auto const& v : V) s2 += (double(v[size_t(d)]) - m) * (double(v[size_t(d)]) - m);
            double sd = std::sqrt(s2 / double(nW)) + 1e-6;
            float wgt = d == 0 ? 1.6f : 1.0f;
            for (auto& v : V) v[size_t(d)] = float((double(v[size_t(d)]) - m) / sd) * wgt;
        }
        const int W = 8;  // 4 s on each side
        std::vector<float> nov(nW, 0.f);
        for (size_t j = size_t(W); j + size_t(W) <= nW; ++j) {
            double dist = 0;
            for (int d = 0; d < 4; ++d) {
                double a = 0, b = 0;
                for (int k = 0; k < W; ++k) {
                    a += double(V[j - size_t(W) + size_t(k)][size_t(d)]);
                    b += double(V[j + size_t(k)][size_t(d)]);
                }
                double df = (b - a) / W;
                dist += df * df;
            }
            nov[j] = float(std::sqrt(dist));
        }
        nov = movAvg(nov, 1);
        double nm2 = 0, ns = 0;
        for (float v : nov) nm2 += double(v);
        nm2 /= double(nW);
        for (float v : nov) ns += (double(v) - nm2) * (double(v) - nm2);
        ns = std::sqrt(ns / double(nW));
        float nmax = *std::max_element(nov.begin(), nov.end());
        float thr = float(std::max(0.30 * double(nmax), nm2 + 0.7 * ns));
        struct Pk {
            size_t j;
            float v;
        };
        std::vector<Pk> pk;
        for (size_t j = size_t(W); j + size_t(W) < nW; ++j) {
            bool isMax = true;
            for (int d = -6; d <= 6 && isMax; ++d) {
                long k = long(j) + d;
                if (k >= 0 && k < long(nW) && nov[size_t(k)] > nov[j]) isMax = false;
            }
            if (isMax && nov[j] > thr) pk.push_back({j, nov[j]});
        }
        std::sort(pk.begin(), pk.end(), [](Pk const& a, Pk const& b) { return a.v > b.v; });
        std::vector<size_t> cuts;
        for (auto const& p : pk) {
            bool ok = true;
            for (size_t c : cuts) {
                size_t d = c > p.j ? c - p.j : p.j - c;
                if (d < 12) ok = false;  // >= 6 s apart
            }
            if (ok) cuts.push_back(p.j);
        }
        std::sort(cuts.begin(), cuts.end());
        std::vector<double> edges{0.0};
        for (size_t c : cuts) edges.push_back(double(c) * double(fw) * F.hop + F.frameT0);
        edges.push_back(dur);
        // merge very short pieces
        for (size_t i = 1; i + 1 < edges.size();) {
            if (edges[i] - edges[i - 1] < 5.0) edges.erase(edges.begin() + long(i));
            else ++i;
        }
        if (edges.size() >= 3 && edges.back() - edges[edges.size() - 2] < 5.0) edges.erase(edges.end() - 2);
        for (size_t i = 0; i + 1 < edges.size(); ++i) {
            Section s;
            s.t0 = edges[i];
            s.t1 = edges[i + 1];
            double acc = 0;
            int cnt = 0;
            for (size_t k = size_t(std::max(0.0, (s.t0 - F.frameT0) / F.hop)); k < n && F.frameT0 + double(k) * F.hop < s.t1; ++k) {
                acc += double(F.loud[k]);
                ++cnt;
            }
            s.loudness = cnt ? float(acc / cnt) : 0.5f;
            s.energy = s.loudness;
            F.sections.push_back(s);
        }
        // relative energy + labels
        float e10 = 1.f, e90 = 0.f;
        for (auto const& s : F.sections) {
            e10 = std::min(e10, s.energy);
            e90 = std::max(e90, s.energy);
        }
        if (e90 - e10 < 0.15f) e90 = e10 + 0.15f;
        std::vector<float> eN(F.sections.size());
        for (size_t i = 0; i < F.sections.size(); ++i) eN[i] = clampf((F.sections[i].energy - e10) / (e90 - e10), 0.f, 1.f);
        for (size_t i = 0; i < F.sections.size(); ++i) {
            auto& s = F.sections[i];
            s.rise = i ? F.sections[i].loudness - F.sections[i - 1].loudness : 0.f;
            // slope of loudness inside the section: last third vs first third
            double a = 0, b = 0;
            int ca = 0, cb = 0;
            double len = s.t1 - s.t0;
            for (size_t k = size_t(std::max(0.0, (s.t0 - F.frameT0) / F.hop)); k < n && F.frameT0 + double(k) * F.hop < s.t1; ++k) {
                double tt = F.frameT0 + double(k) * F.hop - s.t0;
                if (tt < len / 3) {
                    a += double(F.loud[k]);
                    ++ca;
                } else if (tt > 2 * len / 3) {
                    b += double(F.loud[k]);
                    ++cb;
                }
            }
            float slope = (ca && cb) ? float(b / cb - a / ca) : 0.f;
            float nextE = i + 1 < F.sections.size() ? eN[i + 1] : eN[i];
            s.kind = labelSection(i, F.sections.size(), eN[i], slope, nextE, eN[i]);
            s.energy = eN[i];
        }
    }
    F.analysed = true;
    rep(1.f);
    return F;
}

Features syntheticFeatures(double duration, double bpm) {
    Features F;
    F.duration = duration;
    F.bpm = bpm;
    F.beatOffset = 0.0;
    F.beatConf = 0.f;
    F.hop = 0.02322;
    F.frameT0 = 0.02322;
    size_t n = size_t(duration / F.hop) + 4;
    F.env.assign(n, 0.f);
    F.loud.assign(n, 0.5f);
    double p = 60.0 / bpm;
    int i = 0;
    for (double t = 0; t < duration; t += p, ++i) {
        F.beats.push_back(t);
        Onset o;
        o.t = t;
        o.strength = (i % 4 == 0) ? 1.0f : (i % 2 == 0 ? 0.75f : 0.55f);
        o.band = (i % 2 == 0) ? 0 : 1;
        F.onsets.push_back(o);
        size_t k = size_t((t - F.frameT0) / F.hop);
        if (k < n) F.env[k] = o.strength;
    }
    auto sec = [&](double a, double b, float e, SecKind k) {
        Section s;
        s.t0 = a * duration;
        s.t1 = b * duration;
        s.energy = e;
        s.kind = k;
        F.sections.push_back(s);
        for (size_t q = size_t(s.t0 / F.hop); q < n && double(q) * F.hop < s.t1; ++q) F.loud[q] = e;
    };
    sec(0.00, 0.14, 0.25f, SecKind::Intro);
    sec(0.14, 0.40, 0.55f, SecKind::Verse);
    sec(0.40, 0.50, 0.70f, SecKind::Build);
    sec(0.50, 0.80, 0.95f, SecKind::Drop);
    sec(0.80, 0.90, 0.30f, SecKind::Break);
    sec(0.90, 1.00, 0.35f, SecKind::Outro);
    F.bassOnsets = int(F.onsets.size());
    F.centroid = 0.4f;
    F.dynamics = 0.6f;
    F.percussive = 0.5f;
    F.analysed = false;
    return F;
}

}  // namespace ais
