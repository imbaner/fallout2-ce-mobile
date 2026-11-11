#include "mui_floating_text.h"

#include <algorithm>
#include <string>
#include <vector>

#include "color.h"
#include "game.h"
#include "hud_layout.h"
#include "mui.h"
#include "mui_draw.h"
#include "svga.h"
#include "text_object.h"
#include "tile.h"
#include "touch_controls.h"
#include "window_manager.h"
#include "world_view.h"

namespace fallout {

namespace {

    // The size of the map's hints: readable, not zoomed with the map (a
    // zoomed out map is for seeing more of it, its texts shouldn't grow).
    constexpr float kTextSizeDp = 13.0f;
    // About the game's 200 pixels wide texts.
    constexpr float kMaxWidthDp = 230.0f;
    // Kept off the screen's edges.
    constexpr float kMarginDp = 6.0f;
    // Over the head: as high above the tile as the game puts its texts.
    constexpr int kAboveTileGamePixels = 60;

    // Game modes of screens covering the map (as for the HUD and the
    // hints).
    constexpr int kScreenGameModes = ~(GameMode::kCombat | GameMode::kPlayerTurn | GameMode::kSpecial | GameMode::kUseOn);

    // A color of the game's palette.
    MuiColor paletteColor(ColorWithFlags color)
    {
        int index = static_cast<int>(color & COLOR_LAST);
        auto channel = [](unsigned char value) {
            return static_cast<Uint8>((value << 2) | (value >> 4));
        };
        return { channel(_cmap[index * 3]), channel(_cmap[index * 3 + 1]), channel(_cmap[index * 3 + 2]), 255 };
    }

    // The game's floating texts (what critters say, script messages) over
    // the zoomed map at a fixed size, in their colors with their outline.
    // The game keeps them (`text_object.cc`: lifetime, how many, where).
    class FloatingTextScreen : public MuiScreen {
    public:
        FloatingTextScreen()
        {
            modal = false;
        }

        bool isActive() override
        {
            return textObjectsDrawnOverMap()
                && textObjectsGetCount() != 0
                && touchControlsIsEnabled()
                && (GameMode::getCurrentGameMode() & kScreenGameModes) == 0
                && !windowIsModalShown();
        }

        void build(MuiContext& ui) override;
    };

    FloatingTextScreen gFloatingTextScreen;
    bool gFloatingTextPushed = false;
    // Drawn at the last frame (the autotests).
    int gFloatingTextShown = 0;

    void FloatingTextScreen::build(MuiContext& ui)
    {
        float scale = ui.screenRect().w / screenGetWidth();
        float size = ui.dp(kTextSizeDp);
        float lineHeight = muiLineHeight(size);
        float outline = std::max(1.0f, ui.dp(1.0f));

        // The screen without the display cutouts.
        HudMetrics metrics = hudMetricsGet();
        MuiRect screen = ui.screenRect();
        MuiRect bounds = {
            screen.x + metrics.insetLeft * scale + ui.dp(kMarginDp),
            screen.y + metrics.insetTop * scale + ui.dp(kMarginDp),
            screen.w - (metrics.insetLeft + metrics.insetRight) * scale - ui.dp(kMarginDp) * 2.0f,
            screen.h - (metrics.insetTop + metrics.insetBottom) * scale - ui.dp(kMarginDp) * 2.0f,
        };

        gFloatingTextShown = 0;
        int count = textObjectsGetCount();
        for (int index = 0; index < count; index++) {
            TextObjectView view;
            if (!textObjectGetView(index, &view)) {
                continue;
            }

            int tileX;
            int tileY;
            if (tileToScreenXY(view.tile, &tileX, &tileY) != 0) {
                continue;
            }

            std::vector<std::u32string> lines = muiWrapText(muiDecodeGameText(view.text), ui.dp(kMaxWidthDp), size);
            if (lines.empty()) {
                continue;
            }

            float width = 0.0f;
            for (const std::u32string& line : lines) {
                width = std::max(width, muiTextWidth(line, size));
            }
            float height = lineHeight * lines.size();

            // Only of who is on screen, as in the game (its texts stay at
            // their tiles, off the window when they are): a zoomed out map
            // hears the whole map (#36).
            int speakerX;
            int speakerY;
            worldViewWorldToScreen(tileX + 16, tileY, &speakerX, &speakerY);
            if (!screen.contains(speakerX * scale, speakerY * scale)) {
                continue;
            }

            // Over the head (its bottom), or at the tile (its top).
            int anchorX;
            int anchorY;
            worldViewWorldToScreen(tileX + 16, view.aboveTile ? tileY - kAboveTileGamePixels : tileY, &anchorX, &anchorY);
            float x = anchorX * scale - width / 2.0f;
            float y = view.aboveTile ? anchorY * scale - height : anchorY * scale;

            // On screen, whole (a speaker at its edge).
            x = std::clamp(x, bounds.x, std::max(bounds.x, bounds.right() - width));
            y = std::clamp(y, bounds.y, std::max(bounds.y, bounds.bottom() - height));

            // Without the game's outline a dark one: readable over any
            // ground.
            MuiColor color = paletteColor(view.color);
            MuiColor outlineColor = view.outlineColor != COLOR_INVALID ? paletteColor(view.outlineColor) : muiRgb(0x000000);

            for (size_t line = 0; line < lines.size(); line++) {
                MuiRect rect = { x, y + lineHeight * line, width, lineHeight };
                const float offsets[][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
                for (const auto& offset : offsets) {
                    MuiRect shadow = { rect.x + offset[0] * outline, rect.y + offset[1] * outline, rect.w, rect.h };
                    muiDrawTextAligned(lines[line], shadow, size, outlineColor.withAlpha(220), MuiAlign::Center, MuiAlign::Center);
                }
                muiDrawTextAligned(lines[line], rect, size, color, MuiAlign::Center, MuiAlign::Center);
            }
            gFloatingTextShown++;
        }
    }

} // namespace

void muiFloatingTextInit()
{
    if (!gFloatingTextPushed) {
        // Under the hints and the HUD (screens draw in the order they were
        // pushed).
        muiPush(&gFloatingTextScreen);
        gFloatingTextPushed = true;
    }
}

int muiFloatingTextShownCount()
{
    return gFloatingTextScreen.isActive() ? gFloatingTextShown : 0;
}

} // namespace fallout
