#include "mui_screen_plan.h"

#include <math.h>

#include <algorithm>

namespace fallout {

using namespace screen_plan;

namespace {

    struct Area {
        float width;
        float height;
    };

    struct Layout {
        float minWidth;
        float minHeight;
        float shape;
    };

    constexpr Layout kPhone = { kPhoneMinWidthDp, kPhoneMinHeightDp, kPhoneShape };
    constexpr Layout kTablet = { kTabletMinWidthDp, kTabletMinHeightDp, kTabletShape };

    // A window's content area: the largest window, else the screen less its
    // margins and the tab column beside it.
    Area windowContent(float safeWidth, float safeHeight)
    {
        float width = std::min(kWindowWidthDp, safeWidth - kWindowMarginDp * 2.0f - kWindowTabsGapDp - kTabColumnWidthDp);
        float height = std::min(kWindowHeightDp, safeHeight - kWindowMarginDp * 2.0f);
        return { width - kWindowPaddingDp * 2.0f, height - kWindowPaddingDp * 2.0f };
    }

    // The whole screen's content area (`muiDialogLayout`).
    Area screenContent(float safeWidth, float safeHeight)
    {
        return {
            safeWidth - kScreenMarginDp * 2.0f - kScreenTabsGapDp - kTabColumnWidthDp,
            safeHeight - kScreenMarginDp * 2.0f,
        };
    }

    // How much smaller [layout] has to be to fit [area] (1: it fits as it
    // is).
    float fitScale(const Layout& layout, const Area& area)
    {
        return std::max(std::min({ 1.0f, area.width / layout.minWidth, area.height / layout.minHeight }), 0.0f);
    }

    // How far [area]'s shape is from [layout]'s (either way alike).
    float shapeDistance(const Layout& layout, const Area& area)
    {
        if (area.width <= 0.0f || area.height <= 0.0f) {
            return 0.0f;
        }
        return fabsf(logf(area.width / area.height / layout.shape));
    }

    // The layouts for [area] (rule 2) and the frame's scale.
    void chooseLayout(MuiScreenPlan* plan, const Area& area, int layout)
    {
        float phoneScale = fitScale(kPhone, area);
        float tabletScale = fitScale(kTablet, area);
        if (layout == kMuiLayoutPhone) {
            plan->tablet = false;
        } else if (layout == kMuiLayoutTablet) {
            plan->tablet = true;
        } else {
            bool phoneFits = phoneScale >= kFitScale;
            bool tabletFits = tabletScale >= kFitScale;
            if (phoneFits && tabletFits) {
                plan->tablet = shapeDistance(kTablet, area) < shapeDistance(kPhone, area);
            } else if (phoneFits || tabletFits) {
                plan->tablet = tabletFits;
            } else {
                plan->tablet = tabletScale > phoneScale;
            }
        }
        plan->scale = std::max(plan->tablet ? tabletScale : phoneScale, kLeastScale);
    }

} // namespace

MuiScreenPlan muiScreenPlan(float safeWidth, float safeHeight, int windows, int layout)
{
    MuiScreenPlan plan;

    // Rule 1: room for a window of the tablet layouts (at least 90% of
    // their size).
    Area window = windowContent(safeWidth, safeHeight);
    float windowScale = fitScale(kTablet, window);
    switch (windows) {
    case kMuiWindowsNever:
        plan.windowed = false;
        break;
    case kMuiWindowsAlways:
        plan.windowed = true;
        break;
    default:
        plan.windowed = windowScale >= kFitScale;
        break;
    }

    if (plan.windowed && windows != kMuiWindowsAlways && layout == kMuiLayoutAuto) {
        // The window is there for them.
        plan.tablet = true;
        plan.scale = windowScale;
        return plan;
    }

    // Rule 2 (or the player's choices) in the frame's area.
    chooseLayout(&plan, plan.windowed ? window : screenContent(safeWidth, safeHeight), layout);
    return plan;
}

} // namespace fallout
