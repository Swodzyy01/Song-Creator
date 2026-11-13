#include <Geode/Geode.hpp>
#include <Geode/modify/CreatorLayer.hpp>

#include "gd/Common.hpp"
#include "gd/SongSelectPopup.hpp"

using namespace geode::prelude;

class $modify(AiCreatorLayer, CreatorLayer) {
    bool init() {
        if (!CreatorLayer::init()) return false;

        CCNode* icon = CCSprite::create(aisgd::modSprite("ai-logo.png").c_str());
        if (icon) {
            icon->setScale(0.30f);  // the logo is 336 px, creator buttons are ~100 px
        } else {
            icon = ButtonSprite::create("AI Studio");
        }
        auto* btn = CCMenuItemSpriteExtra::create(icon, nullptr, this, menu_selector(AiCreatorLayer::onAiStudio));
        btn->setID(aisgd::modSprite("studio-button"));

        if (auto* menu = this->getChildByID("creator-buttons-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            // another mod changed the layout: put the button in its own little menu in the bottom right corner
            auto win = CCDirector::sharedDirector()->getWinSize();
            auto* own = CCMenu::create();
            own->setPosition(CCPoint{win.width - 50.f, 50.f});
            own->addChild(btn);
            btn->setPosition(CCPoint{0.f, 0.f});
            this->addChild(own, 10);
        }
        return true;
    }

    void onAiStudio(CCObject*) {
        if (auto* popup = aisgd::SongSelectPopup::create()) popup->show();
    }
};
