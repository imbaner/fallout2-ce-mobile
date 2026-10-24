#include "mui.h"
#include "mui_notify.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "dev_autotest.h"
#include "game.h"
#include "game_charset.h"
#include "hud_layout.h"
#include "input.h"
#include "kb.h"
#include "message.h"
#include "platform_compat.h"
#include "settings.h"
#include "svga.h"
#include "touch.h"
#include "touch_log.h"

namespace fallout {

namespace {

    // Terminal-like placeholder theme, see `MuiTheme`.
    const MuiTheme kTheme = {
        muiRgb(0x000000, 150), // dim
        muiRgb(0x0B120D, 240), // panel
        muiRgb(0x3C6E45), // panelBorder
        muiRgb(0xBDF5B5), // text
        muiRgb(0x74A874), // textDim
        muiRgb(0x5CFF5C), // accent
        muiRgb(0x14281A, 245), // button
        muiRgb(0x2E6A3A), // buttonPressed
        muiRgb(0x4C8F57), // buttonBorder
        muiRgb(0xCFFFD0), // buttonText
        muiRgb(0x3F9A4C), // buttonPrimary
        muiRgb(0x041006), // buttonPrimaryText
        8.0f, // radius
        20.0f, // padding
        12.0f, // gap
        52.0f, // buttonHeight
        1.5f, // borderWidth
        21.0f, // titleSize
        17.0f, // bodySize
        18.0f, // buttonTextSize
        10.0f, // touchSlop
    };

    // Typed text not taken yet (see `MuiContext::takeTextInput`).
    std::u32string gTextInput;

    constexpr unsigned int kAppearDurationMs = 160;
    constexpr const char* kDragActiveId = "#drag";

    enum class PointerEventType {
        Down,
        Move,
        Up,
    };

    struct PointerEvent {
        PointerEventType type;
        float x;
        float y;
        // When it happened (SDL ticks of the finger event, not when it was
        // read: after a stall they're read late).
        unsigned int time;
    };

    // A touch pressed and released this long ago which the screens haven't
    // seen yet (the game stalled meanwhile) is dropped whole: replayed late it
    // would act twice (people tap again when nothing happens). The map's
    // gestures are dropped the same (touch.h).
    constexpr unsigned int kStaleTouchMs = kTouchStaleMs;

    struct Pointer {
        bool down = false;
        // Transitions of this frame.
        bool pressed = false;
        bool released = false;
        float x = 0.0f;
        float y = 0.0f;
        float startX = 0.0f;
        float startY = 0.0f;
        unsigned int downTime = 0;
        // Moved farther than touch slop since down.
        bool moved = false;
    };

    std::vector<MuiScreen*> gScreens;

    std::deque<PointerEvent> gEvents;
    Pointer gPointer;
    bool gFingerCaptured = false;
    SDL_FingerID gCapturedFinger = 0;

    // Second finger while the first one is on the UI (pinch, see
    // `MuiContext::panZoom`); its position as it moves.
    struct SecondFinger {
        bool down = false;
        SDL_FingerID id = 0;
        float x = 0.0f;
        float y = 0.0f;
    };

    SecondFinger gSecondFinger;

    struct PanZoomState {
        bool active = false;
        bool pinching = false;
        float lastX = 0.0f;
        float lastY = 0.0f;
        float lastDistance = 0.0f;
        // A second finger joined during this touch (not a tap).
        bool pinched = false;
    };

    std::unordered_map<std::string, PanZoomState> gPanZooms;

    // Widget owning the current touch.
    std::string gActiveId;
    bool gActiveLongPressed = false;

    struct DragState {
        bool active = false;
        std::string source;
        int payload = 0;
    };

    DragState gDrag;

    // Touch areas and widget rects of the last frame.
    std::vector<MuiRect> gRegions;
    std::vector<MuiRect> gFrameRegions;
    std::unordered_map<std::string, MuiRect> gWidgets;
    std::unordered_map<std::string, MuiRect> gFrameWidgets;

    struct ScrollState {
        float offset = 0.0f;
        // Content pixels per ms.
        float velocity = 0.0f;
        bool dragging = false;
        // Finger position along the scroll.
        float lastY = 0.0f;
        unsigned int lastTime = 0;
        unsigned int lastUpdate = 0;
    };

    std::unordered_map<std::string, ScrollState> gScrolls;

    int gOutputWidth = 1;
    int gOutputHeight = 1;
    float gPixelsPerDp = 1.0f;

    MuiRect gSafeRect;
    unsigned int gSafeRectTime = 0;
    bool gSafeRectValid = false;

    // Applies queued pointer events up to one down/up transition, so a quick
    // tap (down and up between two frames) is seen as press in one frame and
    // release in the next one.
    // Drops touches waiting in the queue which ended long ago (see
    // `kStaleTouchMs`): a down with its up in the queue, the up old.
    void dropStaleTouches(unsigned int now)
    {
        while (!gEvents.empty() && gEvents.front().type == PointerEventType::Down) {
            size_t up = 1;
            while (up < gEvents.size() && gEvents[up].type == PointerEventType::Move) {
                up++;
            }

            if (up >= gEvents.size() || gEvents[up].type != PointerEventType::Up
                || static_cast<int>(now - gEvents[up].time) <= static_cast<int>(kStaleTouchMs)) {
                return;
            }

            touchLogUiStaleDrop(static_cast<int>(up + 1));
            gEvents.erase(gEvents.begin(), gEvents.begin() + up + 1);
        }
    }

    void processEvents()
    {
        gPointer.pressed = false;
        gPointer.released = false;

        // Only touches not begun on screen yet: a pressed one gets its end.
        if (!gPointer.down) {
            dropStaleTouches(SDL_GetTicks());
        }

        while (!gEvents.empty()) {
            PointerEvent event = gEvents.front();
            gEvents.pop_front();

            gPointer.x = event.x;
            gPointer.y = event.y;

            if (event.type == PointerEventType::Down) {
                gPointer.down = true;
                gPointer.pressed = true;
                gPointer.startX = event.x;
                gPointer.startY = event.y;
                // When the finger went down (queued ones: before now).
                gPointer.downTime = event.time;
                gPointer.moved = false;
                break;
            }

            if (event.type == PointerEventType::Up) {
                gPointer.down = false;
                gPointer.released = true;
                break;
            }

            float slop = kTheme.touchSlop * gPixelsPerDp;
            if (std::fabs(gPointer.x - gPointer.startX) > slop || std::fabs(gPointer.y - gPointer.startY) > slop) {
                gPointer.moved = true;
            }
        }
    }

    void updateSafeRect(unsigned int now)
    {
        if (gSafeRectValid && now - gSafeRectTime < 1000) {
            return;
        }

        // Cutout insets from the HUD layout metrics (logical pixels).
        HudMetrics metrics = hudMetricsGet();
        float scale = static_cast<float>(gOutputWidth) / std::max(metrics.screenWidth, 1);
        gSafeRect = {
            metrics.insetLeft * scale,
            metrics.insetTop * scale,
            gOutputWidth - (metrics.insetLeft + metrics.insetRight) * scale,
            gOutputHeight - (metrics.insetTop + metrics.insetBottom) * scale,
        };
        gSafeRectTime = now;
        gSafeRectValid = true;
    }

    MuiScreen* topScreen()
    {
        for (auto it = gScreens.rbegin(); it != gScreens.rend(); ++it) {
            if ((*it)->isActive()) {
                return *it;
            }
        }
        return nullptr;
    }

} // namespace

void muiToScreen(float x, float y, int* screenX, int* screenY)
{
    float scale = gOutputWidth > 0 ? static_cast<float>(screenGetWidth()) / gOutputWidth : 1.0f;
    *screenX = static_cast<int>(x * scale);
    *screenY = static_cast<int>(y * scale);
}

const char* muiText(int id, const char* fallback)
{
    static MessageList* messageList = nullptr;
    static bool loaded = false;
    // The language loaded: texts asked for before the game's fonts are
    // known may be in another (game_charset.h). The texts of the one before
    // are kept, given out already.
    static std::string language;

    const char* uiLanguage = gameCharsetUiLanguage();
    if (messageList == nullptr || compat_stricmp(language.c_str(), uiLanguage) != 0) {
        messageList = new MessageList();
        language = uiLanguage;
        loaded = messageListInit(messageList) && messageListLoadInLanguage(messageList, "game\\ce.msg", uiLanguage);
    }

    if (loaded) {
        MessageListItem item;
        item.num = id;
        if (messageListGetItem(messageList, &item)) {
            return item.text;
        }
    }

    return fallback;
}

#if !FALLOUT_TOUCH_ONLY
bool muiIsEnabled()
{
    return settings.touch.mobile_ui;
}
#endif

const MuiTheme& muiTheme()
{
    return kTheme;
}

float MuiContext::appear() const
{
    if (current == nullptr) {
        return 1.0f;
    }

    unsigned int elapsed = now - current->openTime;
    return std::min(static_cast<float>(elapsed) / kAppearDurationMs, 1.0f);
}

void MuiContext::dim()
{
    MuiColor color = kTheme.dim;
    color.a = static_cast<Uint8>(color.a * appear());
    muiFillRect(screen, color);
}

void MuiContext::panel(const MuiRect& rect)
{
    region(rect);
    muiFillRoundRect(rect, dp(kTheme.radius), kTheme.panel);
    muiStrokeRoundRect(rect, dp(kTheme.radius), dp(kTheme.borderWidth), kTheme.panelBorder);
}

void MuiContext::region(const MuiRect& rect)
{
    gFrameRegions.push_back(rect);
}

bool MuiContext::touchable(const std::string& id, const MuiRect& rect, bool* pressed, bool* longPressed)
{
    region(rect);
    gFrameWidgets[id] = rect;

    if (pressed != nullptr) {
        *pressed = false;
    }

    if (longPressed != nullptr) {
        *longPressed = false;
    }

    if (!isInteractive) {
        return false;
    }

    // Not a touch begun before the screen showed (waiting in the queue while
    // it faded in, or held since the previous screen).
    if (gPointer.pressed && gActiveId.empty() && rect.contains(gPointer.startX, gPointer.startY)
        && !(current != nullptr && static_cast<int>(gPointer.downTime - current->openTime) < 0)) {
        gActiveId = id;
        gActiveLongPressed = false;
    }

    if (gActiveId != id) {
        return false;
    }

    // Finger may wander a bit outside while pressing.
    float slop = dp(kTheme.touchSlop);
    bool inside = rect.inset(-slop).contains(gPointer.x, gPointer.y);

    if (pressed != nullptr) {
        *pressed = gPointer.down && inside;
    }

    if (longPressed != nullptr && gPointer.down && !gPointer.moved && !gActiveLongPressed
        && SDL_GetTicks() - gPointer.downTime >= touchLongPressMs()) {
        gActiveLongPressed = true;
        *longPressed = true;
    }

    if (gActiveLongPressed) {
        return false;
    }

    return gPointer.released && inside;
}

void MuiContext::mark(const std::string& id, const MuiRect& rect)
{
    gFrameWidgets[id] = rect;
}

bool MuiContext::button(const std::string& id, const MuiRect& rect, const std::u32string& label, MuiButtonStyle style)
{
    bool pressed;
    bool clicked = touchable(id, rect, &pressed);

    bool primary = style == MuiButtonStyle::Primary;
    MuiColor fill = pressed ? kTheme.buttonPressed : (primary ? kTheme.buttonPrimary : kTheme.button);
    MuiColor textColor = primary && !pressed ? kTheme.buttonPrimaryText : kTheme.buttonText;
    MuiColor border = pressed || style == MuiButtonStyle::AccentBorder ? kTheme.accent : kTheme.buttonBorder;
    if (style == MuiButtonStyle::AccentBorder) {
        textColor = kTheme.accent;
    }
    if (style == MuiButtonStyle::Danger) {
        fill = muiRgb(pressed ? 0x47211D : 0x1B100E);
        textColor = border = muiRgb(0xFF8E72);
    }

    muiFillRoundRect(rect, dp(kTheme.radius), fill);
    muiStrokeRoundRect(rect, dp(kTheme.radius), dp(kTheme.borderWidth), border);
    // Long labels (other languages, narrow buttons) get smaller.
    float size = muiFitTextSize(label, rect.w - dp(kTheme.padding), dp(kTheme.buttonTextSize), dp(10.0f));
    muiDrawTextAligned(label, rect, size, textColor, MuiAlign::Center, MuiAlign::Center);

    return clicked;
}

float MuiContext::scroll(const std::string& id, const MuiRect& rect, float contentHeight)
{
    return scrollAlong(id, rect, contentHeight, false);
}

float MuiContext::scrollHorizontal(const std::string& id, const MuiRect& rect, float contentWidth)
{
    return scrollAlong(id, rect, contentWidth, true);
}

float MuiContext::scrollAlong(const std::string& id, const MuiRect& rect, float contentSize, bool horizontal)
{
    region(rect);
    gFrameWidgets[id] = rect;

    ScrollState& state = gScrolls[id];
    float maxOffset = std::max(contentSize - (horizontal ? rect.w : rect.h), 0.0f);
    unsigned int dt = state.lastUpdate != 0 ? now - state.lastUpdate : 0;
    state.lastUpdate = now;

    if (isInteractive) {
        bool startedInside = rect.contains(gPointer.startX, gPointer.startY);

        if (gPointer.pressed && startedInside) {
            // Touch stops a fling.
            state.velocity = 0.0f;
            state.dragging = false;
            state.lastY = horizontal ? gPointer.x : gPointer.y;
            state.lastTime = now;
        }

        float slop = dp(kTheme.touchSlop);
        // Along the scroll and across it.
        float dy = horizontal ? gPointer.x - gPointer.startX : gPointer.y - gPointer.startY;
        float dx = horizontal ? gPointer.y - gPointer.startY : gPointer.x - gPointer.startX;
        bool ownedByChild = gActiveId.empty() || gActiveId.rfind(id + ".", 0) == 0;
        // Content that fits doesn't scroll, the finger stays with the child.
        if (gPointer.down && startedInside && !state.dragging && ownedByChild && maxOffset > 0.0f
            && std::fabs(dy) > slop && std::fabs(dy) > std::fabs(dx)) {
            state.dragging = true;
            gActiveId = id;
            state.lastY = horizontal ? gPointer.x : gPointer.y;
            state.lastTime = now;
        }

        if (state.dragging && gActiveId == id) {
            if (gPointer.down || gPointer.released) {
                float move = (horizontal ? gPointer.x : gPointer.y) - state.lastY;
                unsigned int elapsed = std::max(now - state.lastTime, 1u);
                state.offset -= move;
                if (move != 0.0f || elapsed > 50) {
                    // Smoothed finger speed for the fling.
                    state.velocity = state.velocity * 0.3f + (-move / elapsed) * 0.7f;
                    state.lastTime = now;
                }
                state.lastY = horizontal ? gPointer.x : gPointer.y;
            }

            if (gPointer.released) {
                state.dragging = false;
            }
        } else if (state.dragging) {
            state.dragging = false;
        }
    }

    if (!state.dragging && state.velocity != 0.0f && dt > 0) {
        state.offset += state.velocity * dt;
        state.velocity *= std::exp(-static_cast<float>(dt) / 325.0f);
        if (std::fabs(state.velocity) < 0.02f) {
            state.velocity = 0.0f;
        }
    }

    if (state.offset <= 0.0f || state.offset >= maxOffset) {
        state.offset = std::clamp(state.offset, 0.0f, maxOffset);
        if (!state.dragging) {
            state.velocity = 0.0f;
        }
    }

    return state.offset;
}

void MuiContext::setScroll(const std::string& id, float offset)
{
    ScrollState& state = gScrolls[id];
    state.offset = std::max(offset, 0.0f);
    state.velocity = 0.0f;
}

bool MuiContext::isScrolling(const std::string& id) const
{
    auto it = gScrolls.find(id);
    return it != gScrolls.end() && (it->second.dragging || it->second.velocity != 0.0f);
}

bool MuiContext::dragSource(const std::string& id, int payload, bool anyDirection)
{
    if (!isInteractive) {
        return false;
    }

    if (gDrag.active) {
        return gDrag.source == id;
    }

    if (gActiveId != id || !gPointer.down || !gPointer.moved || gActiveLongPressed) {
        return false;
    }

    float dx = gPointer.x - gPointer.startX;
    float dy = gPointer.y - gPointer.startY;
    if (!anyDirection && std::fabs(dx) <= std::fabs(dy)) {
        return false;
    }

    gDrag.active = true;
    gDrag.source = id;
    gDrag.payload = payload;
    gActiveId = kDragActiveId;
    return true;
}

bool MuiContext::dragging(int* payload) const
{
    if (!gDrag.active) {
        return false;
    }

    if (payload != nullptr) {
        *payload = gDrag.payload;
    }
    return true;
}

bool MuiContext::dropped(const MuiRect& rect, int* payload)
{
    if (!isInteractive || !gDrag.active || !gPointer.released || !rect.contains(gPointer.x, gPointer.y)) {
        return false;
    }

    if (payload != nullptr) {
        *payload = gDrag.payload;
    }

    // Once.
    gDrag.source.clear();
    gDrag.active = false;
    return true;
}

bool MuiContext::panZoom(const std::string& id, const MuiRect& rect, MuiPanZoom* view, bool* tapped)
{
    region(rect);
    gFrameWidgets[id] = rect;

    if (tapped != nullptr) {
        *tapped = false;
    }

    PanZoomState& state = gPanZooms[id];
    if (!isInteractive) {
        state.active = false;
        return false;
    }

    // Not a touch begun before the screen showed (see `touchable`).
    if (gPointer.pressed && gActiveId.empty() && rect.contains(gPointer.startX, gPointer.startY)
        && !(current != nullptr && static_cast<int>(gPointer.downTime - current->openTime) < 0)) {
        gActiveId = id;
        state = PanZoomState();
        state.active = true;
        state.lastX = gPointer.x;
        state.lastY = gPointer.y;
    }

    if (gActiveId != id || !state.active) {
        return false;
    }

    if (gSecondFinger.down && gPointer.down) {
        // Pinch: the content point between the fingers stays between them.
        float midX = (gPointer.x + gSecondFinger.x) / 2.0f;
        float midY = (gPointer.y + gSecondFinger.y) / 2.0f;
        float distance = std::hypot(gSecondFinger.x - gPointer.x, gSecondFinger.y - gPointer.y);

        if (state.pinching && state.lastDistance > 0.0f && distance > 0.0f) {
            float contentX = view->centerX + (state.lastX - rect.centerX()) / view->zoom;
            float contentY = view->centerY + (state.lastY - rect.centerY()) / view->zoom;
            view->zoom = std::clamp(view->zoom * distance / state.lastDistance, view->minZoom, view->maxZoom);
            view->centerX = contentX - (midX - rect.centerX()) / view->zoom;
            view->centerY = contentY - (midY - rect.centerY()) / view->zoom;
        }

        state.pinching = true;
        state.pinched = true;
        state.lastX = midX;
        state.lastY = midY;
        state.lastDistance = distance;
    } else if (gPointer.down) {
        // Drag (after a pinch from where the finger is, no jump).
        if (!state.pinching) {
            view->centerX -= (gPointer.x - state.lastX) / view->zoom;
            view->centerY -= (gPointer.y - state.lastY) / view->zoom;
        }
        state.pinching = false;
        state.lastX = gPointer.x;
        state.lastY = gPointer.y;
    }

    if (gPointer.released) {
        state.active = false;
        if (tapped != nullptr && !state.pinched && !gPointer.moved) {
            *tapped = true;
        }
    }

    return true;
}

std::u32string MuiContext::takeTextInput()
{
    std::u32string text;
    text.swap(gTextInput);
    return text;
}

float MuiContext::pointerX() const
{
    return gPointer.x;
}

float MuiContext::pointerY() const
{
    return gPointer.y;
}

bool MuiContext::pointerDown() const
{
    return gPointer.down;
}

float MuiContext::slider(const std::string& id, const MuiRect& rect, float value, float minValue, float maxValue, bool* dragging)
{
    // Taller touch area than the track.
    MuiRect touchRect = rect.inset(0.0f, -dp(10.0f));
    bool pressed;
    touchable(id, touchRect, &pressed);

    bool held = isInteractive && gActiveId == id && gPointer.down && maxValue > minValue;
    if (held) {
        float ratio = std::clamp((gPointer.x - rect.x) / std::max(rect.w, 1.0f), 0.0f, 1.0f);
        value = minValue + ratio * (maxValue - minValue);
    }

    if (dragging != nullptr) {
        *dragging = held;
    }

    float ratio = maxValue > minValue ? std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f) : 0.0f;
    float trackHeight = dp(6.0f);
    MuiRect track = { rect.x, rect.centerY() - trackHeight / 2.0f, rect.w, trackHeight };
    muiFillRoundRect(track, trackHeight / 2.0f, kTheme.button);
    muiStrokeRoundRect(track, trackHeight / 2.0f, dp(1.0f), kTheme.buttonBorder);
    muiFillRoundRect({ track.x, track.y, track.w * ratio, track.h }, trackHeight / 2.0f, kTheme.buttonPrimary);

    float thumbRadius = dp(pressed ? 13.0f : 11.0f);
    float thumbX = rect.x + rect.w * ratio;
    muiFillCircle(thumbX, rect.centerY(), thumbRadius, kTheme.buttonPrimary);
    muiStrokeCircle(thumbX, rect.centerY(), thumbRadius, dp(1.5f), kTheme.accent);

    return value;
}

bool MuiContext::toggle(const std::string& id, const MuiRect& rect, bool* value)
{
    bool pressed;
    bool tapped = touchable(id, rect.inset(-dp(6.0f)), &pressed);
    if (tapped) {
        *value = !*value;
    }

    // Track with the knob at the side of the value.
    float height = std::min(rect.h, dp(24.0f));
    float width = std::min(rect.w, height * 1.9f);
    MuiRect track = { rect.right() - width, rect.centerY() - height / 2.0f, width, height };
    muiFillRoundRect(track, height / 2.0f, *value ? kTheme.buttonPrimary : kTheme.button);
    muiStrokeRoundRect(track, height / 2.0f, dp(1.0f), pressed ? kTheme.accent : kTheme.buttonBorder);

    float radius = height / 2.0f - dp(3.0f);
    float knobX = *value ? track.right() - height / 2.0f : track.x + height / 2.0f;
    muiFillCircle(knobX, track.centerY(), radius, *value ? kTheme.buttonPrimaryText : kTheme.accent);

    return tapped;
}

bool MuiContext::segmented(const std::string& id, const MuiRect& rect, const std::vector<std::u32string>& labels, int* selected)
{
    if (labels.empty()) {
        return false;
    }

    // One text size for all, as large as fits; each cell as wide as its
    // label with the room left shared equally.
    float padding = dp(12.0f);
    float size = dp(kTheme.buttonTextSize) * 0.9f;
    float textWidth = 0.0f;
    for (const std::u32string& label : labels) {
        textWidth += muiTextWidth(label, size);
    }
    float room = rect.w - padding * labels.size();
    if (textWidth > room && textWidth > 0.0f) {
        size = std::max(size * room / textWidth, dp(10.0f));
        textWidth = 0.0f;
        for (const std::u32string& label : labels) {
            textWidth += muiTextWidth(label, size);
        }
    }
    float extra = std::max(room - textWidth, 0.0f) / labels.size();

    bool changed = false;
    float radius = dp(kTheme.radius);
    muiFillRoundRect(rect, radius, kTheme.button);

    float x = rect.x;
    for (size_t index = 0; index < labels.size(); index++) {
        float width = index + 1 == labels.size() ? rect.right() - x : muiTextWidth(labels[index], size) + padding + extra;
        MuiRect cell = { x, rect.y, width, rect.h };
        x += width;

        bool pressed;
        if (touchable(id + "." + std::to_string(index), cell, &pressed) && *selected != static_cast<int>(index)) {
            *selected = static_cast<int>(index);
            changed = true;
        }

        bool on = *selected == static_cast<int>(index);
        if (on || pressed) {
            muiFillRoundRect(cell.inset(dp(2.0f)), radius - dp(2.0f), pressed && !on ? kTheme.buttonPressed : kTheme.buttonPrimary);
        }

        MuiColor color = on ? kTheme.buttonPrimaryText : kTheme.buttonText;
        muiDrawTextAligned(labels[index], cell, size, color, MuiAlign::Center, MuiAlign::Center);
    }

    muiStrokeRoundRect(rect, radius, dp(1.0f), kTheme.buttonBorder);
    return changed;
}

bool MuiContext::tappedOutside(const MuiRect& rect)
{
    if (!isInteractive || !gPointer.released || !gActiveId.empty()) {
        return false;
    }

    // Not the finger which was down when the screen opened (it opened on a
    // long press, the finger is lifted elsewhere).
    if (current != nullptr && static_cast<int>(gPointer.downTime - current->openTime) < 0) {
        return false;
    }

    return !rect.contains(gPointer.startX, gPointer.startY) && !rect.contains(gPointer.x, gPointer.y) && !gPointer.moved;
}

void muiPush(MuiScreen* screen)
{
    muiRemove(screen);
    screen->finished = false;
    screen->openTime = SDL_GetTicks();
    gScreens.push_back(screen);

    // New screen doesn't continue a touch that started below it.
    gActiveId.clear();
}

void muiRemove(MuiScreen* screen)
{
    auto it = std::find(gScreens.begin(), gScreens.end(), screen);
    if (it != gScreens.end()) {
        gScreens.erase(it);
        gActiveId.clear();
    }
}

bool muiHasModal()
{
    for (MuiScreen* screen : gScreens) {
        if (screen->modal && screen->isActive()) {
            return true;
        }
    }
    return false;
}

bool muiHidesHud()
{
    for (MuiScreen* screen : gScreens) {
        if (screen->hidesHud() && screen->isActive()) {
            return true;
        }
    }
    return false;
}

bool muiCoversScreen()
{
    for (MuiScreen* screen : gScreens) {
        if (screen->coversScreen() && screen->isActive()) {
            return true;
        }
    }
    return false;
}

// Switch between full screens asked for by this screen (see
// `muiHoldFrameForSwitch`).
static bool gSwitchPending = false;
static MuiScreen* gSwitchFrom = nullptr;

void muiHoldFrameForSwitch(MuiScreen* from)
{
    gSwitchPending = true;
    gSwitchFrom = from;
}

bool muiHoldsFrame()
{
    // Safety net: never hold longer than this.
    constexpr unsigned int kMaxHoldMs = 500;
    static unsigned int holdStart = 0;

    bool hold = false;
    for (MuiScreen* screen : gScreens) {
        if (screen->holdsFrame()) {
            hold = true;
            break;
        }
    }

    // Until the next screen covers everything.
    if (gSwitchPending) {
        bool arrived = false;
        for (MuiScreen* screen : gScreens) {
            if (screen != gSwitchFrom && screen->isActive() && screen->coversScreen()) {
                arrived = true;
                break;
            }
        }

        if (arrived) {
            gSwitchPending = false;
        } else {
            hold = true;
        }
    }

    if (!hold) {
        holdStart = 0;
        return false;
    }

    unsigned int now = SDL_GetTicks();
    if (holdStart == 0) {
        holdStart = now;
    }

    if (now - holdStart >= kMaxHoldMs) {
        // The next screen didn't come (e.g. not enough action points).
        gSwitchPending = false;
        return false;
    }
    return true;
}

void muiRunModal(MuiScreen* screen)
{
    muiPush(screen);

    while (!screen->finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen->back();
        } else if (keyCode != -1) {
            screen->key(keyCode);
        }

        devAutotestTick();

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    muiRemove(screen);
}

static bool gBackRequested = false;

void muiRequestBack()
{
    gBackRequested = true;
}

// A requested Back goes to the topmost modal screen, or to the overlays
// when there's none.
static void muiDispatchBack(const std::vector<MuiScreen*>& screens)
{
    for (auto it = screens.rbegin(); it != screens.rend(); ++it) {
        if ((*it)->isActive() && (*it)->modal) {
            (*it)->back();
            return;
        }
    }

    for (auto it = screens.rbegin(); it != screens.rend(); ++it) {
        if ((*it)->isActive()) {
            (*it)->back();
        }
    }
}

void muiRender(SDL_Renderer* renderer)
{
    if (gScreens.empty()) {
        // Nothing to go back from.
        gBackRequested = false;

        // Touches may be captured by a screen that is gone. Its pointer
        // events are dropped: a screen shown later would take them as taps.
        gRegions.clear();
        gWidgets.clear();
        gEvents.clear();
        gPointer = Pointer();
        return;
    }

    if (SDL_GetRendererOutputSize(renderer, &gOutputWidth, &gOutputHeight) != 0) {
        return;
    }

    gPixelsPerDp = hudGetScreenDensity() * static_cast<float>(settings.touch.hud_scale) / 100.0f;

    unsigned int now = SDL_GetTicks();
    updateSafeRect(now);
    processEvents();

    // Draw in output pixels.
    int logicalWidth;
    int logicalHeight;
    SDL_RenderGetLogicalSize(renderer, &logicalWidth, &logicalHeight);
    SDL_RenderSetLogicalSize(renderer, 0, 0);

    muiDrawBegin(renderer);

    gFrameRegions.clear();
    gFrameWidgets.clear();

    MuiContext context;
    context.pixelsPerDp = gPixelsPerDp;
    context.screen = { 0.0f, 0.0f, static_cast<float>(gOutputWidth), static_cast<float>(gOutputHeight) };
    context.safe = gSafeRect;
    context.now = now;

    // Copy: screens may push/remove screens while building.
    std::vector<MuiScreen*> screens = gScreens;

    // As a screen's own Back button (while building).
    if (gBackRequested) {
        gBackRequested = false;
        muiDispatchBack(screens);
    }

    MuiScreen* top = topScreen();
    for (MuiScreen* screen : screens) {
        if (!screen->isActive()) {
            continue;
        }
        context.current = screen;
        context.isInteractive = screen == top;
        screen->build(context);
    }

    // Over every screen, not touchable.
    context.current = nullptr;
    context.isInteractive = false;
    muiNotifyRender(context);

    muiDrawEnd();

    if (logicalWidth != 0 && logicalHeight != 0) {
        SDL_RenderSetLogicalSize(renderer, logicalWidth, logicalHeight);
    }

    if (gPointer.released) {
        gActiveId.clear();
        // Dropped outside of any target.
        gDrag.active = false;
        gDrag.source.clear();
    }

    gRegions = gFrameRegions;
    gWidgets = gFrameWidgets;
}

void muiResetRenderer()
{
    muiDrawReset();
}

void muiHandleTextInput(const char* text)
{
    gTextInput += muiDecodeUtf8(text);
}

bool muiHandleFingerEvent(const SDL_Event* event)
{
    const SDL_TouchFingerEvent& finger = event->tfinger;
    if (gSdlRenderer != nullptr) {
        SDL_GetRendererOutputSize(gSdlRenderer, &gOutputWidth, &gOutputHeight);
    }
    float x = finger.x * gOutputWidth;
    float y = finger.y * gOutputHeight;

    if (event->type == SDL_FINGERDOWN) {
        if (gFingerCaptured) {
            // Second finger: pinch gestures, more are ignored.
            if (!gSecondFinger.down) {
                gSecondFinger = { true, finger.fingerId, x, y };
            }
            return true;
        }

        bool capture = muiHasModal();
        if (!capture && !gScreens.empty()) {
            for (const MuiRect& rect : gRegions) {
                if (rect.contains(x, y)) {
                    capture = true;
                    break;
                }
            }
        }

        if (!capture) {
            return false;
        }

        gFingerCaptured = true;
        gCapturedFinger = finger.fingerId;
        gEvents.push_back({ PointerEventType::Down, x, y, finger.timestamp });
        return true;
    }

    if (gSecondFinger.down && finger.fingerId == gSecondFinger.id) {
        gSecondFinger.x = x;
        gSecondFinger.y = y;
        if (event->type == SDL_FINGERUP) {
            gSecondFinger.down = false;
        }
        return true;
    }

    if (!gFingerCaptured || finger.fingerId != gCapturedFinger) {
        // Other fingers while UI is modal don't reach the game either.
        return gFingerCaptured || muiHasModal();
    }

    if (event->type == SDL_FINGERMOTION) {
        gEvents.push_back({ PointerEventType::Move, x, y, finger.timestamp });
    } else if (event->type == SDL_FINGERUP) {
        gEvents.push_back({ PointerEventType::Up, x, y, finger.timestamp });
        gFingerCaptured = false;
    }

    return true;
}

bool muiForgetMissingFingers(const SDL_FingerID* down, int count)
{
    auto isDown = [&](SDL_FingerID id) {
        for (int index = 0; index < count; index++) {
            if (down[index] == id) {
                return true;
            }
        }
        return false;
    };

    bool forgotten = false;

    // Released far outside: nothing is tapped.
    if (gFingerCaptured && !isDown(gCapturedFinger)) {
        gEvents.push_back({ PointerEventType::Up, -1.0e6f, -1.0e6f, SDL_GetTicks() });
        gFingerCaptured = false;
        forgotten = true;
    }

    if (gSecondFinger.down && !isDown(gSecondFinger.id)) {
        gSecondFinger.down = false;
        forgotten = true;
    }

    return forgotten;
}

bool muiGetWidgetCenter(const char* id, float* x, float* y)
{
    auto it = gWidgets.find(id);
    if (it == gWidgets.end()) {
        return false;
    }

    *x = it->second.centerX() / gOutputWidth;
    *y = it->second.centerY() / gOutputHeight;
    return true;
}

} // namespace fallout
