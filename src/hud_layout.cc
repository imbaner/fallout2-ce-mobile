#include "hud_layout.h"

#include <algorithm>
#include <cmath>

#include <SDL.h>

#ifdef __ANDROID__
#include <jni.h>
#endif

#ifdef __EMSCRIPTEN__
#include "platform/web/window.h"
#endif

#include "settings.h"
#include "svga.h"

namespace fallout {

// Default size of a square HUD button, dp. Close to Android's recommended
// 48 dp touch target, a bit smaller to leave more of the map visible.
constexpr float kButton = 44.0f;

// Landscape layout, see docs/android-touch-notes.md (Touch HUD).
//
// Top left: menu, quick save/load. Top right: inventory, character, Pip-Boy,
// map. Top center: indicators. Left edge: skills, highlight, sneak. Bottom
// left: log. Bottom right: actions (attack modes, reload / use, start
// combat) over a panel with AP/HP and the two hands. Right edge (combat
// only): end turn and end combat, kept between top right and bottom right
// groups.
static const HudLayout kLandscapeLayout = {
    8.0f,
    6.0f,
    {
        { HudGroupId::System, HudAnchor::TopLeft, HudFlow::Rows, HudAlign::Start,
            { { { HudElementId::Menu, kButton, kButton }, { HudElementId::QuickSave, kButton, kButton }, { HudElementId::QuickLoad, kButton, kButton } } },
            false },
        { HudGroupId::Screens, HudAnchor::TopRight, HudFlow::Rows, HudAlign::End,
            { { { HudElementId::Inventory, kButton, kButton }, { HudElementId::Character, kButton, kButton }, { HudElementId::Pipboy, kButton, kButton }, { HudElementId::Map, kButton, kButton } } },
            false },
        { HudGroupId::Tools, HudAnchor::Left, HudFlow::Columns, HudAlign::Start,
            { { { HudElementId::PartyOrders, kButton, kButton }, { HudElementId::Skills, kButton, kButton }, { HudElementId::Highlight, kButton, kButton }, { HudElementId::Sneak, kButton, kButton } } },
            false },
        { HudGroupId::Log, HudAnchor::BottomLeft, HudFlow::Rows, HudAlign::Start,
            { { { HudElementId::Log, kButton, kButton } } },
            false },
        { HudGroupId::Weapon, HudAnchor::BottomRight, HudFlow::Rows, HudAlign::End,
            {
                { { HudElementId::Modes, kHudFill, 40.0f } },
                { { HudElementId::Status, kHudFill, 22.0f } },
                { { HudElementId::HandLeft, 118.0f, 64.0f }, { HudElementId::HandRight, 118.0f, 64.0f } },
            },
            false, 6.0f, 1 },
        { HudGroupId::Indicators, HudAnchor::Top, HudFlow::Rows, HudAlign::Center,
            { { { HudElementId::Indicators, 300.0f, 22.0f } } },
            true },
        { HudGroupId::Combat, HudAnchor::Right, HudFlow::Columns, HudAlign::Start,
            { { { HudElementId::TacticalView, 96.0f, 40.0f }, { HudElementId::EndTurn, 96.0f, 40.0f }, { HudElementId::EndCombat, 96.0f, 40.0f } } },
            true },
    },
};

const HudLayout& hudLayoutLandscape()
{
    return kLandscapeLayout;
}

float hudGetScreenDensity()
{
    if (settings.touch.hud_density > 0.0) {
        return static_cast<float>(settings.touch.hud_density);
    }

#ifdef __ANDROID__
    // Android reports `DisplayMetrics.densityDpi` here, dp = densityDpi / 160.
    float ddpi = 0.0f;
    if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) == 0 && ddpi > 0.0f) {
        return ddpi / 160.0f;
    }
    return 2.0f;
#elif defined(__EMSCRIPTEN__)
    return webScreenDensity();
#else
    return 1.0f;
#endif
}

float hudGetScreenShortSideDp()
{
    int width = 0;
    int height = 0;
    if (gSdlRenderer == nullptr || SDL_GetRendererOutputSize(gSdlRenderer, &width, &height) != 0 || width <= 0 || height <= 0) {
        return 0.0f;
    }
    return static_cast<float>(std::min(width, height)) / hudGetScreenDensity();
}

float hudGetInterfaceScale()
{
    return static_cast<float>(settings.touch.hud_scale) / 100.0f;
}

// Display cutout safe insets in physical pixels (left, top, right, bottom).
static void hudGetSafeInsets(int insets[4])
{
    insets[0] = insets[1] = insets[2] = insets[3] = 0;

#ifdef __ANDROID__
    // `MainActivity.getSafeInsets` (SDL lays the surface out under the
    // cutout, `LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES`).
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (env == nullptr || activity == nullptr) {
        return;
    }

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = env->GetStaticMethodID(activityClass, "getSafeInsets", "()[I");
    if (method != nullptr) {
        jintArray result = static_cast<jintArray>(env->CallStaticObjectMethod(activityClass, method));
        if (result != nullptr) {
            if (env->GetArrayLength(result) == 4) {
                jint values[4];
                env->GetIntArrayRegion(result, 0, 4, values);
                for (int index = 0; index < 4; index++) {
                    insets[index] = values[index];
                }
            }
            env->DeleteLocalRef(result);
        }
    }

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
#elif defined(__EMSCRIPTEN__)
    webSafeInsets(insets);
#endif
}

// Physical pixels per logical pixel (logical size is letterboxed into
// renderer output).
static float hudGetOutputScale(int* outputWidth, int* outputHeight)
{
    *outputWidth = 0;
    *outputHeight = 0;
    if (gSdlRenderer != nullptr && SDL_GetRendererOutputSize(gSdlRenderer, outputWidth, outputHeight) == 0 && *outputWidth > 0 && *outputHeight > 0) {
        return std::min(static_cast<float>(*outputWidth) / screenGetWidth(),
            static_cast<float>(*outputHeight) / screenGetHeight());
    }
    return 1.0f;
}

float hudGetPixelsPerDp()
{
    int outputWidth;
    int outputHeight;
    float outputScale = hudGetOutputScale(&outputWidth, &outputHeight);
    return std::max(hudGetScreenDensity() / outputScale * hudGetInterfaceScale(), 0.25f);
}

HudMetrics hudMetricsGet()
{
    HudMetrics metrics;
    metrics.screenWidth = screenGetWidth();
    metrics.screenHeight = screenGetHeight();

    int outputWidth;
    int outputHeight;
    float outputScale = hudGetOutputScale(&outputWidth, &outputHeight);
    metrics.pixelsPerDp = hudGetPixelsPerDp();

    // Cutout insets to logical pixels, minus letterbox bars of logical size.
    int insets[4];
    hudGetSafeInsets(insets);
    int barX = std::max(static_cast<int>((outputWidth - metrics.screenWidth * outputScale) / 2), 0);
    int barY = std::max(static_cast<int>((outputHeight - metrics.screenHeight * outputScale) / 2), 0);
    metrics.insetLeft = static_cast<int>(std::ceil(std::max(insets[0] - barX, 0) / outputScale));
    metrics.insetTop = static_cast<int>(std::ceil(std::max(insets[1] - barY, 0) / outputScale));
    metrics.insetRight = static_cast<int>(std::ceil(std::max(insets[2] - barX, 0) / outputScale));
    metrics.insetBottom = static_cast<int>(std::ceil(std::max(insets[3] - barY, 0) / outputScale));
    return metrics;
}

int hudDpToPixels(const HudMetrics& metrics, float dp)
{
    return static_cast<int>(std::lround(dp * metrics.pixelsPerDp));
}

static int hudAlignOffset(HudAlign align, int available, int size)
{
    switch (align) {
    case HudAlign::Center:
        return (available - size) / 2;
    case HudAlign::End:
        return available - size;
    default:
        return 0;
    }
}

static HudPlacedGroup hudLayoutPlaceGroupContent(const HudGroupSpec& group, const HudMetrics& metrics, int gap)
{
    HudPlacedGroup placed;
    placed.id = group.id;
    placed.rect = { 0, 0, 0, 0 };
    placed.framed = group.padding > 0.0f;

    bool rows = group.flow == HudFlow::Rows;

    // Line length (along the line) and thickness (across), fill elements
    // excluded from length.
    std::vector<int> lineLengths;
    std::vector<int> lineThicknesses;
    int longest = 0;
    for (const auto& line : group.lines) {
        int length = 0;
        int thickness = 0;
        bool hasFill = false;
        for (size_t index = 0; index < line.size(); index++) {
            const HudElementSpec& element = line[index];
            float along = rows ? element.width : element.height;
            float across = rows ? element.height : element.width;
            if (index > 0) {
                length += gap;
            }
            if (along == kHudFill) {
                hasFill = true;
            } else {
                length += hudDpToPixels(metrics, along);
            }
            thickness = std::max(thickness, hudDpToPixels(metrics, across));
        }
        lineLengths.push_back(hasFill ? -1 : length);
        lineThicknesses.push_back(thickness);
        if (!hasFill) {
            longest = std::max(longest, length);
        }
    }

    int offset = 0;
    // Where the panel's lines begin, the loose lines' elements.
    int panelOffset = 0;
    size_t looseElements = 0;
    for (size_t lineIndex = 0; lineIndex < group.lines.size(); lineIndex++) {
        const auto& line = group.lines[lineIndex];
        if (static_cast<int>(lineIndex) == group.looseLines) {
            panelOffset = offset;
            looseElements = placed.elements.size();
        }
        int thickness = lineThicknesses[lineIndex];
        int length = lineLengths[lineIndex];
        int fixedLength = 0;
        int fillCount = 0;
        for (size_t index = 0; index < line.size(); index++) {
            float along = rows ? line[index].width : line[index].height;
            if (index > 0) {
                fixedLength += gap;
            }
            if (along == kHudFill) {
                fillCount++;
            } else {
                fixedLength += hudDpToPixels(metrics, along);
            }
        }

        int fillSize = fillCount > 0 ? std::max((longest - fixedLength) / fillCount, 0) : 0;
        if (length == -1) {
            length = longest;
        }

        int position = hudAlignOffset(group.align, longest, length);
        for (size_t index = 0; index < line.size(); index++) {
            const HudElementSpec& element = line[index];
            float along = rows ? element.width : element.height;
            float across = rows ? element.height : element.width;
            int alongSize = along == kHudFill ? fillSize : hudDpToPixels(metrics, along);
            int acrossSize = hudDpToPixels(metrics, across);
            // Elements are centered across the line.
            int acrossPosition = offset + (thickness - acrossSize) / 2;

            HudPlacedElement placedElement;
            placedElement.id = element.id;
            if (rows) {
                placedElement.rect = { position, acrossPosition, alongSize, acrossSize };
            } else {
                placedElement.rect = { acrossPosition, position, acrossSize, alongSize };
            }
            placed.elements.push_back(placedElement);

            position += alongSize + gap;
        }

        offset += thickness + gap;
    }

    int total = std::max(offset - gap, 0);
    if (rows) {
        placed.rect.width = longest;
        placed.rect.height = total;
    } else {
        placed.rect.width = total;
        placed.rect.height = longest;
    }

    int padding = hudDpToPixels(metrics, group.padding);
    for (size_t index = 0; index < placed.elements.size(); index++) {
        HudPlacedElement& element = placed.elements[index];
        element.rect.x += padding;
        // Loose lines from the top, the panel a gap under them.
        element.rect.y += index < looseElements ? 0 : padding;
    }
    placed.rect.width += padding * 2;
    placed.rect.height += padding * 2;
    placed.panel = { 0, panelOffset, placed.rect.width, placed.rect.height - panelOffset };

    return placed;
}

static bool hudRangesOverlap(int start1, int size1, int start2, int size2)
{
    return start1 < start2 + size2 && start2 < start1 + size1;
}

std::vector<HudPlacedGroup> hudLayoutPlace(const HudLayout& layout, const HudMetrics& metrics)
{
    int margin = hudDpToPixels(metrics, layout.margin);
    int gap = hudDpToPixels(metrics, layout.gap);

    int left = metrics.insetLeft + margin;
    int top = metrics.insetTop + margin;
    int right = metrics.screenWidth - metrics.insetRight - margin;
    int bottom = metrics.screenHeight - metrics.insetBottom - margin;

    std::vector<HudPlacedGroup> result;
    for (const HudGroupSpec& group : layout.groups) {
        HudPlacedGroup placed = hudLayoutPlaceGroupContent(group, metrics, gap);
        int width = placed.rect.width;
        int height = placed.rect.height;

        int x;
        int y;
        switch (group.anchor) {
        case HudAnchor::TopLeft:
        case HudAnchor::Left:
        case HudAnchor::BottomLeft:
            x = left;
            break;
        case HudAnchor::TopRight:
        case HudAnchor::Right:
        case HudAnchor::BottomRight:
            x = right - width;
            break;
        default:
            x = (left + right - width) / 2;
            break;
        }

        switch (group.anchor) {
        case HudAnchor::TopLeft:
        case HudAnchor::Top:
        case HudAnchor::TopRight:
            y = top;
            break;
        case HudAnchor::BottomLeft:
        case HudAnchor::Bottom:
        case HudAnchor::BottomRight:
            y = bottom - height;
            break;
        default:
            y = (top + bottom - height) / 2;
            break;
        }

        if (group.avoidOthers) {
            bool vertical = group.anchor == HudAnchor::Left || group.anchor == HudAnchor::Right;
            if (vertical || group.anchor == HudAnchor::Top || group.anchor == HudAnchor::Bottom) {
                // Free range along the edge between groups placed before.
                int position = vertical ? y : x;
                int size = vertical ? height : width;
                int minPosition = vertical ? top : left;
                int maxEnd = vertical ? bottom : right;
                for (const HudPlacedGroup& other : result) {
                    const HudRect& rect = other.rect;
                    bool crosses = vertical
                        ? hudRangesOverlap(x, width, rect.x, rect.width)
                        : hudRangesOverlap(y, height, rect.y, rect.height);
                    if (!crosses) {
                        continue;
                    }

                    int otherStart = vertical ? rect.y : rect.x;
                    int otherSize = vertical ? rect.height : rect.width;
                    if (otherStart * 2 + otherSize < position * 2 + size) {
                        minPosition = std::max(minPosition, otherStart + otherSize + gap);
                    } else {
                        maxEnd = std::min(maxEnd, otherStart - gap);
                    }
                }

                position = std::max(std::min(position, maxEnd - size), minPosition);
                if (vertical) {
                    y = position;
                } else {
                    x = position;
                }
            }
        }

        placed.rect.x = x;
        placed.rect.y = y;
        result.push_back(placed);
    }

    return result;
}

} // namespace fallout
