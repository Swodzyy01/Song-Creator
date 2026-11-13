#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/Studio.hpp"
#include "Common.hpp"
#include "Songs.hpp"

namespace aisgd {

// Full-screen black overlay. Decodes the song (on the main thread, in small slices so the animation keeps
// running), then runs the whole generator on a worker thread and shows what it is really doing.
class ThinkingLayer : public geode::Popup {
public:
    static ThinkingLayer* create(SongInfo const& song, double totalSeconds, ais::Settings const& settings);
    ~ThinkingLayer() override;

protected:
    bool init(SongInfo const& song, double totalSeconds, ais::Settings const& settings);
    void update(float dt) override;
    void keyBackClicked() override {}  // no accidental exit while thinking; there is a Cancel button
    void onCancel(CCObject*);

private:
    enum class Phase { Decode, Think, Fade, Failed };

    struct Job {
        std::mutex m;
        std::vector<std::string> logs;
        std::string stage = "Starting";
        float progress = 0.f;
        std::atomic<bool> done{false};
        std::atomic<bool> cancel{false};
        ais::RunResult result;
        ais::PCM pcm;
        bool hasPcm = false;
    };

    void startWorker();
    void pollWorker();
    void pushLine(std::string const& line);
    void openLevel();
    void closeSelf();
    void stopWorker();

    SongInfo m_song;
    double m_total = 0;
    ais::Settings m_s;
    Phase m_phase = Phase::Decode;
    SongDecoder m_dec;
    std::shared_ptr<Job> m_job;
    std::thread m_thread;
    ais::RunResult m_result;

    float m_elapsed = 0, m_fade = 0, m_bar = 0, m_barTarget = 0, m_decodeShare = 0.12f;
    std::string m_stageShown;
    std::deque<std::string> m_pending;
    std::string m_typing;
    double m_typed = 0;
    std::vector<std::string> m_visible;

    CCLayerColor* m_barFill = nullptr;
    CCLabelBMFont* m_stageLabel = nullptr;
    CCLabelBMFont* m_titleLabel = nullptr;
    CCLabelBMFont* m_timeLabel = nullptr;
    std::vector<CCLabelBMFont*> m_lines;
    CCMenuItemSpriteExtra* m_cancelBtn = nullptr;
    ButtonSprite* m_cancelSpr = nullptr;
};

}  // namespace aisgd
