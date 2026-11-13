#pragma once
// Portable audio analysis: spectral-flux onsets, tempo + beat grid, section segmentation.
// No Geode / cocos / FMOD includes here, so it can be unit-tested on a normal PC.
#include <functional>
#include <string>
#include <vector>

namespace ais {

struct PCM {
    std::vector<float> mono;  // mono, [-1, 1]
    int sampleRate = 22050;
    double duration() const { return sampleRate > 0 ? double(mono.size()) / sampleRate : 0.0; }
};

struct Onset {
    double t = 0;
    float strength = 0;  // 0..1
    int band = 0;        // 0 bass, 1 mid, 2 high
};

enum class SecKind { Intro, Verse, Build, Drop, Break, Outro };
char const* secName(SecKind k);

struct Section {
    double t0 = 0, t1 = 0;
    float energy = 0;    // 0..1 loudness relative to the loudest/quietest section
    float loudness = 0;  // 0..1 absolute loudness (0 = very quiet track level)
    float rise = 0;    // energy change versus the previous section
    SecKind kind = SecKind::Verse;
};

struct Features {
    double duration = 0;
    double hop = 0.02322;      // seconds between envelope frames
    double frameT0 = 0.02322;  // time of the centre of frame 0
    std::vector<float> env;    // combined onset-strength envelope, ~0..1
    std::vector<float> loud;   // smoothed loudness 0..1 per frame
    std::vector<Onset> onsets;
    double bpm = 120.0;
    double beatOffset = 0.0;  // time of first beat
    float beatConf = 0.f;     // 0..1
    std::vector<double> beats;
    std::vector<Section> sections;
    float centroid = 0.5f;    // 0 dark .. 1 bright
    float dynamics = 0.5f;    // 0 flat .. 1 very dynamic
    float percussive = 0.5f;  // 0 smooth .. 1 very busy
    int bassOnsets = 0, midOnsets = 0, highOnsets = 0;
    bool analysed = false;

    float envAt(double t) const;
    float intensityAt(double t) const;
    double nearestBeat(double t) const;  // returns t if no beat grid
    float sectionEnergyAt(double t) const;
};

using ProgressFn = std::function<void(float)>;
using CancelFn = std::function<bool()>;

// Analyses at most `maxSeconds` of the clip.
Features analyse(PCM const& pcm, double maxSeconds, ProgressFn const& progress = {}, CancelFn const& cancelled = {});

// Used when audio could not be decoded: a plain beat grid with a generic song shape.
Features syntheticFeatures(double duration, double bpm = 120.0);

}  // namespace ais
