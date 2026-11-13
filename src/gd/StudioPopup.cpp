#include "StudioPopup.hpp"

#include <chrono>
#include <cmath>

#include "ThinkingLayer.hpp"

namespace aisgd {

namespace {
constexpr int kQuickRanks[7] = {1000, 500, 250, 100, 50, 10, 1};
}

StudioPopup* StudioPopup::create(SongInfo const& song) {
    auto* ret = new StudioPopup();
    if (ret->init(song)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool StudioPopup::init(SongInfo const& song) {
    if (!Popup::init(440.f, 290.f)) return false;
    this->setTitle("AI Level Studio");
    m_song = song;
    m_total = song.duration > 20.0 ? std::min(song.duration, 600.0) : 180.0;
    m_s.difficulty = ais::Insane;
    m_s.decor = 0;
    m_s.lengthSec = std::min(m_total, 90.0);

    // ---- song ---------------------------------------------------------------------------------------
    auto* title = makeLabel(shorten(song.title, 40).c_str(), "goldFont.fnt", 0.6f, CCPoint{0.f, 0.5f});
    title->limitLabelWidth(300.f, 0.6f, 0.2f);
    m_mainLayer->addChildAtPosition(title, Anchor::Center, CCPoint{-205.f, 106.f});
    std::string dur = song.duration > 1.0 ? "length " + mmss(song.duration) : "length unknown";
    m_mainLayer->addChildAtPosition(makeLabel(dur.c_str(), "chatFont.fnt", 0.6f, CCPoint{1.f, 0.5f}, ccColor3B{190, 190, 190}), Anchor::Center, CCPoint{205.f, 106.f});

    // ---- how much of the song ----------------------------------------------------------------------
    m_mainLayer->addChildAtPosition(makeLabel("Level length", "bigFont.fnt", 0.4f, CCPoint{0.f, 0.5f}), Anchor::Center, CCPoint{-205.f, 80.f});
    m_slider = Slider::create(this, menu_selector(StudioPopup::onLength), 0.75f);
    double minLen = std::min(20.0, m_total);
    m_slider->setValue(m_total > minLen ? float((m_s.lengthSec - minLen) / (m_total - minLen)) : 1.f);
    m_mainLayer->addChildAtPosition(m_slider, Anchor::Center, CCPoint{-5.f, 80.f});
    m_lenLabel = makeLabel("", "bigFont.fnt", 0.36f, CCPoint{1.f, 0.5f});
    m_mainLayer->addChildAtPosition(m_lenLabel, Anchor::Center, CCPoint{205.f, 80.f});

    // ---- difficulty ----------------------------------------------------------------------------------
    static int const frames[10] = {1, 2, 3, 4, 5, 7, 8, 6, 9, 10};  // GD's own difficulty icons (demons are numbered oddly)
    static char const* const abbr[10] = {"E", "N", "H", "HR", "I", "ED", "MD", "HD", "ID", "XD"};
    for (int i = 0; i < 10; ++i) {
        char name[48];
        std::snprintf(name, sizeof name, "diffIcon_%02d_btn_001.png", frames[i]);
        Chip c = makeChip(34.f, 34.f, abbr[i], 0.45f, safeFrame(name), this, menu_selector(StudioPopup::onDiff), i);
        m_buttonMenu->addChildAtPosition(c.btn, Anchor::Center, CCPoint{-171.f + 38.f * float(i), 46.f});
        m_diff.push_back(c);
    }
    m_diffLabel = makeLabel("", "goldFont.fnt", 0.55f);
    m_mainLayer->addChildAtPosition(m_diffLabel, Anchor::Center, CCPoint{0.f, 20.f});

    // ---- demon list (only for Extreme Demon) ---------------------------------------------------------
    m_rankLabel = makeLabel("Demon List  Top", "bigFont.fnt", 0.38f, CCPoint{0.f, 0.5f});
    m_mainLayer->addChildAtPosition(m_rankLabel, Anchor::Center, CCPoint{-205.f, -6.f});
    m_rank = TextInput::create(60.f, "1-1500", "bigFont.fnt");
    m_rank->setFilter("0123456789 topTOP#");
    m_rank->setMaxCharCount(8);
    m_rank->setString("1000");
    m_rank->setCallback([this](std::string const& text) { this->onRankText(text); });
    m_mainLayer->addChildAtPosition(m_rank, Anchor::Center, CCPoint{-92.f, -6.f});
    for (int i = 0; i < 7; ++i) {
        std::string t = std::to_string(kQuickRanks[i]);
        Chip c = makeChip(34.f, 24.f, t.c_str(), 0.4f, nullptr, this, menu_selector(StudioPopup::onQuickRank), kQuickRanks[i]);
        m_buttonMenu->addChildAtPosition(c.btn, Anchor::Center, CCPoint{-48.f + 37.f * float(i), -6.f});
        m_quick.push_back(c);
    }
    m_upcoming = makeChip(40.f, 24.f, "UP", 0.4f, nullptr, this, menu_selector(StudioPopup::onUpcoming), 0);
    m_buttonMenu->addChildAtPosition(m_upcoming.btn, Anchor::Center, CCPoint{196.f, -6.f});

    // ---- frame perfect ---------------------------------------------------------------------------------
    m_fp = makeChip(170.f, 24.f, "Frame perfect: OFF", 0.4f, nullptr, this, menu_selector(StudioPopup::onFramePerfect), 0);
    m_buttonMenu->addChildAtPosition(m_fp.btn, Anchor::Center, CCPoint{-120.f, -36.f});

    // ---- decoration --------------------------------------------------------------------------------------
    m_mainLayer->addChildAtPosition(makeLabel("Decoration", "bigFont.fnt", 0.38f, CCPoint{0.f, 0.5f}), Anchor::Center, CCPoint{-205.f, -66.f});
    static char const* const decorNames[5] = {"Hell", "Green", "Yellow", "Music", "None"};
    for (int i = 0; i < 5; ++i) {
        Chip c = makeChip(62.f, 26.f, decorNames[i], 0.42f, nullptr, this, menu_selector(StudioPopup::onDecor), i);
        m_buttonMenu->addChildAtPosition(c.btn, Anchor::Center, CCPoint{-96.f + 66.f * float(i), -66.f});
        m_decor.push_back(c);
    }

    // ---- go ------------------------------------------------------------------------------------------------
    auto* gen = ButtonSprite::create("Generate level");
    gen->setScale(0.9f);
    auto* genBtn = CCMenuItemSpriteExtra::create(gen, nullptr, this, menu_selector(StudioPopup::onGenerate));
    m_buttonMenu->addChildAtPosition(genBtn, Anchor::Center, CCPoint{0.f, -106.f});
    m_hint = makeLabel("", "chatFont.fnt", 0.55f, CCPoint{0.5f, 0.5f}, ccColor3B{170, 170, 185});
    m_mainLayer->addChildAtPosition(m_hint, Anchor::Center, CCPoint{0.f, -133.f});

    m_building = false;
    refresh();
    return true;
}

void StudioPopup::refresh() {
    if (m_building) return;
    bool extreme = m_s.difficulty == ais::ExtremeDemon;
    for (size_t i = 0; i < m_diff.size(); ++i) paintChip(m_diff[i], int(i) == m_s.difficulty);
    m_diffLabel->setString(ais::makeProfile(m_s).name.c_str());

    m_rankLabel->setVisible(extreme);
    m_rank->setVisible(extreme);
    m_rank->setEnabled(extreme);
    for (auto& c : m_quick) {
        c.btn->setVisible(extreme);
        paintChip(c, extreme && !m_s.upcoming && m_s.rank == c.btn->getTag());
    }
    m_upcoming.btn->setVisible(extreme);
    paintChip(m_upcoming, m_s.upcoming);

    bool allowed = ais::frameperfectAllowed(m_s);
    if (!allowed) m_s.frameperfect = false;
    m_fp.btn->setVisible(extreme);
    m_fp.label->setString(allowed ? (m_s.frameperfect ? "Frame perfect: ON" : "Frame perfect: OFF") : "Frame perfect (Top 250+)");
    m_fp.label->setScale(0.4f);
    m_fp.label->limitLabelWidth(160.f, 0.4f, 0.1f);
    paintChip(m_fp, m_s.frameperfect, !allowed);

    for (size_t i = 0; i < m_decor.size(); ++i) paintChip(m_decor[i], int(i) == m_s.decor);

    std::string len = "up to " + mmss(m_s.lengthSec) + " of " + mmss(m_total);
    m_lenLabel->setString(len.c_str());
    if (extreme) {
        m_hint->setString(m_s.upcoming ? "Upcoming = harder than the current Top 1, lots of frame perfect"
                                       : "The AI studies the Top N level and builds at its difficulty (type a rank or pick one)");
    } else {
        m_hint->setString("Pick Extreme Demon to choose a Demon List rank");
    }
}

void StudioPopup::onDiff(CCObject* sender) {
    m_s.difficulty = static_cast<CCNode*>(sender)->getTag();
    if (m_s.difficulty != ais::ExtremeDemon) {
        m_s.upcoming = false;
        m_s.frameperfect = false;
    } else if (m_s.rank <= 0 && !m_s.upcoming) {
        m_s.rank = 1000;
    }
    refresh();
}

void StudioPopup::onQuickRank(CCObject* sender) {
    m_s.rank = static_cast<CCNode*>(sender)->getTag();
    m_s.upcoming = false;
    m_building = true;  // do not let the text callback undo what we just set
    m_rank->setString(std::to_string(m_s.rank));
    m_building = false;
    refresh();
}

void StudioPopup::onUpcoming(CCObject*) {
    m_s.upcoming = !m_s.upcoming;
    if (m_s.upcoming) {
        m_s.rank = 0;
        m_building = true;
        m_rank->setString("");
        m_building = false;
    } else {
        m_s.rank = 1000;
        m_building = true;
        m_rank->setString("1000");
        m_building = false;
    }
    refresh();
}

void StudioPopup::onRankText(std::string const& text) {
    if (m_building) return;
    int n = 0;
    bool any = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            any = true;
            n = n * 10 + (c - '0');
            if (n > 100000) break;
        }
    }
    m_s.upcoming = false;
    m_s.rank = any ? std::max(1, std::min(1500, n)) : 0;
    refresh();
}

void StudioPopup::onFramePerfect(CCObject*) {
    if (!ais::frameperfectAllowed(m_s)) {
        Notification::create("Frame perfect needs Extreme Demon Top 250 or harder", NotificationIcon::Warning, 2.0f)->show();
        return;
    }
    m_s.frameperfect = !m_s.frameperfect;
    refresh();
}

void StudioPopup::onDecor(CCObject* sender) {
    m_s.decor = static_cast<CCNode*>(sender)->getTag();
    refresh();
}

void StudioPopup::onLength(CCObject* sender) {
    float v = static_cast<SliderThumb*>(sender)->getValue();
    double minLen = std::min(20.0, m_total);
    double sec = minLen + (m_total - minLen) * double(v);
    sec = std::round(sec / 5.0) * 5.0;
    m_s.lengthSec = std::max(minLen, std::min(m_total, sec));
    refresh();
}

void StudioPopup::onGenerate(CCObject*) {
    ais::Settings s = m_s;
    if (s.difficulty == ais::ExtremeDemon && !s.upcoming && s.rank <= 0) s.rank = 1000;
    s.depth = int(Mod::get()->getSettingValue<int64_t>("think-depth"));
    s.syncOffsetSec = double(Mod::get()->getSettingValue<int64_t>("sync-offset")) / 1000.0;
    s.seed = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    if (auto* layer = ThinkingLayer::create(m_song, m_total, s)) layer->show();
}

}  // namespace aisgd
