#include "ThinkingLayer.hpp"

#include <algorithm>
#include <cstdio>

namespace aisgd {

namespace {
constexpr int kLineCount = 8;
constexpr float kBarWidth = 300.f;
}  // namespace

ThinkingLayer* ThinkingLayer::create(SongInfo const& song, double totalSeconds, ais::Settings const& settings) {
    auto* ret = new ThinkingLayer();
    if (ret->init(song, totalSeconds, settings)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

ThinkingLayer::~ThinkingLayer() {
    if (m_job) m_job->cancel = true;
    if (m_thread.joinable()) m_thread.join();
}

bool ThinkingLayer::init(SongInfo const& song, double totalSeconds, ais::Settings const& settings) {
    auto win = CCDirector::sharedDirector()->getWinSize();
    if (!Popup::init(win.width, win.height)) return false;
    m_noElasticity = true;
    m_song = song;
    m_total = totalSeconds;
    m_s = settings;
    if (m_bgSprite) m_bgSprite->setVisible(false);
    if (m_closeBtn) m_closeBtn->setVisible(false);

    // the black screen
    auto* cover = CCLayerColor::create(ccColor4B{0, 0, 0, 255}, win.width, win.height);
    this->addChild(cover, -10);

    // logo (falls back to text when the resource is missing)
    CCNode* logo = CCSprite::create(modSprite("ai-logo.png").c_str());
    if (logo) {
        float s = 72.f / std::max(1.f, logo->getContentSize().height);
        logo->setScale(s);
        logo->runAction(CCRepeatForever::create(CCSequence::create(CCScaleTo::create(0.9f, s * 1.08f), CCScaleTo::create(0.9f, s), nullptr)));
    } else {
        logo = makeLabel("AI", "goldFont.fnt", 1.4f);
    }
    m_mainLayer->addChildAtPosition(logo, Anchor::Center, CCPoint{0.f, 102.f});

    m_titleLabel = makeLabel("AI is thinking", "goldFont.fnt", 0.8f);
    m_mainLayer->addChildAtPosition(m_titleLabel, Anchor::Center, CCPoint{0.f, 54.f});

    m_stageLabel = makeLabel("Starting", "bigFont.fnt", 0.42f, CCPoint{0.5f, 0.5f}, ccColor3B{110, 220, 255});
    m_mainLayer->addChildAtPosition(m_stageLabel, Anchor::Center, CCPoint{0.f, 30.f});

    auto* barBg = CCLayerColor::create(ccColor4B{45, 45, 55, 255}, kBarWidth, 8.f);
    m_mainLayer->addChildAtPosition(barBg, Anchor::Center, CCPoint{-kBarWidth / 2, 10.f});
    m_barFill = CCLayerColor::create(ccColor4B{90, 220, 255, 255}, 1.f, 8.f);
    m_mainLayer->addChildAtPosition(m_barFill, Anchor::Center, CCPoint{-kBarWidth / 2, 10.f});

    for (int i = 0; i < kLineCount; ++i) {
        auto* l = makeLabel("", "chatFont.fnt", 0.5f, CCPoint{0.f, 0.5f}, ccColor3B{200, 200, 210});
        m_mainLayer->addChildAtPosition(l, Anchor::Center, CCPoint{-200.f, -12.f - 14.f * float(i)});
        m_lines.push_back(l);
    }
    m_timeLabel = makeLabel("0:00", "chatFont.fnt", 0.55f, CCPoint{1.f, 0.5f}, ccColor3B{120, 120, 130});
    m_mainLayer->addChildAtPosition(m_timeLabel, Anchor::Center, CCPoint{200.f, 54.f});

    m_cancelSpr = ButtonSprite::create("Cancel");
    m_cancelSpr->setScale(0.6f);
    m_cancelBtn = CCMenuItemSpriteExtra::create(m_cancelSpr, nullptr, this, menu_selector(ThinkingLayer::onCancel));
    m_buttonMenu->addChildAtPosition(m_cancelBtn, Anchor::Center, CCPoint{0.f, -143.f});

    // ---- start: read the song in slices on the main thread -------------------------------------------------
    pushLine("song: " + shorten(song.title, 48) + (song.official ? " (game track)" : " (id " + std::to_string(song.id) + ")"));
    pushLine("reading the song file...");
    bool opened = fileExists(song.path) && m_dec.begin(song.path, settings.lengthSec + 2.0);
    if (!opened) {
        pushLine("could not open the song file" + (m_dec.error.empty() ? std::string() : " (" + m_dec.error + ")") + " -> generic beat grid");
        startWorker();
    } else {
        m_phase = Phase::Decode;
        m_stageShown = "Reading the song file";
        m_stageLabel->setString(m_stageShown.c_str());
    }
    this->scheduleUpdate();
    return true;
}

void ThinkingLayer::pushLine(std::string const& line) {
    m_pending.push_back(line);
}

void ThinkingLayer::startWorker() {
    m_job = std::make_shared<Job>();
    if (m_dec.ok()) {
        m_job->pcm = m_dec.take();
        m_job->hasPcm = true;
        pushLine("decoded " + mmss(m_job->pcm.duration()) + " of audio");
    } else if (!m_dec.error.empty()) {
        pushLine("decode problem: " + m_dec.error + " -> generic beat grid");
    }
    auto job = m_job;
    ais::Settings S = m_s;
    std::string title = m_song.title;
    double total = m_total;
    m_thread = std::thread([job, S, title, total] {
        try {
            ais::RunHooks H;
            H.log = [job](std::string const& s) {
                std::lock_guard<std::mutex> g(job->m);
                job->logs.push_back(s);
            };
            H.stage = [job](float f, std::string const& n) {
                std::lock_guard<std::mutex> g(job->m);
                job->progress = f;
                job->stage = n;
            };
            H.cancelled = [job] { return job->cancel.load(); };
            job->result = ais::runStudio(job->hasPcm ? &job->pcm : nullptr, total, S, title, H);
        } catch (...) {
            job->result.ok = false;
            job->result.error = "internal error";
        }
        job->done = true;
    });
    m_phase = Phase::Think;
}

void ThinkingLayer::pollWorker() {
    std::vector<std::string> logs;
    std::string stage;
    float prog = 0.f;
    {
        std::lock_guard<std::mutex> g(m_job->m);
        logs.swap(m_job->logs);
        stage = m_job->stage;
        prog = m_job->progress;
    }
    for (auto const& l : logs) pushLine(l);
    m_barTarget = m_decodeShare + (1.f - m_decodeShare) * prog;
    if (stage != m_stageShown) {
        m_stageShown = stage;
        m_stageLabel->setString(stage.c_str());
    }
    if (!m_job->done) return;

    if (m_thread.joinable()) m_thread.join();
    {
        std::lock_guard<std::mutex> g(m_job->m);
        for (auto const& l : m_job->logs) pushLine(l);
        m_job->logs.clear();
    }
    m_result = m_job->result;
    if (m_result.ok) {
        m_phase = Phase::Fade;
        m_fade = 0.f;
        m_barTarget = 1.f;
        pushLine("opening the level...");
    } else if (m_job->cancel) {
        closeSelf();
    } else {
        m_phase = Phase::Failed;
        pushLine("could not finish: " + m_result.error);
    }
}

void ThinkingLayer::openLevel() {
    auto* level = GameLevelManager::sharedState()->createNewLevel();
    if (!level) {
        m_phase = Phase::Failed;
        pushLine("the game refused to create a new level");
        return;
    }
    std::string name = shorten("AI " + m_song.title, 26);
    level->m_levelName = name;
    level->m_levelString = m_result.packed;
    if (m_song.official) {
        level->m_audioTrack = m_song.id;
        level->m_songID = 0;
    } else {
        level->m_songID = m_song.id;
        level->m_audioTrack = 0;
    }
    std::string mode = Mod::get()->getSettingValue<std::string>("open-mode");
    CCScene* scene = (mode == "Editor") ? LevelEditorLayer::scene(level, false) : EditLevelLayer::scene(level);
    CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
}

void ThinkingLayer::onCancel(CCObject*) {
    if (m_job) m_job->cancel = true;
    if (m_thread.joinable()) m_thread.join();
    closeSelf();
}

void ThinkingLayer::closeSelf() {
    this->unscheduleUpdate();
    this->setKeypadEnabled(false);
    this->removeFromParentAndCleanup(true);
}

void ThinkingLayer::update(float dt) {
    m_elapsed += dt;
    switch (m_phase) {
        case Phase::Decode: {
            bool done = m_dec.step(6.0);
            m_barTarget = m_decodeShare * m_dec.progress();
            if (done) {
                if (m_dec.ok()) pushLine("song read successfully");
                startWorker();
            }
            break;
        }
        case Phase::Think:
            pollWorker();
            break;
        case Phase::Fade:
            m_fade += dt;
            if (m_fade > 1.1f && m_pending.empty() && m_typing.empty()) {
                m_phase = Phase::Failed;  // guard: openLevel() replaces the scene, never run it twice
                openLevel();
            }
            break;
        case Phase::Failed:
            break;
    }

    // ---- animation: progress bar, dots, clock, typewriter log --------------------------------------------------
    m_bar += (m_barTarget - m_bar) * std::min(1.f, dt * 6.f);
    m_barFill->setContentSize(CCSize{std::max(1.f, kBarWidth * aclamp(m_bar, 0.f, 1.f)), 8.f});

    int dots = int(m_elapsed * 2.f) % 4;
    std::string title = "AI is thinking" + std::string(size_t(dots), '.');
    if (m_phase == Phase::Fade) title = "Level ready";
    if (m_phase == Phase::Failed && !m_result.ok) title = "Something went wrong";
    m_titleLabel->setString(title.c_str());
    char t[16];
    std::snprintf(t, sizeof t, "%d:%02d", int(m_elapsed) / 60, int(m_elapsed) % 60);
    m_timeLabel->setString(t);

    if (m_typing.empty() && !m_pending.empty()) {
        m_typing = m_pending.front();
        m_pending.pop_front();
        m_typed = 0;
    }
    if (!m_typing.empty()) {
        size_t backlog = 0;
        for (auto const& p : m_pending) backlog += p.size();
        double speed = 120.0 + 8.0 * double(backlog);  // never fall far behind the real work
        m_typed += double(dt) * speed;
        if (size_t(m_typed) >= m_typing.size()) {
            m_visible.push_back(m_typing);
            m_typing.clear();
            if (m_visible.size() > size_t(kLineCount)) m_visible.erase(m_visible.begin());
        }
    }
    std::vector<std::string> shown = m_visible;
    if (!m_typing.empty()) {
        shown.push_back(m_typing.substr(0, std::min(m_typing.size(), size_t(m_typed))) + "_");
        if (shown.size() > size_t(kLineCount)) shown.erase(shown.begin(), shown.begin() + long(shown.size() - size_t(kLineCount)));
    }
    for (size_t i = 0; i < m_lines.size(); ++i) {
        if (i < shown.size()) {
            m_lines[i]->setString(shown[i].c_str());
            m_lines[i]->setScale(0.5f);
            m_lines[i]->limitLabelWidth(400.f, 0.5f, 0.2f);
            bool last = (i + 1 == shown.size());
            m_lines[i]->setColor(last ? ccColor3B{235, 235, 245} : ccColor3B{150, 150, 165});
        } else {
            m_lines[i]->setString("");
        }
    }
}

}  // namespace aisgd
