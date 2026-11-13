#include "SongSelectPopup.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "StudioPopup.hpp"

namespace aisgd {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool isNumber(std::string const& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

CCNode* arrow(bool right) {
    if (auto* s = safeFrame("GJ_arrow_03_001.png")) {
        s->setFlipX(right);
        return s;
    }
    return makeLabel(right ? ">" : "<", "bigFont.fnt", 0.8f);
}

}  // namespace

SongSelectPopup* SongSelectPopup::create() {
    auto* ret = new SongSelectPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SongSelectPopup::init() {
    if (!Popup::init(380.f, 280.f)) return false;
    this->setTitle("Select a song");
    m_all = collectSongs();

    m_search = TextInput::create(250.f, "Search name or song ID", "bigFont.fnt");
    m_search->setCallback([this](std::string const& text) { this->applyFilter(text); });
    m_mainLayer->addChildAtPosition(m_search, Anchor::Center, CCPoint{0.f, 100.f});

    auto* prev = CCMenuItemSpriteExtra::create(arrow(false), nullptr, this, menu_selector(SongSelectPopup::onPrev));
    auto* next = CCMenuItemSpriteExtra::create(arrow(true), nullptr, this, menu_selector(SongSelectPopup::onNext));
    m_buttonMenu->addChildAtPosition(prev, Anchor::Center, CCPoint{-70.f, -122.f});
    m_buttonMenu->addChildAtPosition(next, Anchor::Center, CCPoint{70.f, -122.f});
    m_pageLabel = makeLabel("", "bigFont.fnt", 0.4f);
    m_mainLayer->addChildAtPosition(m_pageLabel, Anchor::Center, CCPoint{0.f, -122.f});

    m_empty = makeLabel("", "chatFont.fnt", 0.6f, CCPoint{0.5f, 0.5f}, ccColor3B{200, 200, 210});
    m_mainLayer->addChildAtPosition(m_empty, Anchor::Center, CCPoint{0.f, 10.f});

    applyFilter("");
    return true;
}

void SongSelectPopup::applyFilter(std::string const& query) {
    m_query = query;
    std::string q = lower(query);
    m_view.clear();
    for (auto const& s : m_all) {
        if (q.empty() || lower(s.title).find(q) != std::string::npos || (!s.official && std::to_string(s.id) == q)) m_view.push_back(s);
    }
    // somebody typed a song id that is not in the list yet: is it downloaded?
    if (isNumber(q) && q.size() >= 2 && q.size() <= 9) {
        int id = std::atoi(q.c_str());
        bool have = false;
        for (auto const& s : m_all)
            if (!s.official && s.id == id) have = true;
        if (!have) {
            std::string path = customSongPath(id);
            if (fileExists(path)) {
                SongInfo s;
                s.official = false;
                s.id = id;
                s.title = customSongTitle(id);
                s.path = path;
                m_all.push_back(s);
                m_view.insert(m_view.begin(), s);
            }
        }
    }
    m_page = 0;
    rebuild();
}

void SongSelectPopup::rebuild() {
    for (auto* n : m_dyn) n->removeFromParent();
    m_dyn.clear();

    int total = int(m_view.size());
    int pages = std::max(1, (total + kRows - 1) / kRows);
    m_page = std::max(0, std::min(m_page, pages - 1));
    std::string pg = "Page " + std::to_string(m_page + 1) + " / " + std::to_string(pages);
    m_pageLabel->setString(pg.c_str());

    if (total == 0) {
        if (isNumber(lower(m_query)) && !m_query.empty())
            m_empty->setString("That song is not downloaded yet.\nOpen it once in the editor's song browser, then it appears here.");
        else
            m_empty->setString("No songs found.");
    } else {
        m_empty->setString("");
    }

    for (int r = 0; r < kRows; ++r) {
        int idx = m_page * kRows + r;
        if (idx >= total) break;
        SongInfo const& s = m_view[size_t(idx)];
        float y = 56.f - 38.f * float(r);

        auto* bg = CCScale9Sprite::create("square02b_001.png", CCRect{0, 0, 80, 80});
        bg->setContentSize(CCSize{340.f, 34.f});
        bg->setColor(ccColor3B{0, 0, 0});
        bg->setOpacity(110);
        m_mainLayer->addChildAtPosition(bg, Anchor::Center, CCPoint{0.f, y});
        m_dyn.push_back(bg);

        auto* title = makeLabel(shorten(s.title, 36).c_str(), "bigFont.fnt", 0.5f, CCPoint{0.f, 0.5f});
        title->limitLabelWidth(240.f, 0.5f, 0.2f);
        m_mainLayer->addChildAtPosition(title, Anchor::Center, CCPoint{-160.f, y + 6.f});
        m_dyn.push_back(title);

        std::string sub = s.official ? "game track" : "song id " + std::to_string(s.id);
        auto* subLabel = makeLabel(sub.c_str(), "chatFont.fnt", 0.5f, CCPoint{0.f, 0.5f}, ccColor3B{160, 160, 170});
        m_mainLayer->addChildAtPosition(subLabel, Anchor::Center, CCPoint{-160.f, y - 9.f});
        m_dyn.push_back(subLabel);

        CCSprite* play = safeFrame("GJ_playMusicBtn_001.png");
        if (!play) play = ButtonSprite::create("Go");
        else play->setScale(0.85f);
        auto* btn = CCMenuItemSpriteExtra::create(play, nullptr, this, menu_selector(SongSelectPopup::onPlay));
        btn->setTag(idx);
        m_buttonMenu->addChildAtPosition(btn, Anchor::Center, CCPoint{150.f, y});
        m_dyn.push_back(btn);
    }
}

void SongSelectPopup::onPrev(CCObject*) {
    --m_page;
    rebuild();
}

void SongSelectPopup::onNext(CCObject*) {
    ++m_page;
    rebuild();
}

void SongSelectPopup::onPlay(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx >= int(m_view.size())) return;
    SongInfo s = m_view[size_t(idx)];
    s.duration = probeDuration(s.path);
    if (auto* studio = StudioPopup::create(s)) studio->show();
}

}  // namespace aisgd
