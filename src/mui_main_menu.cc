#include "mui.h"
#include "mui_screens.h"

#include <stdio.h>

#include <algorithm>
#include <string>

#include "app_update.h"
#include "art.h"
#include "fps_limiter.h"
#include "game.h"
#include "input.h"
#include "loadsave.h"
#include "mainmenu.h"
#include "message.h"
#include "mui_draw.h"
#include "mui_update.h"
#include "platform_compat.h"
#include "settings.h"
#include "svga.h"
#include "version.h"

#include <SDL.h>

namespace fallout {

namespace {

    // The buttons going away before a screen opens in windows.
    constexpr unsigned int kLeaveMs = 150;

    const MuiColor kBackground = muiRgb(0x000000);

    // Texts in `game\ce.msg`.
    constexpr int kTextContinue = 214;
    constexpr int kTextLoadGame = 216;
    constexpr int kTextSettings = 217;
    constexpr int kTextExit = 218;
    constexpr int kTextNewGame = 290;
    constexpr int kTextIntro = 291;
    constexpr int kTextCredits = 292;

    // Copyright in MISC.MSG.
    constexpr int kMessageCopyright = 20;

    // The game's 640x480 menu picture (used only when the high resolution
    // one is missing) has its button plates drawn at the left: the part
    // right of them is shown.
    constexpr int kVanillaArtLeft = 215;

    // Title menu: the game's picture fit to the screen height at the right
    // edge (a wider picture shows more of itself on wider screens), buttons at
    // the left. Continue loads the save made last, the rest are the game's
    // buttons (the menu's loop runs them).
    class MainMenuScreen : public MuiScreen {
    public:
        MainMenuScreen();

        bool coversScreen() override { return true; }
        void build(MuiContext& ui) override;
        // Exit, as the game's menu does on Esc.
        void back() override { choice = MainMenuOption::Exit; }

        MainMenuOption choice = MainMenuOption::Invalid;
        // Going away in windows (`muiMainMenuLeave`): the buttons fade out
        // over the picture.
        unsigned int leaveTime = 0;
        bool pendingUpdate = false;

    private:
        // The save Continue loads, -1 - none (or the button is off).
        int continueSlot = -1;
        // Load game only when there's a save to load (as Continue).
        bool hasSaves = false;
        std::u32string continuePlace;

        void buildButtons(MuiContext& ui, const MuiRect& column);
        void buildFooter(MuiContext& ui, const MuiRect& rect);
        void buildLinks(MuiContext& ui, const MuiRect& safe);
    };

    MainMenuScreen::MainMenuScreen()
    {
        modal = true;
        hasSaves = lsgMobileNewestSlot() != -1;

        if (settings.enhancements.main_menu_continue) {
            continueSlot = lsgMobileNewestSlot();
            MobileSaveSlotInfo info;
            if (continueSlot != -1 && lsgMobileGetSlotInfo(continueSlot, &info)) {
                // Map names aren't loaded before a game starts.
                MessageList names;
                if (messageListInit(&names)) {
                    char path[COMPAT_MAX_PATH];
                    snprintf(path, sizeof(path), "%smap.msg", gGameMsgPath);
                    if (messageListLoad(&names, path)) {
                        continuePlace = muiPlaceText(info.map, info.elevation, &names);
                    }
                    messageListFree(&names);
                }
                if (continuePlace.empty()) {
                    continuePlace = muiDecodeGameText(info.characterName);
                }
            }
        }
    }

    void MainMenuScreen::build(MuiContext& ui)
    {
        MuiRect safe = ui.safeRect();
        float margin = ui.dp(24.0f);
        float width = std::min(ui.dp(300.0f), safe.w * 0.42f);
        MuiRect column = { safe.x + margin, safe.y + margin, width, safe.h - margin * 2.0f };
        muiDrawTitleArt(ui, column.right() + margin);

        // The buttons fade in and out over the picture (which stays: the
        // screens opened over it in windows draw it too).
        float alpha = ui.appear();
        if (leaveTime != 0 && !muiReduceMotion()) {
            alpha = 1.0f - std::min(static_cast<float>(ui.now - leaveTime) / kLeaveMs, 1.0f);
        }
        float layer = muiLayerAlpha();
        muiSetLayerAlpha(alpha);
        buildButtons(ui, column);
        buildLinks(ui, safe);
        muiSetLayerAlpha(layer);
    }

    void MainMenuScreen::buildButtons(MuiContext& ui, const MuiRect& column)
    {
        const MuiTheme& theme = muiTheme();
        float gap = ui.dp(8.0f);
        float buttonHeight = ui.dp(44.0f);
        float continueHeight = continueSlot != -1 ? ui.dp(56.0f) : 0.0f;
        float smallHeight = ui.dp(38.0f);
        float footerHeight = ui.dp(30.0f);

        // Center the actions that are actually visible, including the
        // update row only once a release is available.
        bool update = appUpdateHasRelease();
        float height = (continueSlot != -1 ? continueHeight + gap : 0.0f) + (buttonHeight + gap) * (hasSaves ? 3.0f : 2.0f) + smallHeight
            + (update ? buttonHeight + gap : 0.0f);
        float scale = std::min(1.0f, std::max(column.h - footerHeight, 0.0f) / height);
        buttonHeight *= scale;
        continueHeight *= scale;
        smallHeight *= scale;
        gap *= scale;
        height *= scale;
        float y = column.y + std::max((column.h - footerHeight - height) / 2.0f, 0.0f);

        if (continueSlot != -1) {
            MuiRect rect = { column.x, y, column.w, continueHeight };
            bool pressed = false;
            if (ui.touchable("mainmenu.continue", rect, &pressed)) {
                choice = MainMenuOption::Continue;
            }
            muiFillRoundRect(rect, ui.dp(theme.radius), pressed ? theme.buttonPressed : theme.buttonPrimary);
            MuiColor text = pressed ? theme.buttonText : theme.buttonPrimaryText;
            MuiRect title = { rect.x + ui.dp(12.0f), rect.y + ui.dp(6.0f), rect.w - ui.dp(24.0f), rect.h * 0.55f - ui.dp(6.0f) };
            MuiRect place = { title.x, title.bottom(), title.w, rect.bottom() - title.bottom() - ui.dp(6.0f) };
            std::u32string label = muiDecodeGameText(muiText(kTextContinue, "Continue"));
            muiDrawTextAligned(label, title, ui.dp(theme.buttonTextSize), text, MuiAlign::Center, MuiAlign::Center);
            if (!continuePlace.empty()) {
                muiDrawTextAligned(continuePlace, place, muiFitTextSize(continuePlace, place.w, ui.dp(11.0f), ui.dp(9.0f)), text.withAlpha(200), MuiAlign::Center, MuiAlign::Center);
            }
            y += continueHeight + gap;
        }

        struct Button {
            const char* id;
            int textId;
            const char* fallback;
            MainMenuOption choice;
        };
        const Button buttons[] = {
            { "mainmenu.new", kTextNewGame, "New game", MainMenuOption::NewGame },
            { "mainmenu.load", kTextLoadGame, "Load game", MainMenuOption::LoadGame },
            { "mainmenu.settings", kTextSettings, "Settings", MainMenuOption::Options },
        };
        for (const Button& button : buttons) {
            if (button.choice == MainMenuOption::LoadGame && !hasSaves) {
                continue;
            }
            if (ui.button(button.id, { column.x, y, column.w, buttonHeight }, muiDecodeGameText(muiText(button.textId, button.fallback)))) {
                choice = button.choice;
            }
            y += buttonHeight + gap;
        }

        if (update) {
            std::u32string label = muiDecodeGameText(muiText(appUpdateReady() ? 406 : appUpdateState() == AppUpdateState::Downloading ? 409
                                                                                                                                      : 405,
                appUpdateReady() ? "Update ready" : appUpdateState() == AppUpdateState::Downloading ? "Downloading update…"
                                                                                                    : "Update available"));
            if (ui.button("mainmenu.update", { column.x, y, column.w, buttonHeight }, label, MuiButtonStyle::AccentBorder)) pendingUpdate = true;
            y += buttonHeight + gap;
        }

        // Seldom used ones in a row.
        const Button small[] = {
            { "mainmenu.intro", kTextIntro, "Intro", MainMenuOption::Intro },
            { "mainmenu.credits", kTextCredits, "Credits", MainMenuOption::Credits },
            { "mainmenu.exit", kTextExit, "Exit", MainMenuOption::Exit },
        };
        float smallWidth = (column.w - gap * 2.0f) / 3.0f;
        for (int index = 0; index < 3; index++) {
            MuiRect rect = { column.x + (smallWidth + gap) * index, y, smallWidth, smallHeight };
            if (ui.button(small[index].id, rect, muiDecodeGameText(muiText(small[index].textId, small[index].fallback)))) {
                choice = small[index].choice;
            }
        }

        buildFooter(ui, { column.x, column.bottom() - footerHeight, column.w * 2.0f, footerHeight });
    }

    // The port author's pages at the top right, over the picture: small, as
    // HUD buttons are, and open in the browser.
    void MainMenuScreen::buildLinks(MuiContext& ui, const MuiRect& safe)
    {
        const MuiTheme& theme = muiTheme();
        struct Link {
            const char* id;
            MuiIcon icon;
            const char* url;
        };
        const Link links[] = {
            { "mainmenu.telegram", MuiIcon::Telegram, kPortTelegramUrl },
            { "mainmenu.youtube", MuiIcon::YouTube, kPortYouTubeUrl },
            { "mainmenu.github", MuiIcon::GitHub, kPortSourceUrl },
        };
        float size = ui.dp(38.0f);
        float gap = ui.dp(8.0f);
        float margin = ui.dp(16.0f);
        float x = safe.right() - margin - size * 3.0f - gap * 2.0f;
        for (const Link& link : links) {
            MuiRect rect = { x, safe.y + margin, size, size };
            bool pressed;
            if (ui.touchable(link.id, rect, &pressed)) SDL_OpenURL(link.url);
            muiFillRoundRect(rect, size / 2.0f, pressed ? theme.buttonPressed : theme.button);
            muiStrokeRoundRect(rect, size / 2.0f, ui.dp(1.0f), pressed ? theme.accent : theme.buttonBorder);
            muiDrawIcon(link.icon, rect.centerX(), rect.centerY(), size * 0.5f, theme.accent);
            x += size + gap;
        }
        // Whose pages they are: the port's author, not Fallout's.
        float right = safe.right() - margin;
        MuiRect caption = { right - size * 3.0f - gap * 2.0f - ui.dp(60.0f), safe.y + margin + size + ui.dp(4.0f), size * 3.0f + gap * 2.0f + ui.dp(60.0f), ui.dp(16.0f) };
        muiDrawTextAligned(muiDecodeGameText(muiText(463, "Port by imbaner")), caption, ui.dp(11.0f), theme.textDim, MuiAlign::End, MuiAlign::Center);
    }

    // The game's copyright and version, as its menu shows them.
    void MainMenuScreen::buildFooter(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        // Longer than the game's VERSION_MAX: a mod's own version string
        // ("FALLOUT: NEVADA (EXPANSION VERSION)") whole.
        char version[128];
        versionGetVersion(version, sizeof(version));

        std::u32string text;
        MessageListItem item;
        item.num = kMessageCopyright;
        if (messageListGetItem(&gMiscMessageList, &item)) {
            text = muiDecodeGameText(item.text) + U"  ·  ";
        }
        text += muiDecodeGameText(version);

        float size = muiFitTextSize(text, rect.w, ui.dp(10.0f), ui.dp(8.0f));
        muiDrawTextAligned(text, rect, size, theme.textDim, MuiAlign::Start, MuiAlign::End);
    }

    MainMenuScreen* gMainMenuScreen = nullptr;

} // namespace

void muiDrawTitleArt(MuiContext& ui, float fadeRight)
{
    MuiRect screen = ui.screenRect();
    muiFillRect(screen, kBackground);

    // The high resolution menu picture (f2_res.dat) has no button plates.
    int width;
    int height;
    int left = 0;
    SDL_Texture* texture = muiPictureTexture(InterfaceFrmId("HR_MAINMENU.FRM"), &width, &height);
    if (texture == nullptr) {
        texture = muiPictureTexture(FrmId(InterfaceFrameId::MainMenuBackgroundImage), &width, &height);
        left = kVanillaArtLeft;
    }
    if (texture == nullptr || height <= 0 || width <= left) {
        return;
    }

    SDL_Rect source = { left, 0, width - left, height };
    float scale = screen.h / height;
    MuiRect rect = { screen.right() - source.w * scale, screen.y, source.w * scale, screen.h };
    muiDrawTexturePart(texture, source, rect);

    // Its left edge fades into the background: a narrow strip, or up to
    // the buttons when the picture goes under them.
    float fadeWidth = std::max(rect.w * 0.08f, fadeRight - rect.x);
    MuiRect fadeRect = { rect.x, rect.y, std::min(fadeWidth, rect.w), rect.h };
    muiFillHorizontalGradient(fadeRect, kBackground, kBackground.withAlpha(0));
    if (rect.x > screen.x) {
        muiFillRect({ screen.x, screen.y, rect.x - screen.x, screen.h }, kBackground);
    }
}

void muiMainMenuShow()
{
    if (gMainMenuScreen != nullptr) {
        return;
    }

    gMainMenuScreen = new MainMenuScreen();
    muiPush(gMainMenuScreen);
    if (settings.touch.update_check) appUpdateCheck(false);
}

void muiMainMenuLeave()
{
    if (gMainMenuScreen == nullptr || muiReduceMotion()) {
        return;
    }

    gMainMenuScreen->leaveTime = SDL_GetTicks();
    while (SDL_GetTicks() - gMainMenuScreen->leaveTime < kLeaveMs) {
        sharedFpsLimiter.mark();
        inputGetInput();
        renderPresent();
        sharedFpsLimiter.throttle();
    }
}

void muiMainMenuHide()
{
    if (gMainMenuScreen == nullptr) {
        return;
    }

    muiRemove(gMainMenuScreen);
    delete gMainMenuScreen;
    gMainMenuScreen = nullptr;
}

MainMenuOption muiMainMenuTakeChoice()
{
    if (gMainMenuScreen == nullptr) {
        return MainMenuOption::Invalid;
    }

    if (gMainMenuScreen->pendingUpdate) {
        gMainMenuScreen->pendingUpdate = false;
        muiUpdateShow();
    }

    MainMenuOption choice = gMainMenuScreen->choice;
    gMainMenuScreen->choice = MainMenuOption::Invalid;
    return choice;
}

} // namespace fallout
