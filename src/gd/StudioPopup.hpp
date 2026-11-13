#pragma once
#include <vector>

#include "../core/Profile.hpp"
#include "Common.hpp"
#include "Songs.hpp"

namespace aisgd {

// The menu that opens when the play button of a song is pressed:
// length, difficulty (Easy .. Extreme Demon), Demon-List rank, frame perfect, decoration, Generate.
class StudioPopup : public geode::Popup {
public:
    static StudioPopup* create(SongInfo const& song);

protected:
    bool init(SongInfo const& song);
    void refresh();
    void onDiff(CCObject* sender);
    void onQuickRank(CCObject* sender);
    void onUpcoming(CCObject*);
    void onFramePerfect(CCObject*);
    void onDecor(CCObject* sender);
    void onLength(CCObject* sender);
    void onGenerate(CCObject*);
    void onRankText(std::string const& text);

private:
    SongInfo m_song;
    double m_total = 180.0;
    ais::Settings m_s;
    bool m_building = true;

    std::vector<Chip> m_diff, m_decor, m_quick;
    Chip m_upcoming, m_fp;
    CCLabelBMFont* m_diffLabel = nullptr;
    CCLabelBMFont* m_lenLabel = nullptr;
    CCLabelBMFont* m_rankLabel = nullptr;
    CCLabelBMFont* m_hint = nullptr;
    TextInput* m_rank = nullptr;
    Slider* m_slider = nullptr;
};

}  // namespace aisgd
