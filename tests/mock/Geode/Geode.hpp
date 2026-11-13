#pragma once
// Compile-check mock of the Geode / cocos2d / Geometry Dash API subset used in src/gd and src/main.cpp.
// Signatures follow the Geode v5 docs; it only exists to catch typos and type errors in OUR code on a machine
// that has no Geode SDK. It is not part of the mod.
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace gd {
class string {
    std::string s;
public:
    string() {}
    string(char const* c) : s(c) {}
    string(std::string const& x) : s(x) {}
    string& operator=(std::string const& x) { s = x; return *this; }
    string& operator=(char const* x) { s = x; return *this; }
    operator std::string() const { return s; }
    char const* c_str() const { return s.c_str(); }
    bool empty() const { return s.empty(); }
};
}  // namespace gd

namespace cocos2d {
struct CCPoint { float x = 0, y = 0; CCPoint() {} CCPoint(float a, float b) : x(a), y(b) {} };
struct CCSize { float width = 0, height = 0; CCSize() {} CCSize(float w, float h) : width(w), height(h) {} };
struct CCRect { float x, y, w, h; CCRect(float a, float b, float c, float d) : x(a), y(b), w(c), h(d) {} };
struct ccColor3B { unsigned char r, g, b; };
struct ccColor4B { unsigned char r, g, b, a; };
class CCObject { public: virtual ~CCObject() {} CCObject* autorelease() { return this; } };
typedef void (CCObject::*SEL_MenuHandler)(CCObject*);
#define menu_selector(_SELECTOR) (cocos2d::SEL_MenuHandler)(&_SELECTOR)
class CCAction : public CCObject {};
class CCFiniteTimeAction : public CCAction {};
class CCScaleTo : public CCFiniteTimeAction { public: static CCScaleTo* create(float, float) { return new CCScaleTo(); } };
class CCSequence : public CCFiniteTimeAction {
public:
    template <class... A> static CCSequence* create(CCFiniteTimeAction*, A...) { return new CCSequence(); }
};
class CCRepeatForever : public CCAction { public: static CCRepeatForever* create(CCAction*) { return new CCRepeatForever(); } };
}  // namespace cocos2d
namespace cocos2d { namespace extension {} }

namespace geode {
enum class Anchor { Center, Bottom, Top, Left, Right };
}

namespace cocos2d {
class CCNode : public CCObject {
public:
    static CCNode* create() { return new CCNode(); }
    void setContentSize(CCSize const&) {}
    CCSize const& getContentSize() const { static CCSize s; return s; }
    void setPosition(CCPoint const&) {}
    void setScale(float) {}
    void setAnchorPoint(CCPoint const&) {}
    void addChild(CCNode*) {}
    void addChild(CCNode*, int) {}
    void removeFromParent() {}
    void removeFromParentAndCleanup(bool) {}
    void setVisible(bool) {}
    void setTag(int t) { m_tag = t; }
    int getTag() const { return m_tag; }
    void setID(std::string const&) {}
    CCNode* getChildByID(std::string const&) { return nullptr; }
    void addChildAtPosition(CCNode*, geode::Anchor, CCPoint const& = CCPoint{}, bool = true) {}
    void updateLayout() {}
    void scheduleUpdate() {}
    void unscheduleUpdate() {}
    virtual void update(float) {}
    void runAction(CCAction*) {}
    int m_tag = 0;
};
class CCSpriteFrame : public CCObject {};
class CCSpriteFrameCache { public: static CCSpriteFrameCache* sharedSpriteFrameCache() { static CCSpriteFrameCache c; return &c; } CCSpriteFrame* spriteFrameByName(char const*) { return nullptr; } };
class CCSprite : public CCNode {
public:
    static CCSprite* create(char const*) { return new CCSprite(); }
    static CCSprite* createWithSpriteFrame(CCSpriteFrame*) { return new CCSprite(); }
    void setColor(ccColor3B const&) {}
    void setFlipX(bool) {}
};
class CCLabelBMFont : public CCNode {
public:
    static CCLabelBMFont* create(char const*, char const*) { return new CCLabelBMFont(); }
    void setString(char const*) {}
    void limitLabelWidth(float, float, float) {}
    void setColor(ccColor3B const&) {}
};
class CCLayer : public CCNode { public: void setKeypadEnabled(bool) {} };
class CCLayerColor : public CCLayer { public: static CCLayerColor* create(ccColor4B const&, float, float) { return new CCLayerColor(); } };
class CCMenu : public CCLayer { public: static CCMenu* create() { return new CCMenu(); } };
class CCScene : public CCNode {};
class CCTransitionFade : public CCScene { public: static CCTransitionFade* create(float, CCScene*) { return new CCTransitionFade(); } };
class CCSize2 {};
class CCDirector {
public:
    static CCDirector* sharedDirector() { static CCDirector d; return &d; }
    CCSize getWinSize() { return CCSize{569, 320}; }
    void replaceScene(CCScene*) {}
};
class CCFileUtils {
public:
    static CCFileUtils* sharedFileUtils() { static CCFileUtils f; return &f; }
    gd::string fullPathForFilename(char const*, bool) { return gd::string(); }
    bool isFileExist(char const*) { return false; }
    gd::string getWritablePath() { return gd::string(); }
};
namespace extension {
class CCScale9Sprite : public CCSprite {
public:
    static CCScale9Sprite* create(char const*, CCRect) { return new CCScale9Sprite(); }
    void setOpacity(unsigned char) {}
};
}  // namespace extension
}  // namespace cocos2d

using namespace cocos2d;
using namespace cocos2d::extension;

class CCMenuItemSpriteExtra : public cocos2d::CCNode {
public:
    static CCMenuItemSpriteExtra* create(cocos2d::CCNode*, cocos2d::CCNode*, cocos2d::CCObject*, cocos2d::SEL_MenuHandler) { return new CCMenuItemSpriteExtra(); }
};
class ButtonSprite : public cocos2d::CCSprite {
public:
    static ButtonSprite* create(char const*) { return new ButtonSprite(); }
};
class SliderThumb : public cocos2d::CCNode { public: float getValue() { return 0.5f; } };
class Slider : public cocos2d::CCLayer {
public:
    static Slider* create(cocos2d::CCObject*, cocos2d::SEL_MenuHandler, float) { return new Slider(); }
    void setValue(float) {}
};
class FLAlertLayer : public cocos2d::CCLayer {
public:
    virtual void show() {}
    virtual void keyBackClicked() {}
    bool m_noElasticity = false;
    cocos2d::CCLayer* m_mainLayer = nullptr;
    cocos2d::CCMenu* m_buttonMenu = nullptr;
    static FLAlertLayer* create(char const*, gd::string, char const*) { return new FLAlertLayer(); }
};

namespace geode {
class Popup : public FLAlertLayer {
public:
    bool init(float, float, char const* = "GJ_square01.png") {
        m_mainLayer = new cocos2d::CCLayer();
        m_buttonMenu = new cocos2d::CCMenu();
        m_bgSprite = new cocos2d::CCSprite();
        m_closeBtn = new CCMenuItemSpriteExtra();
        return true;
    }
    void setTitle(std::string const&) {}
    cocos2d::CCSprite* m_bgSprite = nullptr;
    CCMenuItemSpriteExtra* m_closeBtn = nullptr;
};
class TextInput : public cocos2d::CCNode {
public:
    static TextInput* create(float, std::string const&, std::string const& = "bigFont.fnt") { return new TextInput(); }
    void setFilter(std::string const&) {}
    void setMaxCharCount(size_t) {}
    void setCallback(std::function<void(std::string const&)>) {}
    void setString(std::string const&, bool = true) {}
    std::string getString() { return {}; }
    void setEnabled(bool) {}
};
enum class NotificationIcon { None, Loading, Success, Warning, Error };
class Notification {
public:
    static Notification* create(std::string const&, NotificationIcon = NotificationIcon::None, float = 1.f) { return new Notification(); }
    void show() {}
};
class Mod {
public:
    static Mod* get() { static Mod m; return &m; }
    std::string getID() const { return "yourname.ai-level-studio"; }
    template <class T> T getSettingValue(std::string_view) const {
        if constexpr (std::is_same_v<T, std::string>) return std::string("Level page");
        else return T(4);
    }
};
namespace prelude {
using namespace geode;
using namespace cocos2d;
using namespace cocos2d::extension;
}  // namespace prelude
}  // namespace geode

// ---- Geometry Dash classes --------------------------------------------------------------------------
class SongInfoObject { public: gd::string m_songName; };
class MusicDownloadManager {
public:
    static MusicDownloadManager* sharedState() { static MusicDownloadManager m; return &m; }
    gd::string pathForSong(int) { return gd::string(); }
    SongInfoObject* getSongInfoObject(int) { return nullptr; }
};
class LevelTools {
public:
    static gd::string getAudioFileName(int) { return gd::string(); }
    static gd::string getAudioTitle(int) { return gd::string(); }
};
class GJGameLevel { public: gd::string m_levelName, m_levelString; int m_audioTrack = 0, m_songID = 0; };
class GameLevelManager {
public:
    static GameLevelManager* sharedState() { static GameLevelManager g; return &g; }
    GJGameLevel* createNewLevel() { return new GJGameLevel(); }
};
class EditLevelLayer { public: static cocos2d::CCScene* scene(GJGameLevel*) { return nullptr; } };
class LevelEditorLayer { public: static cocos2d::CCScene* scene(GJGameLevel*, bool) { return nullptr; } };
class CreatorLayer : public cocos2d::CCLayer { public: virtual bool init() { return true; } };
namespace FMOD { class System; }
class FMODAudioEngine {
public:
    static FMODAudioEngine* sharedEngine() { static FMODAudioEngine e; return &e; }
    FMOD::System* m_system = nullptr;
};
