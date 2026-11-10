#include "mui.h"
#include "mui_icons.h"
#include "mui_map_view.h"
#include "mui_notify.h"
#include "mui_screens.h"

#include <string>
#include <vector>

#include "automap.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_sound.h"
#include "input.h"
#include "kb.h"
#include "map.h"
#include "object.h"
#include "svga.h"

namespace fallout {

namespace {

    const MuiColor kBackground = muiRgb(0x060A07);
    const MuiColor kListBackground = muiRgb(0x0B120D);
    const MuiColor kDude = muiRgb(0xFF5A4A);
    const MuiColor kExit = muiRgb(0xFFB000);
    const MuiColor kCritter = muiRgb(0xFF3B30);

    // Texts in `game\ce.msg`.
    constexpr int kTextDetails = 212;
    constexpr int kTextMotionSensor = 213;

    // Map: the automap of the current map as the game's window shows it
    // (seen walls, scenery with details, the player, exit grids, critters
    // with the motion sensor), dragged and pinched; the game screens' tabs at
    // the edge.
    class AutomapScreen : public MuiScreen {
    public:
        AutomapScreen()
        {
            modal = true;
        }

        bool coversScreen() override { return !muiUsesWindows(); }
        bool isGameScreen() override { return true; }

        void reset();
        void build(MuiContext& ui) override;
        void back() override { finished = true; }

    private:
        MuiMapView mapView;
        bool opened = false;
        // Tablet windows: the roomy sizes (`muiIsTallContent`).
        bool tall = false;

        // What the automap shows, taken again when it changes (the game is
        // paused under the screen).
        AutomapView view;
        bool viewDirty = true;

        bool toggle(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& label, bool on, bool enabled);
    };

    AutomapScreen gAutomapScreen;

    void AutomapScreen::reset()
    {
        finished = false;
        opened = false;
        viewDirty = true;
    }

    bool AutomapScreen::toggle(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& label, bool on, bool enabled)
    {
        const MuiTheme& theme = muiTheme();
        bool pressed;
        bool tapped = ui.touchable(id, rect, &pressed);
        MuiColor fill = pressed ? theme.buttonPressed : (on ? theme.buttonPrimary : theme.button);
        MuiColor text = on && !pressed ? theme.buttonPrimaryText : theme.buttonText;
        if (!enabled && !on) {
            text = text.withAlpha(90);
        }
        muiFillRoundRect(rect, ui.dp(6.0f), fill);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(1.0f), on || pressed ? theme.accent : theme.buttonBorder);
        float size = muiFitTextSize(label, rect.w - ui.dp(12.0f), ui.dp(tall ? 16.0f : 14.0f), ui.dp(10.0f));
        muiDrawTextAligned(label, rect, size, text, MuiAlign::Center, MuiAlign::Center);
        return tapped;
    }

    void AutomapScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        // Opaque over the map, or a window over it.
        MuiScreenFrame frame = muiScreenFrame(ui, kBackground);
        const MuiRect& content = frame.content;

        MuiGameScreenTab tab = muiGameScreenTabs(ui, frame.tabs, "automap", MuiGameScreenTab::Map, true);
        if (tab == MuiGameScreenTab::Back || frame.tappedOutside) {
            back();
        } else if (tab != MuiGameScreenTab::None && muiSwitchGameScreen(ui, tab)) {
            finished = true;
        }

        // Header: where (the game's names), details and motion sensor at the
        // right. Tablet windows: the roomy sizes.
        tall = muiIsTallContent(ui);
        float headerHeight = ui.dp(tall ? 44.0f : 32.0f);
        MuiRect header = { content.x, content.y, content.w, headerHeight };
        float size = ui.dp(tall ? 16.0f : 14.0f);
        float padding = ui.dp(tall ? 16.0f : 9.0f);
        float gap = ui.dp(tall ? 8.0f : 6.0f);

        Map map = mapGetCurrentMap();
        float x = header.x;
        if (mapIsValid(map)) {
            std::u32string city = muiDecodeGameText(mapGetCityName(map));
            std::u32string name = muiDecodeGameText(mapGetName(map, gElevation));
            for (const auto& chip : { std::make_pair(city, theme.accent), std::make_pair(name, theme.textDim) }) {
                if (chip.first.empty()) {
                    continue;
                }
                float width = muiTextWidth(chip.first, size) + padding * 2.0f;
                MuiRect rect = { x, header.y, width, header.h };
                muiFillRoundRect(rect, ui.dp(6.0f), kListBackground);
                muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(1.0f), theme.panelBorder);
                muiDrawTextAligned(chip.first, rect, size, chip.second, MuiAlign::Center, MuiAlign::Center);
                x += width + gap;
            }
        }

        float buttonWidth = ui.dp(tall ? 200.0f : 170.0f);
        float detailsWidth = ui.dp(tall ? 140.0f : 120.0f);
        MuiRect sensor = { header.right() - buttonWidth, header.y, buttonWidth, header.h };
        MuiRect details = { sensor.x - gap - detailsWidth, header.y, detailsWidth, header.h };

        // High details: scenery too (the game's switch, remembered).
        if (toggle(ui, "automap.details", details, muiDecodeGameText(muiText(kTextDetails, "Details")), automapGetHighDetails(), true)) {
            _gsound_toggle_butt_press_(-1, 0);
            automapSetHighDetails(!automapGetHighDetails());
            viewDirty = true;
        }

        // Motion sensor in the hands: critters for this look (a charge).
        bool scanner = automapIsScannerActive();
        if (toggle(ui, "automap.sensor", sensor, muiDecodeGameText(muiText(kTextMotionSensor, "Motion sensor")), scanner, !scanner) && !scanner) {
            const char* error = automapActivateScanner();
            if (error != nullptr) {
                soundPlayFile("iisxxxx1");
                muiNotify(error, MuiNoticeKind::Warning);
            } else {
                _gsound_red_butt_press(-1, 0);
                viewDirty = true;
            }
        }

        float bodyGap = ui.dp(tall ? 10.0f : 8.0f);
        MuiRect body = { content.x, header.bottom() + bodyGap, content.w, content.bottom() - header.bottom() - bodyGap };
        ui.panel(body);

        if (viewDirty) {
            viewDirty = false;
            automapGetView(gElevation, &view);
        }
        mapView.setTiles("automap." + std::to_string(map) + "." + std::to_string(gElevation), view.tiles, HEX_GRID_WIDTH, HEX_GRID_HEIGHT);
        if (!opened) {
            opened = true;
            mapView.fit();
        }

        std::vector<MuiMapMarker> markers;
        for (const auto& exit : view.exits) {
            markers.push_back({ static_cast<float>(exit.first), static_cast<float>(exit.second), kExit, 2.5f });
        }
        for (const auto& critter : view.critters) {
            markers.push_back({ static_cast<float>(critter.first), static_cast<float>(critter.second), kCritter, 3.0f });
        }
        if (view.dudeX != -1) {
            markers.push_back({ static_cast<float>(view.dudeX), static_cast<float>(view.dudeY), kDude, 4.0f });
        }

        mapView.build(ui, "automap.map", body.inset(ui.dp(4.0f)), markers);
    }

} // namespace

void muiAutomapScreenRun()
{
    AutomapScreen& screen = gAutomapScreen;
    screen.reset();
    muiPush(&screen);

    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        switch (keyCode) {
        // The game's keys closing the automap.
        case KEY_ESCAPE:
        case KEY_TAB:
        case KEY_UPPERCASE_A:
        case KEY_LOWERCASE_A:
            screen.back();
            break;
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            screen.finished = true;
        }

        devAutotestTick();

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);
}

} // namespace fallout
