#include "mui_screens.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "input.h"
#include "inventory.h"
#include "item.h"
#include "kb.h"
#include "mui_icons.h"
#include "object.h"

namespace fallout {

namespace {

    // Texts in `game\ce.msg`.
    constexpr int kTextTakeAll = 158;
    constexpr int kTextGiveAll = 159;

    const MuiColor kBackground = muiRgb(0x07100A);
    const MuiColor kListBackground = muiRgb(0x0B120D);

    enum Side {
        SIDE_LEFT,
        SIDE_RIGHT,
        SIDE_COUNT,
    };

    const char* const kListIds[SIDE_COUNT] = {
        "loot.left",
        "loot.right",
    };

    // Drag payload: side and index in its list.
    constexpr int kPayloadSideStride = 100000;

    std::u32string number(int value)
    {
        return muiDecodeUtf8(std::to_string(value).c_str());
    }

    // Loot screen: player side (party tabs, filters, grid, "Give all") and
    // what is looted (name, dead critters at the tile, filters, grid, "Take
    // all"). Tap moves an item to the other side, long press opens the
    // game's action menu, dragging moves it too and gives player's items to
    // party members over their tabs. Stealing goes through the same moves
    // (the game checks every one). Runs over the game's looting (see
    // `lootQueueAction`).
    class LootScreen : public MuiScreen {
    public:
        LootScreen()
        {
            modal = true;
        }

        bool coversScreen() override { return !muiUsesWindows(); }
        bool isGameScreen() override { return true; }

        bool isActive() override
        {
            LootView view;
            bool active = muiIsEnabled() && lootGetView(&view);
            if (active && !wasActive) {
                filters[SIDE_LEFT] = MuiItemFilterState();
                filters[SIDE_RIGHT] = MuiItemFilterState();
            }
            wasActive = active;
            return active;
        }

        void build(MuiContext& ui) override;
        void back() override { lootRequestClose(); }

    private:
        bool wasActive = false;
        MuiItemFilterState filters[SIDE_COUNT];
        std::vector<MuiListItem> lists[SIDE_COUNT];
        // Tablet windows: the roomy sizes (`muiIsTallContent`).
        bool tall = false;
        MuiRect listRects[SIDE_COUNT];
        std::vector<MuiRect> partyTabRects;

        void buildHeader(MuiContext& ui, const LootView& view, Side side, const MuiRect& rect);
        void buildPartyTabs(MuiContext& ui, const LootView& view, const MuiRect& rect);
        void buildTargetName(MuiContext& ui, const LootView& view, const MuiRect& rect);
        void buildContainer(MuiContext& ui, Side side, Object* container, const MuiRect& rect);
        void buildList(MuiContext& ui, const LootView& view, Side side, const MuiRect& rect);
        void buildBottom(MuiContext& ui, const LootView& view, Side side, const MuiRect& rect);
        void handleDrop(MuiContext& ui, const LootView& view);
        void drawDragged(MuiContext& ui);

        void queue(LootActionType type, Object* item, bool fromTarget, int index = 0);
    };

    LootScreen gLootScreen;
    bool gLootScreenPushed = false;

    void LootScreen::queue(LootActionType type, Object* item, bool fromTarget, int index)
    {
        LootAction action;
        action.type = type;
        action.item = item;
        action.fromTarget = fromTarget;
        action.index = index;
        lootQueueAction(action);
    }

    void LootScreen::build(MuiContext& ui)
    {
        LootView view;
        if (!lootGetView(&view)) {
            return;
        }

        const MuiTheme& theme = muiTheme();

        // Opaque over the map, or a window over it.
        MuiScreenFrame frame = muiScreenFrame(ui, kBackground);
        const MuiRect& content = frame.content;
        const MuiRect& tabs = frame.tabs;

        MuiRect back = { tabs.x, tabs.y, tabs.w, tabs.w };
        bool backPressed;
        if (ui.touchable("loot.back", back, &backPressed) || frame.tappedOutside) {
            lootRequestClose();
        }
        muiFillRoundRect(back, ui.dp(theme.radius), backPressed ? theme.buttonPressed : theme.button);
        muiStrokeRoundRect(back, ui.dp(theme.radius), ui.dp(theme.borderWidth), backPressed ? theme.accent : theme.buttonBorder);
        muiDrawIcon(MuiIcon::Back, back.centerX(), back.centerY(), back.w * 0.46f, theme.accent);

        lists[SIDE_LEFT] = muiInventoryItems(view.leftOwner, filters[SIDE_LEFT]);
        lists[SIDE_RIGHT] = muiInventoryItems(view.rightOwner, filters[SIDE_RIGHT]);

        tall = muiIsTallContent(ui);
        float gap = ui.dp(12.0f);
        float sideWidth = (content.w - gap) / 2.0f;
        float headerHeight = ui.dp(tall ? 36.0f : 30.0f);
        float filterHeight = ui.dp(tall ? 40.0f : 30.0f);
        float bottomHeight = ui.dp(tall ? 44.0f : 34.0f);
        float rowGap = ui.dp(tall ? 8.0f : 6.0f);

        for (int side = 0; side < SIDE_COUNT; side++) {
            float x = content.x + side * (sideWidth + gap);
            float y = content.y;
            buildHeader(ui, view, static_cast<Side>(side), { x, y, sideWidth, headerHeight });
            y += headerHeight + rowGap;

            std::string filterId = side == SIDE_LEFT ? "loot.filter.left" : "loot.filter.right";
            muiItemFilterRow(ui, filterId, { x, y, sideWidth, filterHeight }, &filters[side]);
            y += filterHeight + rowGap;

            float listHeight = content.bottom() - bottomHeight - rowGap - y;
            buildList(ui, view, static_cast<Side>(side), { x, y, sideWidth, listHeight });
            buildBottom(ui, view, static_cast<Side>(side), { x, content.bottom() - bottomHeight, sideWidth, bottomHeight });
        }

        handleDrop(ui, view);
        drawDragged(ui);
    }

    // Left: party tabs or the looter's name; right: what is looted. An open
    // container replaces them with a way back.
    void LootScreen::buildHeader(MuiContext& ui, const LootView& view, Side side, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        Object* container = side == SIDE_LEFT ? view.leftContainer : view.rightContainer;
        if (container != nullptr) {
            buildContainer(ui, side, container, rect);
            return;
        }

        bool tabsHere = view.party.size() > 1 && (side == SIDE_RIGHT) == view.partySwitchesTarget;
        if (tabsHere) {
            buildPartyTabs(ui, view, rect);
        } else if (side == SIDE_LEFT) {
            muiDrawTextAligned(muiDecodeGameText(objectGetName(view.looter)), rect, ui.dp(tall ? 16.0f : 14.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);
        } else {
            buildTargetName(ui, view, rect);
        }
    }

    // Party members; the looter side (or the looted one when stealing from
    // party members) shows the chosen one. Player's items dropped on a tab go
    // to that member.
    void LootScreen::buildPartyTabs(MuiContext& ui, const LootView& view, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        partyTabRects.clear();

        int dragPayload;
        bool dragFromLeft = !view.partySwitchesTarget && ui.dragging(&dragPayload) && dragPayload / kPayloadSideStride == SIDE_LEFT;

        float gap = ui.dp(3.0f);
        int count = static_cast<int>(view.party.size());
        float width = (rect.w - gap * (count - 1)) / count;
        for (int index = 0; index < count; index++) {
            MuiRect tab = { rect.x + index * (width + gap), rect.y, width, rect.h };
            partyTabRects.push_back(tab);

            bool active = index == view.partyIndex;
            bool pressed;
            bool tapped = ui.touchable("loot.party." + std::to_string(index), tab, &pressed);

            bool reachable = inventoryPartyMemberIsReachable(view.party[index]);
            bool target = dragFromLeft && !active && reachable;
            MuiColor fill = pressed ? theme.buttonPressed : (active ? theme.buttonPrimary : theme.button);
            MuiColor color = active && !pressed ? theme.buttonPrimaryText : theme.accent;
            muiFillRoundRect(tab, ui.dp(5.0f), fill);
            muiStrokeRoundRect(tab, ui.dp(5.0f), ui.dp(target ? 2.0f : 1.0f), target || active ? theme.accent : theme.buttonBorder);

            MuiRect label = tab.inset(ui.dp(4.0f), 0.0f);
            muiPushClip(label);
            muiDrawTextAligned(muiDecodeGameText(objectGetName(view.party[index])), label, ui.dp(tall ? 14.0f : 12.0f), color, MuiAlign::Center, MuiAlign::Center);
            muiPopClip();
            if (!reachable) {
                muiDrawUnavailableOverlay(ui, tab, 5.0f);
            }

            if (tapped && !active) {
                if (reachable) {
                    queue(LootActionType::SelectPartyMember, nullptr, false, index);
                } else {
                    muiNotifyPartyMemberOutOfReach(view.party[index]);
                }
            }
        }
    }

    // Name of what is looted, arrows between dead critters at the tile.
    void LootScreen::buildTargetName(MuiContext& ui, const LootView& view, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        MuiRect label = rect;

        if (view.targetCount > 1) {
            float arrow = rect.h * 1.2f;
            MuiRect previous = { rect.x, rect.y, arrow, rect.h };
            MuiRect next = { rect.right() - arrow, rect.y, arrow, rect.h };
            if (ui.button("loot.target.previous", previous, U"‹")) {
                queue(LootActionType::SwitchTarget, nullptr, true, -1);
            }
            if (ui.button("loot.target.next", next, U"›")) {
                queue(LootActionType::SwitchTarget, nullptr, true, 1);
            }
            label = { previous.right() + ui.dp(8.0f), rect.y, next.x - previous.right() - ui.dp(16.0f), rect.h };
        }

        muiPushClip(label);
        muiDrawTextAligned(muiDecodeGameText(objectGetName(view.target)), label, ui.dp(tall ? 16.0f : 14.0f), theme.accent, view.targetCount > 1 ? MuiAlign::Center : MuiAlign::Start, MuiAlign::Center);
        muiPopClip();
    }

    void LootScreen::buildContainer(MuiContext& ui, Side side, Object* container, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        bool pressed;
        if (ui.touchable(side == SIDE_LEFT ? "loot.container.left" : "loot.container.right", rect, &pressed)) {
            queue(LootActionType::CloseContainer, nullptr, side == SIDE_RIGHT);
        }

        muiFillRoundRect(rect, ui.dp(5.0f), pressed ? theme.buttonPressed : theme.button);
        muiStrokeRoundRect(rect, ui.dp(5.0f), ui.dp(1.0f), pressed ? theme.accent : theme.buttonBorder);
        float iconSize = rect.h * 0.5f;
        muiDrawIcon(MuiIcon::Back, rect.x + ui.dp(8.0f) + iconSize / 2.0f, rect.centerY(), iconSize, theme.accent);
        MuiRect label = { rect.x + iconSize + ui.dp(16.0f), rect.y, rect.w - iconSize - ui.dp(20.0f), rect.h };
        muiPushClip(label);
        muiDrawTextAligned(muiDecodeGameText(objectGetName(container)), label, ui.dp(14.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);
        muiPopClip();
    }

    void LootScreen::buildList(MuiContext& ui, const LootView& view, Side side, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        listRects[side] = rect;

        // The other side's items go here.
        int dragPayload;
        bool target = ui.dragging(&dragPayload) && dragPayload / kPayloadSideStride != side;

        muiFillRoundRect(rect, ui.dp(6.0f), kListBackground);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(target ? 2.0f : 1.0f), target ? theme.accent : theme.panelBorder);

        float gap = ui.dp(tall ? 6.0f : 4.0f);
        MuiRect area = rect.inset(gap);
        int columns = muiItemColumns(ui, area.w, gap, tall);
        float cell = (area.w - gap * (columns - 1)) / columns;
        const std::vector<MuiListItem>& items = lists[side];
        int rows = (static_cast<int>(items.size()) + columns - 1) / columns;
        float contentHeight = rows * (cell + gap);

        std::string id = kListIds[side];
        float offset = ui.scroll(id, area, contentHeight);
        bool scrolling = ui.isScrolling(id);
        // Vertical moves scroll only a list that doesn't fit.
        bool scrollable = contentHeight > area.h;

        muiPushClip(area);
        for (int index = 0; index < static_cast<int>(items.size()); index++) {
            MuiRect cellRect = { area.x + (index % columns) * (cell + gap), area.y - offset + (index / columns) * (cell + gap), cell, cell };
            if (cellRect.bottom() < area.y || cellRect.y > area.bottom()) {
                continue;
            }

            MuiRect touchRect = cellRect;
            touchRect.y = std::max(cellRect.y, area.y);
            touchRect.h = std::min(cellRect.bottom(), area.bottom()) - touchRect.y;

            Object* item = items[index].item;
            std::string cellId = id + "." + std::to_string(index);
            ui.mark(id + ".pid." + std::to_string(item->pid), touchRect);

            bool pressed;
            bool longPressed;
            bool tapped = ui.touchable(cellId, touchRect, &pressed, &longPressed);
            bool dragged = ui.dragSource(cellId, side * kPayloadSideStride + index, !scrollable);

            if (dragged) {
                // Placeholder, the item follows the finger.
                muiStrokeRoundRect(cellRect, ui.dp(5.0f), ui.dp(1.0f), theme.buttonBorder);
                continue;
            }

            muiDrawItemCell(ui, cellRect, item, items[index].quantity, pressed);

            if (longPressed && !scrolling) {
                LootAction action;
                action.type = LootActionType::OpenActionMenu;
                action.item = item;
                action.fromTarget = side == SIDE_RIGHT;
                muiToScreen(ui.pointerX(), ui.pointerY(), &(action.x), &(action.y));
                lootQueueAction(action);
            } else if (tapped && !scrolling) {
                queue(LootActionType::Move, item, side == SIDE_RIGHT);
            }
        }
        muiPopClip();
    }

    // Mirrored around the center: "Give all" and "Take all" of the shown
    // items (not when stealing) next to each other in the middle, weights at
    // the outer edges (with the carry weight for the looter).
    void LootScreen::buildBottom(MuiContext& ui, const LootView& view, Side side, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        // The right side as the left one: weight / carry weight of critters,
        // weight of the rest; containers also their room (a volume).
        bool over = false;
        std::u32string weight = side == SIDE_LEFT
            ? number(view.weight) + (view.carryWeight > 0 ? U"/" + number(view.carryWeight) : U"")
            : muiCarriedText(view.rightOwner, &over);
        int used;
        int capacity;
        std::u32string volume;
        if (side == SIDE_RIGHT && muiContainerVolume(view.rightOwner, &used, &capacity)) {
            volume = number(used) + U"/" + number(capacity);
        }

        float iconSize = ui.dp(tall ? 20.0f : 15.0f);
        float textSize = ui.dp(tall ? 17.0f : 14.0f);
        float edge = ui.dp(2.0f);
        float iconGap = ui.dp(6.0f);
        float weightWidth = iconSize + iconGap + muiTextWidth(weight, textSize);
        float volumeWidth = volume.empty() ? 0.0f : iconSize + iconGap + muiTextWidth(volume, textSize);
        float partsGap = volume.empty() ? 0.0f : ui.dp(14.0f);
        float weightX = side == SIDE_LEFT ? rect.x + edge : rect.right() - edge - volumeWidth - partsGap - weightWidth;
        muiDrawIcon(MuiIcon::Weight, weightX + iconSize / 2.0f, rect.centerY(), iconSize, theme.textDim);
        MuiRect weightRect = { weightX + iconSize + iconGap, rect.y, weightWidth, rect.h };
        muiDrawTextAligned(weight, weightRect, textSize, over ? kMuiOverLimit : theme.textDim, MuiAlign::Start, MuiAlign::Center);
        if (!volume.empty()) {
            float volumeX = rect.right() - edge - volumeWidth;
            muiDrawIcon(MuiIcon::Inventory, volumeX + iconSize / 2.0f, rect.centerY(), iconSize, theme.textDim);
            muiDrawTextAligned(volume, { volumeX + iconSize + iconGap, rect.y, volumeWidth, rect.h }, textSize, used > capacity ? kMuiOverLimit : theme.textDim, MuiAlign::Start, MuiAlign::Center);
        }

        if (view.steal) {
            return;
        }

        const std::vector<MuiListItem>& items = lists[side];
        float buttonWidth = rect.w * 0.55f;
        MuiRect button = { side == SIDE_LEFT ? rect.right() - buttonWidth : rect.x, rect.y, buttonWidth, rect.h };
        bool pressed;
        if (ui.touchable(side == SIDE_LEFT ? "loot.give_all" : "loot.take_all", button, &pressed) && !items.empty()) {
            LootAction action;
            action.type = side == SIDE_LEFT ? LootActionType::GiveAll : LootActionType::TakeAll;
            for (const MuiListItem& entry : items) {
                action.items.push_back(entry.item);
            }
            lootQueueAction(action);
        }

        bool primary = side == SIDE_RIGHT;
        MuiColor fill = pressed ? theme.buttonPressed : (primary ? theme.buttonPrimary : theme.button);
        MuiColor color = primary && !pressed ? theme.buttonPrimaryText : theme.accent;
        muiFillRoundRect(button, ui.dp(6.0f), fill);
        muiStrokeRoundRect(button, ui.dp(6.0f), ui.dp(1.0f), primary || pressed ? theme.accent : theme.buttonBorder);

        // Arrow towards where the items go.
        std::u32string label = muiDecodeGameText(primary ? muiText(kTextTakeAll, "TAKE ALL") : muiText(kTextGiveAll, "GIVE ALL"));
        textSize = ui.dp(tall ? 15.0f : 13.0f);
        float arrowSize = ui.dp(tall ? 16.0f : 14.0f);
        float arrowGap = ui.dp(6.0f);
        float textWidth = std::min(muiTextWidth(label, textSize), button.w - arrowSize - arrowGap - ui.dp(12.0f));
        float start = button.centerX() - (textWidth + arrowGap + arrowSize) / 2.0f;
        float arrowX = primary ? start + arrowSize / 2.0f : start + textWidth + arrowGap + arrowSize / 2.0f;
        float textX = primary ? start + arrowSize + arrowGap : start;
        MuiRect textRect = { textX, button.y, textWidth, button.h };
        muiPushClip(textRect);
        muiDrawTextAligned(label, textRect, textSize, color, MuiAlign::Start, MuiAlign::Center);
        muiPopClip();

        // Back icon points left; "Give all" points right.
        if (primary) {
            muiDrawIcon(MuiIcon::Back, arrowX, button.centerY(), arrowSize, color);
        } else {
            float half = arrowSize / 2.0f;
            float y = button.centerY();
            muiDrawPolyline({ { arrowX - half, y }, { arrowX + half, y } }, ui.dp(1.6f), color);
            muiDrawPolyline({ { arrowX + half * 0.1f, y - half * 0.72f }, { arrowX + half, y }, { arrowX + half * 0.1f, y + half * 0.72f } }, ui.dp(1.6f), color);
        }
    }

    void LootScreen::handleDrop(MuiContext& ui, const LootView& view)
    {
        int payload;
        if (!ui.dragging(&payload)) {
            return;
        }

        int side = payload / kPayloadSideStride;
        int index = payload % kPayloadSideStride;
        if (side < 0 || side >= SIDE_COUNT || index >= static_cast<int>(lists[side].size())) {
            return;
        }
        Object* item = lists[side][index].item;

        // Player side item over another party member's tab: give it.
        if (side == SIDE_LEFT && !view.partySwitchesTarget) {
            for (int tab = 0; tab < static_cast<int>(partyTabRects.size()); tab++) {
                if (tab != view.partyIndex && ui.dropped(partyTabRects[tab], nullptr)) {
                    if (inventoryPartyMemberIsReachable(view.party[tab])) {
                        queue(LootActionType::GiveToPartyMember, item, false, tab);
                    } else {
                        muiNotifyPartyMemberOutOfReach(view.party[tab]);
                    }
                    return;
                }
            }
        }

        int other = side == SIDE_LEFT ? SIDE_RIGHT : SIDE_LEFT;
        if (ui.dropped(listRects[other], nullptr)) {
            queue(LootActionType::Move, item, side == SIDE_RIGHT);
        }
    }

    // Dragged item under the finger with its name and weight.
    void LootScreen::drawDragged(MuiContext& ui)
    {
        int payload;
        if (!ui.dragging(&payload)) {
            return;
        }

        int side = payload / kPayloadSideStride;
        int index = payload % kPayloadSideStride;
        if (side < 0 || side >= SIDE_COUNT || index >= static_cast<int>(lists[side].size())) {
            return;
        }

        const MuiListItem& entry = lists[side][index];
        muiDrawDraggedItem(ui, entry.item, entry.quantity, -1);
    }

} // namespace

void muiLootInit()
{
    if (!muiIsEnabled() || gLootScreenPushed) {
        return;
    }

    muiPush(&gLootScreen);
    gLootScreenPushed = true;
}

} // namespace fallout
