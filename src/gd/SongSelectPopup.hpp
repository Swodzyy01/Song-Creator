#pragma once
#include <vector>

#include "Common.hpp"
#include "Songs.hpp"

namespace aisgd {

// Step 1: choose a song. Every row has a play button that opens the AI Level Studio menu for that song.
class SongSelectPopup : public geode::Popup {
public:
    static SongSelectPopup* create();

protected:
    bool init();
    void rebuild();
    void applyFilter(std::string const& query);
    void onPrev(CCObject*);
    void onNext(CCObject*);
    void onPlay(CCObject* sender);

private:
    static constexpr int kRows = 5;
    std::vector<SongInfo> m_all, m_view;
    int m_page = 0;
    std::string m_query;
    TextInput* m_search = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCLabelBMFont* m_empty = nullptr;
    std::vector<CCNode*> m_dyn;  // row widgets that are rebuilt on every page change
};

}  // namespace aisgd
