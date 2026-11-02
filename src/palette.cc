#include "palette.h"

#include <string.h>

#include "color.h"
#include "cycle.h"
#include "debug.h"
#include "game_sound.h"
#include "input.h"
#include "mui_draw.h"
#include "settings.h"
#include "svga.h"

namespace fallout {

static void _palette_reset_();

// 0x6639D0 current_palette
static unsigned char gPalette[256 * 3];

// 0x663CD0 white_palette
unsigned char gPaletteWhite[256 * 3];

// 0x663FD0 black_palette
unsigned char gPaletteBlack[256 * 3];

// 0x6642D0 fade_steps
static unsigned int gPaletteFadeDurationMs = 700;

// CE: The last whole palette set or faded to is no picture (black, white):
// the mobile UI is faded out with it. Not from `gPalette`: colors cycling
// (`paletteSetEntriesInRange`) keep writing there while the screen is
// black.
static bool gPaletteBlank = false;

// 0x493A00 palette_init
void paletteInit()
{
    memset(gPaletteBlack, 0, 256 * 3);
    memset(gPaletteWhite, 63, 256 * 3);
    memcpy(gPalette, _cmap, 256 * 3);

    // CE: Fades are timed (see `colorPaletteFadeBetween`); the original
    // measured a reference fade here and picked a number of steps, which
    // took many times longer on screens slower to present.
    constexpr int vanillaFadeDurationMs = 700;
    gPaletteFadeDurationMs = static_cast<unsigned int>(vanillaFadeDurationMs / settings.ui.anim_speed);
}

// NOTE: Collapsed.
//
// 0x493AD0
static void _palette_reset_()
{
}

// NOTE: Uncollapsed 0x493AD0.
void paletteReset()
{
    _palette_reset_();
}

// NOTE: Uncollapsed 0x493AD0.
void paletteExit()
{
    _palette_reset_();
}

// 0x493AD4 palette_fade_to
// No picture: all black, or all white (leaving a game).
static bool paletteIsBlank(const unsigned char* palette)
{
    for (int index = 1; index < 768; index++) {
        if (palette[index] != palette[0]) {
            return false;
        }
    }
    return palette[0] == 0 || palette[0] == 63;
}

static void paletteFadeFor(unsigned char* palette, unsigned int durationMs)
{
    // CE: Already there: no waiting for a fade that changes nothing.
    if (memcmp(gPalette, palette, sizeof(gPalette)) == 0) {
        return;
    }

    // CE: The mobile UI (drawn in its own colors) fades with the picture.
    bool blank = paletteIsBlank(palette);
    float from = gPaletteBlank ? 0.0f : 1.0f;
    float to = blank ? 0.0f : 1.0f;
    bool sceneFade = from != to;
    gPaletteBlank = blank;
    if (sceneFade) {
        muiSceneFadeBegin(from, to, durationMs);
    }

    bool colorCycleWasEnabled = colorCycleEnabled();
    colorCycleDisable();

    if (backgroundSoundIsEnabled() || speechIsEnabled()) {
        colorPaletteSetTransitionCallback(soundContinueAll);
    }

    colorPaletteFadeBetween(gPalette, palette, durationMs);
    colorPaletteSetTransitionCallback(nullptr);

    memcpy(gPalette, palette, 768);

    if (colorCycleWasEnabled) {
        colorCycleEnable();
    }

    if (sceneFade) {
        muiSceneFadeEnd();
    }
}

void paletteFadeTo(unsigned char* palette)
{
    paletteFadeFor(palette, gPaletteFadeDurationMs);
}

void paletteFadeToForMenu(unsigned char* palette)
{
    paletteFadeFor(palette, settings.enhancements.fast_menu_fades ? gPaletteFadeDurationMs / 3 : gPaletteFadeDurationMs);
}

// 0x493B48 palette_set_to
void paletteSetEntries(unsigned char* palette)
{
    gPaletteBlank = paletteIsBlank(palette);
    if (!gPaletteBlank) {
        muiSceneFadeRelease();
    }
    memcpy(gPalette, palette, sizeof(gPalette));
    _setSystemPalette(palette);
}

// 0x493B78 palette_set_entries
void paletteSetEntriesInRange(unsigned char* palette, int start, int end)
{
    memcpy(gPalette + 3 * start, palette, 3 * (end - start + 1));
    _setSystemPaletteEntries(palette, start, end);
}

} // namespace fallout
