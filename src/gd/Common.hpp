#pragma once
#include <Geode/Geode.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

using namespace geode::prelude;

namespace aisgd {

// Returns nullptr instead of crashing when a sprite frame does not exist in this game version.
inline CCSprite* safeFrame(char const* name) {
    auto* frame = CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(name);
    return frame ? CCSprite::createWithSpriteFrame(frame) : nullptr;
}

// Same as the "name"_spr literal: resource sprites of this mod are registered as "<mod id>/<file>".
inline std::string modSprite(char const* name) { return std::string(Mod::get()->getID()) + "/" + name; }

inline float aclamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

inline std::string shorten(std::string s, size_t maxChars) {
    if (s.size() > maxChars) {
        s.resize(maxChars > 3 ? maxChars - 3 : maxChars);
        s += "...";
    }
    return s;
}

inline std::string mmss(double seconds) {
    int s = int(seconds + 0.5);
    char b[24];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

// A small rounded button with either a text label or an icon. Painted differently when selected.
struct Chip {
    CCMenuItemSpriteExtra* btn = nullptr;
    CCScale9Sprite* bg = nullptr;
    CCLabelBMFont* label = nullptr;
    CCSprite* icon = nullptr;
};

inline Chip makeChip(float w, float h, char const* text, float textScale, CCSprite* icon, CCObject* target, SEL_MenuHandler selector, int tag) {
    Chip c;
    auto* holder = CCNode::create();
    holder->setContentSize(CCSize{w, h});
    c.bg = CCScale9Sprite::create("square02b_001.png", CCRect{0, 0, 80, 80});
    c.bg->setContentSize(CCSize{w, h});
    c.bg->setPosition(CCPoint{w / 2, h / 2});
    holder->addChild(c.bg);
    if (icon) {
        float iw = icon->getContentSize().width, ih = icon->getContentSize().height;
        float s = std::min((w - 4) / (iw > 1 ? iw : 1), (h - 4) / (ih > 1 ? ih : 1));
        icon->setScale(s);
        icon->setPosition(CCPoint{w / 2, h / 2});
        holder->addChild(icon);
        c.icon = icon;
    } else {
        c.label = CCLabelBMFont::create(text, "bigFont.fnt");
        c.label->limitLabelWidth(w - 8, textScale, 0.1f);
        c.label->setPosition(CCPoint{w / 2, h / 2});
        holder->addChild(c.label);
    }
    c.btn = CCMenuItemSpriteExtra::create(holder, nullptr, target, selector);
    c.btn->setTag(tag);
    return c;
}

inline void paintChip(Chip const& c, bool selected, bool dim = false) {
    if (c.bg) {
        c.bg->setColor(selected ? ccColor3B{70, 210, 255} : ccColor3B{20, 20, 28});
        c.bg->setOpacity(selected ? 255 : (dim ? 90 : 150));
    }
    if (c.label) c.label->setColor(dim ? ccColor3B{120, 120, 120} : (selected ? ccColor3B{0, 0, 0} : ccColor3B{255, 255, 255}));
    if (c.icon) c.icon->setColor(dim ? ccColor3B{120, 120, 120} : ccColor3B{255, 255, 255});
}

inline CCLabelBMFont* makeLabel(char const* text, char const* font, float scale, CCPoint anchor = CCPoint{0.5f, 0.5f}, ccColor3B color = ccColor3B{255, 255, 255}) {
    auto* l = CCLabelBMFont::create(text, font);
    l->setScale(scale);
    l->setAnchorPoint(anchor);
    l->setColor(color);
    return l;
}

}  // namespace aisgd
