#include "mui.h"
#include "mui_screens.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "input.h"
#include "kb.h"
#include "loadsave.h"
#include "map.h"
#include "message.h"
#include "preferences.h"
#include "scripts.h"
#include "svga.h"
#include "worldmap.h"

namespace fallout {

namespace {

    // Texts in `game\ce.msg`.
    constexpr int kTextContinue = 214;
    constexpr int kTextSaveGame = 215;
    constexpr int kTextLoadGame = 216;
    constexpr int kTextSettings = 217;
    constexpr int kTextExit = 218;

    // Game menu (the game's options window): where and when at the top,
    // continue, save, load, settings, exit (on the world map without save
    // and load, as the game has no options there). A tap outside or back
    // continues.
    class GameMenuScreen : public MuiScreen {
    public:
        enum class Action {
            None,
            Save,
            Load,
            Settings,
            Exit,
        };

        GameMenuScreen(bool saveLoad, const std::u32string* place)
            : saveLoad(saveLoad)
            , hasPlace(place != nullptr)
            , place(place != nullptr ? *place : std::u32string())
        {
            modal = true;
        }

        void build(MuiContext& ui) override;
        void back() override { finished = true; }
        void key(int keyCode) override;

        Action pending = Action::None;

    private:
        bool saveLoad;
        // The place in the header, the current map's without it.
        bool hasPlace;
        std::u32string place;
    };

    // The game's window keys.
    void GameMenuScreen::key(int keyCode)
    {
        switch (keyCode) {
        case KEY_RETURN:
        case KEY_LOWERCASE_O:
        case KEY_UPPERCASE_O:
        case KEY_LOWERCASE_D:
        case KEY_UPPERCASE_D:
            back();
            break;
        case KEY_LOWERCASE_S:
        case KEY_UPPERCASE_S:
            if (saveLoad) {
                pending = Action::Save;
            }
            break;
        case KEY_LOWERCASE_L:
        case KEY_UPPERCASE_L:
            if (saveLoad) {
                pending = Action::Load;
            }
            break;
        case KEY_LOWERCASE_P:
        case KEY_UPPERCASE_P:
            pending = Action::Settings;
            break;
        case KEY_LOWERCASE_E:
        case KEY_UPPERCASE_E:
        case KEY_F10:
            pending = Action::Exit;
            break;
        }
    }

    void GameMenuScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        ui.dim();

        MuiRect safe = ui.safeRect().inset(ui.dp(8.0f));
        float padding = ui.dp(12.0f);
        float gap = ui.dp(6.0f);
        float headerHeight = ui.dp(27.0f);

        struct Button {
            const char* id;
            int textId;
            const char* fallback;
            Action action;
        };
        const Button allButtons[] = {
            { "menu.continue", kTextContinue, "Continue", Action::None },
            { "menu.save", kTextSaveGame, "Save game", Action::Save },
            { "menu.load", kTextLoadGame, "Load game", Action::Load },
            { "menu.settings", kTextSettings, "Settings", Action::Settings },
            { "menu.exit", kTextExit, "Exit", Action::Exit },
        };
        const Button* buttons[5];
        int buttonCount = 0;
        for (const Button& button : allButtons) {
            if (saveLoad || (button.action != Action::Save && button.action != Action::Load)) {
                buttons[buttonCount++] = &button;
            }
        }

        float buttonHeight = std::min(ui.dp(44.0f), (safe.h - padding * 2.0f - headerHeight - gap * (buttonCount - 1)) / buttonCount);
        float width = std::min(safe.w, ui.dp(312.0f));
        float height = padding * 2.0f + headerHeight + buttonHeight * buttonCount + gap * (buttonCount - 1);
        MuiRect card = { safe.centerX() - width / 2.0f, safe.centerY() - height / 2.0f, width, height };

        if (ui.tappedOutside(card)) {
            back();
        }

        ui.panel(card);

        // Where and the game's date and time.
        MuiRect header = { card.x + padding, card.y + padding, card.w - padding * 2.0f, headerHeight };
        int month;
        int day;
        int year;
        gameTimeGetDate(&month, &day, &year);
        unsigned int minutes = gameTimeGetTime() / 600;
        char date[48];
        snprintf(date, sizeof(date), "%02d.%02d.%04d  %02u:%02u", day, month, year, minutes / 60 % 24, minutes % 60);
        MuiRect placeRect = { header.x, header.y, header.w * 0.52f, header.h };
        MuiRect dateRect = { placeRect.right(), header.y, header.w - placeRect.w, header.h };
        muiDrawTextAligned(hasPlace ? place : muiPlaceText(mapGetCurrentMap(), gElevation), placeRect, ui.dp(12.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
        muiDrawTextAligned(muiDecodeUtf8(date), dateRect, ui.dp(11.0f), theme.textDim, MuiAlign::End, MuiAlign::Center);

        float y = header.bottom();
        for (int index = 0; index < buttonCount; index++) {
            const Button& button = *buttons[index];
            MuiRect rect = { header.x, y, header.w, buttonHeight };
            MuiButtonStyle style = button.action == Action::None ? MuiButtonStyle::Primary : MuiButtonStyle::Normal;
            if (ui.button(button.id, rect, muiDecodeGameText(muiText(button.textId, button.fallback)), style)) {
                if (button.action == Action::None) {
                    back();
                } else {
                    pending = button.action;
                }
            }
            y += buttonHeight + gap;
        }
    }

} // namespace

std::u32string muiPlaceText(int map, int elevation)
{
    return muiPlaceText(map, elevation, &gMapMessageList);
}

std::u32string muiPlaceText(int map, int elevation, MessageList* names)
{
    if (!mapIsValid(static_cast<Map>(map)) || !elevationIsValid(elevation)) {
        return std::u32string();
    }

    // As `mapGetCityName` and `mapGetName`, without their "Error" for a
    // missing name.
    std::u32string place;
    MessageListItem item;
    City city;
    if (wmMatchAreaContainingMapIdx(static_cast<Map>(map), &city) == 0) {
        item.num = 1500 + city;
        if (messageListGetItem(names, &item)) {
            place = muiDecodeGameText(item.text);
        }
    }

    item.num = map * 3 + elevation + 200;
    if (messageListGetItem(names, &item)) {
        if (!place.empty()) {
            place += U" · ";
        }
        place += muiDecodeGameText(item.text);
    }

    return place;
}

int muiOptionsScreenRun()
{
    return muiGameMenuRun(true, nullptr);
}

int muiGameMenuRun(bool saveLoad, const std::u32string* place)
{
    GameMenuScreen screen(saveLoad, place);
    muiPush(&screen);

    // As the game's window: 1 - a game was saved or loaded.
    int result = 0;
    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen.back();
        } else if (keyCode != -1) {
            screen.key(keyCode);
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            screen.finished = true;
        }

        devAutotestTick();
        renderPresent();

        // What was tapped runs after the frame (its own screens).
        GameMenuScreen::Action action = screen.pending;
        screen.pending = GameMenuScreen::Action::None;
        switch (action) {
        case GameMenuScreen::Action::Save:
            if (lsgSaveGame(LoadSaveMode::Normal) == 1) {
                result = 1;
                screen.finished = true;
            }
            break;
        case GameMenuScreen::Action::Load:
            if (lsgLoadGame(LoadSaveMode::Normal) == 1) {
                result = 1;
                screen.finished = true;
            }
            break;
        case GameMenuScreen::Action::Settings:
            doPreferences(false);
            break;
        case GameMenuScreen::Action::Exit:
            showQuitConfirmationDialog();
            break;
        case GameMenuScreen::Action::None:
            break;
        }

        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);
    return result;
}

} // namespace fallout
