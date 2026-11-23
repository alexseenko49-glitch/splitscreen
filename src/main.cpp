// Split Screen 4x — прототип v0.1 (Geode, Geometry Dash, Android)
//
// Идея: кнопка "Split" в главном меню открывает сцену, где экран поделён на 4 части.
// В каждой части работает своя копия уровня (отдельный PlayLayer, масштаб 0.5),
// и у каждой части своя зона касания: тап в углу прыгает только игрок этого угла.

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <map>

#if __has_include(<GLES2/gl2.h>)
#include <GLES2/gl2.h>
#endif

using namespace geode::prelude;

static bool g_splitActive = false; // сейчас открыт режим Split
static bool g_injecting = false;   // нажатие отправляет наш код, а не игра

// ---------------------------------------------------------------------------
// Узел, который рисует детей только внутри прямоугольника (чтобы уровень одной
// четверти не заезжал в соседние).
// ---------------------------------------------------------------------------
class ClipNode : public CCNode {
public:
    CCRect m_rect;

    static ClipNode* create(CCRect const& rect) {
        auto ret = new ClipNode();
        if (ret && ret->init()) {
            ret->m_rect = rect;
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void visit() override {
        glEnable(GL_SCISSOR_TEST);
        CCDirector::sharedDirector()->getOpenGLView()->setScissorInPoints(
            m_rect.origin.x, m_rect.origin.y, m_rect.size.width, m_rect.size.height
        );
        CCNode::visit();
        glDisable(GL_SCISSOR_TEST);
    }
};

// ---------------------------------------------------------------------------
// Экран Split: 4 копии уровня, по одной в каждой четверти.
// Порядок зон: 0 = лево-верх, 1 = право-верх, 2 = лево-низ, 3 = право-низ.
// ---------------------------------------------------------------------------
class SplitLayer : public CCLayer {
protected:
    PlayLayer* m_views[4] = {nullptr, nullptr, nullptr, nullptr};
    int m_held[4] = {0, 0, 0, 0};
    std::map<CCTouch*, int> m_touchZone;

    bool init() override {
        if (!CCLayer::init()) return false;

        auto win = CCDirector::sharedDirector()->getWinSize();
        float hw = win.width / 2.f;
        float hh = win.height / 2.f;

        this->setTouchEnabled(true);
        this->setKeypadEnabled(true); // кнопка "назад" на Android

        // фон
        auto bg = CCLayerColor::create(ccColor4B{12, 12, 20, 255});
        this->addChild(bg, -10);

        // уровень
        int levelID = static_cast<int>(Mod::get()->getSettingValue<int64_t>("level-id"));
        auto level = GameLevelManager::sharedState()->getMainLevel(levelID, false);
        if (!level) {
            log::error("Split: не удалось получить уровень {}", levelID);
            return false;
        }

        CCPoint origins[4] = {{0.f, hh}, {hw, hh}, {0.f, 0.f}, {hw, 0.f}};

        int created = 0;
        for (int i = 0; i < 4; ++i) {
            auto pl = PlayLayer::create(level, false, false);
            if (!pl) {
                log::error("Split: PlayLayer {} не создался", i);
                continue;
            }
            m_views[i] = pl;
            ++created;

            // уменьшаем в 2 раза и ставим в свою четверть
            pl->setScale(0.5f);
            pl->setPosition(origins[i]);

            // прячем родной интерфейс (кнопка паузы и т.п.) и выключаем его касания,
            // управление делаем сами
            for (auto child : CCArrayExt<CCNode*>(pl->getChildren())) {
                if (auto ui = typeinfo_cast<UILayer*>(child)) {
                    ui->setVisible(false);
                    ui->setTouchEnabled(false);
                }
            }

            auto clip = ClipNode::create(CCRect(origins[i].x, origins[i].y, hw, hh));
            clip->addChild(pl);
            this->addChild(clip, i);
        }
        log::info("Split: создано экранов: {}", created);
        if (created == 0) return false;

        // линии-разделители
        auto vLine = CCLayerColor::create(ccColor4B{255, 255, 255, 200}, 2.f, win.height);
        vLine->setPosition({hw - 1.f, 0.f});
        this->addChild(vLine, 100);
        auto hLine = CCLayerColor::create(ccColor4B{255, 255, 255, 200}, win.width, 2.f);
        hLine->setPosition({0.f, hh - 1.f});
        this->addChild(hLine, 100);

        // маленький крестик сверху по центру для выхода (ещё работает кнопка "назад")
        auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        closeSpr->setScale(0.6f);
        auto closeBtn = CCMenuItemSpriteExtra::create(
            closeSpr, this, menu_selector(SplitLayer::onClose)
        );
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        closeBtn->setPosition({hw, win.height - 16.f});
        menu->addChild(closeBtn);
        this->addChild(menu, 200);

        g_splitActive = true;
        return true;
    }

    // --- касания: каждый палец идёт в зону, где он коснулся ---

    void registerWithTouchDispatcher() override {
        CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -50, true);
    }

    int zoneFor(CCPoint const& p) {
        auto win = CCDirector::sharedDirector()->getWinSize();
        bool right = p.x >= win.width / 2.f;
        bool top = p.y >= win.height / 2.f;
        return (top ? 0 : 2) + (right ? 1 : 0);
    }

    void sendJump(int zone, bool down) {
        auto pl = m_views[zone];
        if (!pl) return;
        g_injecting = true;
        pl->handleButton(down, 1, true); // кнопка 1 = прыжок, игрок 1
        g_injecting = false;
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        int zone = zoneFor(touch->getLocation());
        m_touchZone[touch] = zone;
        if (++m_held[zone] == 1) this->sendJump(zone, true);
        return true;
    }

    void finishTouch(CCTouch* touch) {
        auto it = m_touchZone.find(touch);
        if (it == m_touchZone.end()) return;
        int zone = it->second;
        m_touchZone.erase(it);
        if (--m_held[zone] <= 0) {
            m_held[zone] = 0;
            this->sendJump(zone, false);
        }
    }

    void ccTouchEnded(CCTouch* touch, CCEvent*) override { this->finishTouch(touch); }
    void ccTouchCancelled(CCTouch* touch, CCEvent*) override { this->finishTouch(touch); }

    // --- выход ---

    void exitSplit() {
        g_splitActive = false;
        CCDirector::sharedDirector()->replaceScene(
            CCTransitionFade::create(0.4f, MenuLayer::scene(false))
        );
    }

    void keyBackClicked() override { this->exitSplit(); }
    void onClose(CCObject*) { this->exitSplit(); }

public:
    ~SplitLayer() override { g_splitActive = false; }

    static SplitLayer* create() {
        auto ret = new SplitLayer();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    static CCScene* scene() {
        auto layer = SplitLayer::create();
        if (!layer) return nullptr;
        auto scene = CCScene::create();
        scene->addChild(layer);
        return scene;
    }
};

// ---------------------------------------------------------------------------
// Пока открыт Split, игнорируем нажатия, которые идут не от нас
// (на случай, если родной интерфейс уровня всё же ловит касания).
// ---------------------------------------------------------------------------
class $modify(SplitBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (g_splitActive && !g_injecting) return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

// ---------------------------------------------------------------------------
// Кнопка "Split" в главном меню.
// ---------------------------------------------------------------------------
class $modify(SplitMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto spr = ButtonSprite::create("Split");
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(SplitMenuLayer::onSplit)
        );
        btn->setID("split-button"_spr);

        if (auto menu = this->getChildByID("bottom-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            // запасной вариант: своё меню в левом верхнем углу
            auto win = CCDirector::sharedDirector()->getWinSize();
            auto menu = CCMenu::create();
            menu->setPosition({0.f, 0.f});
            btn->setPosition({60.f, win.height - 30.f});
            menu->addChild(btn);
            this->addChild(menu, 100);
        }
        return true;
    }

    void onSplit(CCObject*) {
        auto scene = SplitLayer::scene();
        if (!scene) {
            FLAlertLayer::create("Split", "Не удалось запустить режим Split. Смотри лог Geode.", "OK")->show();
            return;
        }
        CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.4f, scene));
    }
};
