#pragma once
// One call that does everything: listen -> understand -> plan -> compose -> simulate -> decorate -> pack.
// Pure C++ (no game headers); it is run on a worker thread by the black "thinking" screen.
#include <functional>
#include <string>

#include "Audio.hpp"
#include "Profile.hpp"

namespace ais {

struct RunResult {
    bool ok = false;
    std::string error;
    std::string packed;  // base64url(gzip(level string)) - what GD stores in GJGameLevel::m_levelString
    int objects = 0;
    int triggers = 0;
    double seconds = 0;
    double bpm = 0;
    std::string summary;
};

struct RunHooks {
    std::function<void(std::string const&)> log;               // human readable "thoughts" (worker thread)
    std::function<void(float, std::string const&)> stage;      // overall progress 0..1 + title of the current stage
    std::function<bool()> cancelled;
};

// pcm may be null (decode failed): a generic beat grid is used then.
RunResult runStudio(PCM const* pcm, double songSeconds, Settings S, std::string const& songTitle, RunHooks const& H);

}  // namespace ais
