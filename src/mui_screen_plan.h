#ifndef FALLOUT_MUI_SCREEN_PLAN_H_
#define FALLOUT_MUI_SCREEN_PLAN_H_

namespace fallout {

// How the mobile UI's game screens show on a screen of any proportions
// (phones, tablets, foldables, a browser's window): a window over the world
// or the whole screen, the phone's layouts (side by side, for a wide area) or
// the tablet's (one over another, for a taller one), and how much smaller
// than the UI size the screen's frame draws to fit them.
//
// A layout fits an area when it does so at least 90% of the UI size (the
// frame then draws that much smaller).
// 1. A window when the tablet layouts fit one, both ways: there is room to
//    spare, and the window shows the most at once (the world around it).
// 2. Else the whole screen, in the layouts whose shape the area is closer
//    to among those fitting it; none fitting,
//    the one needing the least shrinking, as a compromise (the player may
//    change the UI size).
// `[touch] windows` and `[touch] layout` other than Auto decide their part.

// `[touch] windows`: game screens as windows by the rule above, never,
// always.
constexpr int kMuiWindowsAuto = 0;
constexpr int kMuiWindowsNever = 1;
constexpr int kMuiWindowsAlways = 2;

// `[touch] layout`: game screens' layouts by the rule above, the phone's,
// the tablet's.
constexpr int kMuiLayoutAuto = 0;
constexpr int kMuiLayoutPhone = 1;
constexpr int kMuiLayoutTablet = 2;

// Sizes the frames are laid out with (`muiScreenFrameLayout`,
// `muiDialogLayout`), dp.
namespace screen_plan {

    // The game screens' tab column.
    constexpr float kTabColumnWidthDp = 52.0f;

    // Windows: the largest one (the size the tablet layouts are made for),
    // the least margin to the screen's edges, the gap to the tab column
    // outside, the window's padding.
    constexpr float kWindowWidthDp = 896.0f;
    constexpr float kWindowHeightDp = 680.0f;
    constexpr float kWindowMarginDp = 24.0f;
    constexpr float kWindowTabsGapDp = 12.0f;
    constexpr float kWindowPaddingDp = 14.0f;

    // The whole screen: the margin inside the safe area, the gap to the tab
    // column.
    constexpr float kScreenMarginDp = 10.0f;
    constexpr float kScreenTabsGapDp = 10.0f;

    // The content area each layout needs at the least and the shape it is
    // made for (width / height): the tablet's from the windows' mockups
    // (680x440 to 868x652), the phone's from phones (a 640x360 dp one has
    // ~558x340, a 20:9 one ~700x340).
    constexpr float kTabletMinWidthDp = 680.0f;
    constexpr float kTabletMinHeightDp = 440.0f;
    constexpr float kTabletShape = 1.45f;
    constexpr float kPhoneMinWidthDp = 540.0f;
    constexpr float kPhoneMinHeightDp = 300.0f;
    constexpr float kPhoneShape = 2.05f;

    // A layout fits when it needs to be at most this much smaller; smaller
    // than the least, the frame isn't drawn (it squeezes instead).
    constexpr float kFitScale = 0.9f;
    constexpr float kLeastScale = 0.75f;

} // namespace screen_plan

struct MuiScreenPlan {
    bool windowed = false;
    // The tablet's layouts (`muiIsTallContent`).
    bool tablet = false;
    // Of the UI size's dp, for the screen's frame and all inside it.
    float scale = 1.0f;
};

// [safeWidth] x [safeHeight]: the screen's safe area in the UI size's dp;
// [windows], [layout]: `[touch] windows` and `[touch] layout`.
MuiScreenPlan muiScreenPlan(float safeWidth, float safeHeight, int windows, int layout);

} // namespace fallout

#endif /* FALLOUT_MUI_SCREEN_PLAN_H_ */
