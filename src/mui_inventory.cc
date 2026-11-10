#include "mui_screens.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "game_mouse.h"
#include "input.h"
#include "inventory.h"
#include "item.h"
#include "kb.h"
#include "mui_icons.h"
#include "object.h"
#include "proto.h"
#include "proto_types.h"
#include "stat.h"

namespace fallout {

namespace {

    // What dropping the dragged item on a target does, in its top left
    // corner: a small round button as the HUD's (its fill and border, the
    // icon bright).
    void drawDropBadge(MuiContext& ui, float x, float y, float iconSize, MuiIcon icon)
    {
        const MuiTheme& theme = muiTheme();
        float radius = iconSize * 0.8f;
        muiFillCircle(x, y, radius, theme.button);
        muiStrokeCircle(x, y, radius, ui.dp(1.5f), theme.buttonBorder);
        muiDrawIcon(icon, x, y, iconSize, theme.accent);
    }

    // Turn hint under the figure (graphics, not an icon: a thin line however
    // wide): the front of a wide flat ellipse, heads at both ends pointing
    // sideways, centered at ([x], [y]) in a [size] square.
    void drawTurnHint(float x, float y, float size, float thickness, MuiColor color)
    {
        constexpr float kPi = 3.14159265f;
        constexpr float kRadiusX = 0.8f;
        constexpr float kRadiusY = 0.3f;
        constexpr float kCenterY = -0.2f;
        constexpr float kFrom = 55.0f;
        constexpr float kTo = 125.0f;
        auto point = [&](float px, float py) {
            return SDL_FPoint { x + px * size, y + py * size };
        };

        std::vector<SDL_FPoint> points;
        for (int step = 0; step <= 12; step++) {
            float angle = (kFrom + (kTo - kFrom) * step / 12.0f) * kPi / 180.0f;
            points.push_back(point(std::cos(angle) * kRadiusX, kCenterY + std::sin(angle) * kRadiusY));
        }
        muiDrawPolyline(points, thickness, color);

        for (float end : { kFrom, kTo }) {
            float angle = end * kPi / 180.0f;
            float px = std::cos(angle) * kRadiusX;
            float py = kCenterY + std::sin(angle) * kRadiusY;
            float direction = end == kFrom ? -1.0f : 1.0f;
            float tangentX = -std::sin(angle) * kRadiusX * direction;
            float tangentY = std::cos(angle) * kRadiusY * direction;
            float length = std::sqrt(tangentX * tangentX + tangentY * tangentY);
            tangentX /= length;
            tangentY /= length;
            float head = 0.14f;
            muiDrawPolyline({ point(px - tangentX * head - tangentY * head * 0.8f, py - tangentY * head + tangentX * head * 0.8f),
                                point(px, py),
                                point(px - tangentX * head + tangentY * head * 0.8f, py - tangentY * head - tangentX * head * 0.8f) },
                thickness, color);
        }
    }

    // Texts in `game\ce.msg`.
    constexpr int kTextDrop = 147;
    constexpr int kTextDropAll = 156;
    constexpr int kTextSure = 157;

    // Taps on "Drop all" within this time confirm it.
    constexpr unsigned int kConfirmMs = 3000;

    const MuiColor kBackground = muiRgb(0x07100A);
    const MuiColor kListBackground = muiRgb(0x0B120D);
    const MuiColor kDropZone = muiRgb(0x1A2A10);
    const MuiColor kWarning = muiRgb(0xFFB000);
    const MuiColor kEncumbered = kMuiOverLimit;
    // An empty magazine's count, red as on the HUD.
    const MuiColor kAmmoEmpty = muiRgb(0xFF4A3A);

    // Glow over what an action changed, ms.
    constexpr unsigned int kFlashMs = 800;

    // Finger movement per character turn step, dp.
    constexpr float kTurnStepDp = 28.0f;

    // Drag payload: list index, or slot.
    constexpr int kPayloadSlotBase = 100000;

    // Tall layout's info rows, dp: the most spaced out ones everything fits
    // with (0 - the font's line).
    constexpr float kInfoRowSteps[] = { 22.0f, 20.0f, 0.0f };

    std::u32string number(int value)
    {
        return muiDecodeUtf8(std::to_string(value).c_str());
    }

    std::u32string trimmed(const std::string& text)
    {
        size_t start = text.find_first_not_of(' ');
        return start == std::string::npos ? std::u32string() : muiDecodeGameText(text.c_str() + start);
    }

    // Inventory screen: items (filters, grid), character with armor and
    // hand slots, character summary or description of the chosen item. Tap
    // shows the description, long press opens the game's action menu,
    // dragging sideways moves items (hands, armor, into containers, ammo into
    // weapons, to the ground) or uses a consumable on the player's figure.
    // Runs over the game's inventory (see
    // `inventoryQueueAction`).
    class InventoryScreen : public MuiScreen {
    public:
        InventoryScreen()
        {
            modal = true;
        }

        bool coversScreen() override { return !muiUsesWindows(); }
        bool isGameScreen() override { return true; }

        bool isActive() override
        {
            InventoryView view;
            bool active = muiIsEnabled() && inventoryGetView(&view);
            if (active && !wasActive) {
                filter = MuiItemFilterState();
                selected = nullptr;
                examinedVersion = view.examinedVersion;
                lastMenuVersion = view.lastMenuVersion;
                flash = Flash();
                dropAllTime = 0;
            }
            wasActive = active;
            return active;
        }

        void build(MuiContext& ui) override;
        void back() override;

    private:
        struct Cell {
            MuiRect rect;
            Object* item;
        };

        bool wasActive = false;
        // Items on the left, the character over the summary on the right.
        bool tall = false;
        // Its info's rows, dp (`kInfoRowSteps`).
        float infoRowDp = 0.0f;
        MuiItemFilterState filter;
        Object* selected = nullptr;
        unsigned int examinedVersion = 0;
        unsigned int dropAllTime = 0;

        // Soft glow over what the last action changed.
        enum class FlashTarget {
            None,
            Figure,
            Slot,
            Item,
            Header,
        };

        struct Flash {
            FlashTarget target = FlashTarget::None;
            InventorySlot slot = InventorySlot::None;
            Object* item = nullptr;
            unsigned int time = 0;
        };

        Flash flash;
        unsigned int lastMenuVersion = 0;
        unsigned int now = 0;

        void startFlash(FlashTarget target, InventorySlot slot, Object* item)
        {
            flash = { target, slot, item, now };
        }

        void drawFlash(MuiContext& ui, const MuiRect& rect, bool matches);

        // Turning the character by a swipe over it.
        bool figureHeld = false;
        bool turning = false;
        float turnStartX = 0.0f;
        int turnStartRotation = 0;

        std::vector<MuiListItem> items;
        std::vector<Cell> cells;
        MuiRect listRect;
        MuiRect figureRect;
        MuiRect slotRects[3];
        // The list's cell size: drop badges are as big on the hands and the
        // figure.
        float listCell = 0.0f;
        MuiRect infoRect;
        MuiRect crumbRect;

        // Description of the selected item, rebuilt when it may change
        // (examining runs item scripts).
        InventoryItemInfo info;
        std::string infoKey;

        void buildHeader(MuiContext& ui, const InventoryView& view, const MuiRect& rect);
        void buildList(MuiContext& ui, const MuiRect& rect);
        void buildBottom(MuiContext& ui, const MuiRect& rect);
        void buildCharacter(MuiContext& ui, const InventoryView& view, const MuiRect& rect);
        void buildCharacterTall(MuiContext& ui, const InventoryView& view, const MuiRect& rect);
        void buildFigure(MuiContext& ui, const InventoryView& view, const MuiRect& figure);
        void buildStats(MuiContext& ui, const InventoryView& view, float x, float y, float width, float bottom, float lineHeight, float iconSize, float textSize, float iconGap);
        void buildSlot(MuiContext& ui, const InventoryView& view, InventorySlot slot, Object* item, const MuiRect& rect, bool active);
        void buildInfo(MuiContext& ui, const InventoryView& view, const MuiRect& rect);
        void buildSummary(MuiContext& ui, const MuiRect& content, float* y, bool draw);
        void buildSummaryColumns(MuiContext& ui, const InventorySummary& summary, const MuiRect& content, float* y, bool draw);
        void buildItemInfo(MuiContext& ui, const MuiRect& content, float* y, bool draw);
        void handleDrop(MuiContext& ui, const InventoryView& view);
        void drawDragged(MuiContext& ui, const InventoryView& view);

        Object* draggedItem(MuiContext& ui, const InventoryView& view, InventorySlot* slot);
        void openActionMenu(MuiContext& ui, Object* item, InventorySlot slot);
        void move(Object* item, InventorySlot slot, InventoryDropTarget target, Object* targetItem);
    };

    InventoryScreen gInventoryScreen;
    bool gInventoryScreenPushed = false;

    Object* slotItem(const InventoryView& view, InventorySlot slot)
    {
        switch (slot) {
        case InventorySlot::LeftHand:
            return view.leftHand;
        case InventorySlot::RightHand:
            return view.rightHand;
        case InventorySlot::Armor:
            return view.armor;
        default:
            return nullptr;
        }
    }

    // Back: closes the open container, then the inventory.
    void InventoryScreen::back()
    {
        InventoryView view;
        if (inventoryGetView(&view) && view.container != nullptr) {
            InventoryAction action;
            action.type = InventoryActionType::CloseContainer;
            inventoryQueueAction(action);
        } else {
            inventoryRequestClose();
        }
    }

    void InventoryScreen::build(MuiContext& ui)
    {
        InventoryView view;
        if (!inventoryGetView(&view)) {
            return;
        }

        now = ui.now;

        // Look from the action menu shows the item here.
        if (view.examinedVersion != examinedVersion) {
            examinedVersion = view.examinedVersion;
            selected = view.examinedItem;
        }

        // Use from the action menu acts on the character, unload on the
        // weapon.
        if (view.lastMenuVersion != lastMenuVersion) {
            lastMenuVersion = view.lastMenuVersion;
            if (view.lastMenuAction == GAME_MOUSE_ACTION_MENU_ITEM_USE && view.container == nullptr) {
                startFlash(FlashTarget::Figure, InventorySlot::None, nullptr);
            } else if (view.lastMenuAction == GAME_MOUSE_ACTION_MENU_ITEM_UNLOAD) {
                startFlash(FlashTarget::Item, InventorySlot::None, view.lastMenuItem);
            }
        }

        items = muiInventoryItems(view.listOwner, view.container != nullptr ? MuiItemFilterState() : filter);

        // Selected item is gone (dropped, used up).
        if (selected != nullptr && selected != view.leftHand && selected != view.rightHand && selected != view.armor) {
            bool listed = false;
            for (const MuiListItem& entry : items) {
                listed = listed || entry.item == selected;
            }
            if (!listed) {
                selected = nullptr;
            }
        }

        // Opaque over the map, or a window over it.
        MuiScreenFrame frame = muiScreenFrame(ui, kBackground);
        const MuiRect& content = frame.content;

        // Back: closes the open container, then the inventory. The other game
        // screens: the inventory closes and the game opens them.
        MuiGameScreenTab tab = muiGameScreenTabs(ui, frame.tabs, "inventory", MuiGameScreenTab::Inventory, view.critter == gDude);
        if (tab == MuiGameScreenTab::Back || frame.tappedOutside) {
            back();
        } else if (tab != MuiGameScreenTab::None && muiSwitchGameScreen(ui, tab)) {
            inventoryRequestClose();
        }

        float gap = ui.dp(8.0f);
        tall = muiIsTallContent(ui);
        if (tall) {
            // Items on the left; the character, armor and hands over the
            // summary on the right.
            float columnGap = ui.dp(12.0f);
            MuiRect left = { content.x, content.y, std::floor(content.w * 0.465f), content.h };
            MuiRect right = { left.right() + columnGap, content.y, content.right() - left.right() - columnGap, content.h };

            float rowHeight = ui.dp(40.0f);
            float bottomHeight = ui.dp(44.0f);
            buildHeader(ui, view, { left.x, left.y, left.w, rowHeight });
            buildList(ui, { left.x, left.y + rowHeight + gap, left.w, left.h - rowHeight - bottomHeight - gap * 2.0f });
            buildBottom(ui, { left.x, left.bottom() - bottomHeight, left.w, bottomHeight });

            // As in the window's mockup: 252 of 652 dp.
            float characterHeight = std::floor(right.h * 0.387f);
            buildCharacterTall(ui, view, { right.x, right.y, right.w, characterHeight });
            buildInfo(ui, view, { right.x, right.y + characterHeight + gap, right.w, right.h - characterHeight - gap });
        } else {
            float leftWidth = content.w * 0.4f;
            float centerWidth = content.w * 0.27f;
            MuiRect left = { content.x, content.y, leftWidth, content.h };
            MuiRect center = { left.right() + gap, content.y, centerWidth, content.h };
            MuiRect right = { center.right() + gap, content.y, content.right() - center.right() - gap, content.h };

            float rowHeight = ui.dp(30.0f);
            float bottomHeight = ui.dp(34.0f);
            buildHeader(ui, view, { left.x, left.y, left.w, rowHeight });
            buildList(ui, { left.x, left.y + rowHeight + ui.dp(6.0f), left.w, left.h - rowHeight - bottomHeight - ui.dp(12.0f) });
            buildBottom(ui, { left.x, left.bottom() - bottomHeight, left.w, bottomHeight });
            buildCharacter(ui, view, center);
            buildInfo(ui, view, right);
        }

        handleDrop(ui, view);
        drawDragged(ui, view);
    }

    // Filters, or the open container with a way back.
    void InventoryScreen::buildHeader(MuiContext& ui, const InventoryView& view, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        crumbRect = MuiRect();

        if (view.container == nullptr) {
            muiItemFilterRow(ui, "inventory.filter", rect, &filter);
            return;
        }

        // Dragging an item of the container over it takes the item out.
        int payload;
        bool target = ui.dragging(&payload) && payload < kPayloadSlotBase;
        crumbRect = rect;

        bool pressed;
        if (ui.touchable("inventory.container", rect, &pressed)) {
            InventoryAction action;
            action.type = InventoryActionType::CloseContainer;
            inventoryQueueAction(action);
        }

        muiFillRoundRect(rect, ui.dp(5.0f), pressed || target ? theme.buttonPressed : theme.button);
        muiStrokeRoundRect(rect, ui.dp(5.0f), ui.dp(target ? 2.0f : 1.0f), target || pressed ? theme.accent : theme.buttonBorder);
        drawFlash(ui, rect, flash.target == FlashTarget::Header);

        float iconSize = rect.h * 0.5f;
        muiDrawIcon(MuiIcon::Back, rect.x + ui.dp(8.0f) + iconSize / 2.0f, rect.centerY(), iconSize, theme.accent);

        // Container name, while dragging - where the item goes.
        Object* owner = target ? view.critter : view.container;
        MuiRect label = { rect.x + iconSize + ui.dp(16.0f), rect.y, rect.w - iconSize - ui.dp(20.0f), rect.h };
        muiPushClip(label);
        muiDrawTextAligned(muiDecodeGameText(objectGetName(owner)), label, ui.dp(tall ? 16.0f : 14.0f), theme.accent, MuiAlign::Start, MuiAlign::Center);
        muiPopClip();
    }

    void InventoryScreen::buildList(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        listRect = rect;
        cells.clear();

        // Items from slots go back here.
        int dragPayload;
        bool dragging = ui.dragging(&dragPayload);
        bool target = dragging && dragPayload >= kPayloadSlotBase;
        Object* dragged = nullptr;
        if (dragging && dragPayload < kPayloadSlotBase && dragPayload < static_cast<int>(items.size())) {
            dragged = items[dragPayload].item;
        }

        muiFillRoundRect(rect, ui.dp(6.0f), kListBackground);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(target ? 2.0f : 1.0f), target ? theme.accent : theme.panelBorder);

        float gap = ui.dp(tall ? 6.0f : 4.0f);
        MuiRect area = rect.inset(gap);
        int columns = muiItemColumns(ui, area.w, gap, tall);
        float cell = (area.w - gap * (columns - 1)) / columns;
        listCell = cell;
        int rows = (static_cast<int>(items.size()) + columns - 1) / columns;
        float contentHeight = rows * (cell + gap);

        float offset = ui.scroll("inventory.list", area, contentHeight);
        bool scrolling = ui.isScrolling("inventory.list");
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
            cells.push_back({ touchRect, item });
            ui.mark("inventory.pid." + std::to_string(item->pid), touchRect);

            std::string id = "inventory.list." + std::to_string(index);
            bool pressed;
            bool longPressed;
            bool tapped = ui.touchable(id, touchRect, &pressed, &longPressed);
            bool isDragged = ui.dragSource(id, index, !scrollable);

            if (isDragged) {
                // Placeholder, the item follows the finger.
                muiStrokeRoundRect(cellRect, ui.dp(5.0f), ui.dp(1.0f), theme.buttonBorder);
                continue;
            }

            muiDrawItemCell(ui, cellRect, item, items[index].quantity, pressed || item == selected);
            drawFlash(ui, cellRect, flash.target == FlashTarget::Item && flash.item == item);

            // What dropping the dragged item here does: load ammo, put into
            // the container.
            if (dragged != nullptr && inventoryCanDropOnto(dragged, item)) {
                muiStrokeRoundRect(cellRect, ui.dp(5.0f), ui.dp(2.0f), theme.accent);
                MuiIcon icon = itemGetType(item) == ITEM_TYPE_CONTAINER ? MuiIcon::Inventory : MuiIcon::Reload;
                float iconSize = cell * 0.26f;
                drawDropBadge(ui, cellRect.x + iconSize * 0.9f, cellRect.y + iconSize * 0.9f, iconSize, icon);
            }

            if (longPressed && !scrolling) {
                openActionMenu(ui, item, InventorySlot::None);
            } else if (tapped && !scrolling) {
                selected = selected == item ? nullptr : item;
            }
        }
        muiPopClip();
    }

    // Weight of the shown items, "Drop all" of them.
    void InventoryScreen::buildBottom(MuiContext& ui, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();

        // Every filter, "All" too: the bottom always tells what the shown
        // items weigh (the total with what is worn and held is on the right).
        float iconSize = ui.dp(tall ? 20.0f : 15.0f);
        muiDrawIcon(MuiIcon::Weight, rect.x + iconSize / 2.0f + ui.dp(2.0f), rect.centerY(), iconSize, theme.textDim);
        MuiRect weightRect = { rect.x + iconSize + ui.dp(8.0f), rect.y, rect.w * 0.4f, rect.h };
        muiDrawTextAligned(number(muiItemsWeight(items)), weightRect, ui.dp(tall ? 17.0f : 14.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);

        bool confirming = dropAllTime != 0 && ui.now - dropAllTime < kConfirmMs;
        MuiRect button = { rect.right() - rect.w * 0.55f, rect.y, rect.w * 0.55f, rect.h };
        bool pressed;
        if (ui.touchable("inventory.drop_all", button, &pressed) && !items.empty()) {
            if (confirming) {
                InventoryAction action;
                action.type = InventoryActionType::DropAll;
                for (const MuiListItem& entry : items) {
                    action.items.push_back(entry.item);
                }
                inventoryQueueAction(action);
                dropAllTime = 0;
                confirming = false;
            } else {
                dropAllTime = ui.now;
                confirming = true;
            }
        }

        MuiColor fill = confirming ? kWarning : (pressed ? theme.buttonPressed : theme.button);
        MuiColor color = confirming ? muiRgb(0x2A1A00) : theme.accent;
        muiFillRoundRect(button, ui.dp(6.0f), fill);
        muiStrokeRoundRect(button, ui.dp(6.0f), ui.dp(1.0f), confirming ? kWarning : theme.buttonBorder);
        const char* label = confirming ? muiText(kTextSure, "SURE?") : muiText(kTextDropAll, "DROP ALL");
        MuiRect labelRect = button.inset(ui.dp(4.0f), 0.0f);
        muiPushClip(labelRect);
        muiDrawTextAligned(muiDecodeGameText(label), labelRect, ui.dp(tall ? 15.0f : 13.0f), color, MuiAlign::Center, MuiAlign::Center);
        muiPopClip();
    }

    // Figure (turning, as in the game), armor slot, hit points, armor class,
    // weight, hand slots.
    void InventoryScreen::buildCharacter(MuiContext& ui, const InventoryView& view, const MuiRect& rect)
    {
        float gap = ui.dp(8.0f);

        MuiRect figure = { rect.x, rect.y, rect.w * 0.58f, rect.h * 0.52f };
        buildFigure(ui, view, figure);

        float armorSize = rect.right() - figure.right() - gap;
        MuiRect armor = { figure.right() + gap, rect.y, armorSize, armorSize };
        buildSlot(ui, view, InventorySlot::Armor, view.armor, armor, false);
        buildStats(ui, view, armor.x, armor.bottom() + ui.dp(10.0f), armor.w, figure.bottom(), ui.dp(22.0f), ui.dp(14.0f), ui.dp(13.0f), ui.dp(6.0f));

        // Hands, the active one is marked.
        float handsY = figure.bottom() + gap;
        float handSize = std::min((rect.w - gap) / 2.0f, rect.bottom() - handsY);
        MuiRect leftHand = { rect.x, handsY, handSize, handSize };
        MuiRect rightHand = { rect.right() - handSize, handsY, handSize, handSize };
        buildSlot(ui, view, InventorySlot::LeftHand, view.leftHand, leftHand, view.activeHand == HAND_LEFT);
        buildSlot(ui, view, InventorySlot::RightHand, view.rightHand, rightHand, view.activeHand == HAND_RIGHT);
    }

    // Tall layout: the figure on the left; armor and the stats over the two
    // hands, each above its weapon's column of the summary.
    void InventoryScreen::buildCharacterTall(MuiContext& ui, const InventoryView& view, const MuiRect& rect)
    {
        float gap = ui.dp(8.0f);

        MuiRect figure = { rect.x, rect.y, std::floor(rect.w * 0.372f), rect.h };
        buildFigure(ui, view, figure);

        MuiRect grid = { figure.right() + gap, rect.y, rect.right() - figure.right() - gap, rect.h };
        float cellWidth = (grid.w - gap) / 2.0f;
        float cellHeight = (grid.h - gap) / 2.0f;
        MuiRect armor = { grid.x, grid.y, cellWidth, cellHeight };
        buildSlot(ui, view, InventorySlot::Armor, view.armor, armor, false);

        float lineHeight = std::min(ui.dp(32.0f), (armor.h - ui.dp(4.0f)) / 3.0f);
        float statsX = armor.right() + gap + ui.dp(6.0f);
        buildStats(ui, view, statsX, armor.centerY() - lineHeight * 1.5f, grid.right() - statsX, armor.bottom(), lineHeight, ui.dp(20.0f), ui.dp(17.0f), ui.dp(10.0f));

        MuiRect leftHand = { grid.x, armor.bottom() + gap, cellWidth, cellHeight };
        MuiRect rightHand = { grid.right() - cellWidth, leftHand.y, cellWidth, cellHeight };
        buildSlot(ui, view, InventorySlot::LeftHand, view.leftHand, leftHand, view.activeHand == HAND_LEFT);
        buildSlot(ui, view, InventorySlot::RightHand, view.rightHand, rightHand, view.activeHand == HAND_RIGHT);
    }

    // The character turned by a swipe over it, a target for using items on
    // it.
    void InventoryScreen::buildFigure(MuiContext& ui, const InventoryView& view, const MuiRect& figure)
    {
        const MuiTheme& theme = muiTheme();
        figureRect = figure;
        muiFillRoundRect(figure, ui.dp(6.0f), kListBackground);
        muiStrokeRoundRect(figure, ui.dp(6.0f), ui.dp(1.0f), theme.panelBorder);

        // Horizontal swipe turns the character the same way, tap shows the
        // character summary again.
        bool figurePressed;
        bool figureTapped = ui.touchable("inventory.figure", figure, &figurePressed);
        if (figurePressed && !turning && !figureHeld) {
            figureHeld = true;
            turnStartX = ui.pointerX();
            turnStartRotation = view.bodyRotation;
        }

        if (figureHeld) {
            float dx = ui.pointerX() - turnStartX;
            if (std::fabs(dx) > ui.dp(muiTheme().touchSlop)) {
                turning = true;
            }
            if (turning) {
                // Swipe to the right turns the front of the figure to the
                // right (rotations go clockwise).
                int steps = static_cast<int>(std::lround(dx / ui.dp(kTurnStepDp)));
                inventorySetBodyRotation(turnStartRotation - steps);
            }
            if (!ui.pointerDown()) {
                figureHeld = false;
            }
        } else if (!ui.pointerDown()) {
            turning = false;
        }

        if (figureTapped && !turning) {
            selected = nullptr;
        }

        // Turn hint under the feet.
        float turnSize = figure.w * 0.55f;
        float turnHeight = turnSize * 0.3f;
        // The ellipse's lowest point is 0.1 of the icon below its center.
        drawTurnHint(figure.centerX(), figure.bottom() - ui.dp(10.0f) - turnSize * 0.1f, turnSize, ui.dp(1.8f), turning ? theme.accent : theme.textDim);

        // One scale for all directions (fits the tallest), so the figure
        // keeps its size while turning; wide frames (spear) may cross the
        // frame.
        int maxHeight = 0;
        for (int rotation = 0; rotation < 6; rotation++) {
            int frameWidth;
            int frameHeight;
            if (muiArtTexture(view.bodyFrmId, 0, &frameWidth, &frameHeight, rotation) != nullptr) {
                maxHeight = std::max(maxHeight, frameHeight);
            }
        }

        int width;
        int height;
        SDL_Texture* texture = muiArtTexture(view.bodyFrmId, 0, &width, &height, view.bodyRotation);
        if (texture != nullptr && maxHeight > 0) {
            MuiRect area = figure.inset(ui.dp(6.0f));
            area.h -= turnHeight;
            float scale = area.h / maxHeight;
            if (scale >= 1.0f) {
                scale = std::floor(scale);
            }
            float w = width * scale;
            float h = height * scale;
            muiDrawTexture(texture, { area.centerX() - w / 2.0f, area.centerY() - h / 2.0f, w, h });
        }

        InventorySlot draggedSlot;
        Object* dragged = draggedItem(ui, view, &draggedSlot);
        if (view.container == nullptr && view.critter == gDude && inventoryCanUseOnSelf(dragged)) {
            muiStrokeRoundRect(figure.inset(-ui.dp(2.0f)), ui.dp(8.0f), ui.dp(2.0f), theme.accent);
            // As big as on list items, whatever the figure's size.
            float iconSize = listCell > 0.0f ? listCell * 0.26f : ui.dp(18.0f);
            drawDropBadge(ui, figure.x + iconSize * 0.9f, figure.y + iconSize * 0.9f, iconSize, MuiIcon::Use);
        }

        drawFlash(ui, figure, flash.target == FlashTarget::Figure);
    }

    // Hit points, armor class, weight: lines from [y] while they fit above
    // [bottom].
    void InventoryScreen::buildStats(MuiContext& ui, const InventoryView& view, float x, float y, float width, float bottom, float lineHeight, float iconSize, float textSize, float iconGap)
    {
        const MuiTheme& theme = muiTheme();
        InventorySummary summary;
        if (!inventoryGetSummary(&summary)) {
            return;
        }

        struct Stat {
            MuiIcon icon;
            std::u32string value;
            MuiColor color;
        };
        const Stat stats[3] = {
            { MuiIcon::HitPoints, trimmed(summary.defense[0].value), theme.text },
            { MuiIcon::FilterArmor, trimmed(summary.defense[1].value), theme.text },
            { MuiIcon::Weight, number(view.weight) + U"/" + number(view.carryWeight), summary.encumbered ? kEncumbered : theme.text },
        };
        for (const Stat& stat : stats) {
            if (y + lineHeight > bottom) {
                break;
            }
            muiDrawIcon(stat.icon, x + iconSize / 2.0f, y + lineHeight / 2.0f, iconSize, theme.accent);
            muiDrawTextAligned(stat.value, { x + iconSize + iconGap, y, width, lineHeight }, textSize, stat.color, MuiAlign::Start, MuiAlign::Center);
            y += lineHeight;
        }
    }

    void InventoryScreen::buildSlot(MuiContext& ui, const InventoryView& view, InventorySlot slot, Object* item, const MuiRect& rect, bool active)
    {
        const MuiTheme& theme = muiTheme();
        int slotIndex = static_cast<int>(slot) - 1;
        slotRects[slotIndex] = rect;

        std::string id = slotIndex == 2 ? "inventory.slot.armor" : (slotIndex == 0 ? "inventory.slot.left" : "inventory.slot.right");

        // Can the dragged item go here?
        InventorySlot draggedSlot;
        Object* dragged = draggedItem(ui, view, &draggedSlot);
        bool target = false;
        if (dragged != nullptr && draggedSlot != slot) {
            target = slot == InventorySlot::Armor
                ? itemGetType(dragged) == ITEM_TYPE_ARMOR
                : true;
        }

        bool pressed = false;
        bool longPressed = false;
        bool tapped = false;
        bool isDragged = false;
        if (item != nullptr) {
            tapped = ui.touchable(id, rect, &pressed, &longPressed);
            isDragged = ui.dragSource(id, kPayloadSlotBase + static_cast<int>(slot), true);
        } else {
            ui.mark(id, rect);
        }

        if (isDragged || item == nullptr) {
            muiFillRoundRect(rect, ui.dp(5.0f), theme.button);
            muiStrokeRoundRect(rect, ui.dp(5.0f), ui.dp(1.0f), theme.buttonBorder);
            MuiIcon icon = slot == InventorySlot::Armor ? MuiIcon::FilterArmor : MuiIcon::Use;
            muiDrawIcon(icon, rect.centerX(), rect.centerY(), rect.w * 0.34f, theme.buttonBorder);
        } else {
            muiDrawItemCell(ui, rect, item, 1, pressed || item == selected);

            // Loaded rounds over the art at the bottom right (it keeps its
            // size), in a row: the loaded ammo's art, the count (empty red)
            // with the magazine's size small.
            int capacity = itemGetType(item) == ITEM_TYPE_WEAPON ? ammoGetCapacity(item) : 0;
            if (capacity > 0) {
                int quantity = ammoGetQuantity(item);
                std::u32string count = number(quantity);
                std::u32string size = U"/" + number(capacity);
                float countSize = ui.dp(20.0f);
                float sizeSize = ui.dp(12.0f);
                float countWidth = muiTextWidth(count, countSize);
                float sizeWidth = muiTextWidth(size, sizeSize);
                float lineHeight = muiLineHeight(countSize);
                float right = rect.right() - ui.dp(6.0f);
                float top = rect.bottom() - ui.dp(4.0f) - lineHeight;
                MuiRect sizeRect = { right - sizeWidth, top + ui.dp(3.0f), sizeWidth, lineHeight };
                MuiRect countRect = { sizeRect.x - countWidth, top, countWidth, lineHeight };

                ProtoId ammoPid = weaponGetAmmoTypeProtoId(item);
                Proto* proto;
                if (ammoPid.valid() && protoGetProto(ammoPid, &proto) != -1) {
                    int width;
                    int height;
                    SDL_Texture* texture = muiArtTexture(FrmId(proto->item.inventoryFid), 0, &width, &height);
                    if (texture != nullptr) {
                        float artWidth = ui.dp(25.0f);
                        float artHeight = ui.dp(16.0f);
                        MuiRect box = { countRect.x - ui.dp(4.0f) - artWidth, countRect.centerY() - artHeight / 2.0f, artWidth, artHeight };
                        MuiRect art = muiFitRect(box, static_cast<float>(width), static_cast<float>(height));
                        // Next to the count, not in the middle of the box.
                        art.x = box.right() - art.w;
                        muiDrawTexture(texture, art);
                    }
                }

                MuiColor countColor = quantity == 0 ? kAmmoEmpty : theme.text;
                float shadow = ui.dp(1.0f);
                for (float offset : { shadow, 0.0f }) {
                    bool isShadow = offset > 0.0f;
                    muiDrawTextAligned(count, { countRect.x + offset, countRect.y + offset, countRect.w, countRect.h }, countSize, isShadow ? muiRgb(0x000000, 200) : countColor, MuiAlign::Start, MuiAlign::Center);
                    muiDrawTextAligned(size, { sizeRect.x + offset, sizeRect.y + offset, sizeRect.w, sizeRect.h }, sizeSize, isShadow ? muiRgb(0x000000, 200) : theme.textDim, MuiAlign::Start, MuiAlign::Center);
                }
            }
        }

        // Ammo onto the weapon in the hand loads it, an item into the bag in
        // it goes in: the badge as on list items, over the active dot.
        bool onto = target && item != nullptr && inventoryCanDropOnto(dragged, item);
        if (active) {
            muiStrokeRoundRect(rect, ui.dp(5.0f), ui.dp(2.0f), theme.accent);
            if (!onto) {
                muiFillCircle(rect.x + ui.dp(8.0f), rect.y + ui.dp(8.0f), ui.dp(3.0f), theme.accent);
            }
        }

        drawFlash(ui, rect, (flash.target == FlashTarget::Slot && flash.slot == slot) || (flash.target == FlashTarget::Item && item != nullptr && flash.item == item));

        // Target: loading ammo or putting into a container in the hand is
        // shown like on list items.
        if (target) {
            muiStrokeRoundRect(rect.inset(-ui.dp(2.0f)), ui.dp(6.0f), ui.dp(2.0f), onto ? theme.accent : theme.buttonPrimary);
            if (onto) {
                float iconSize = (listCell > 0.0f ? listCell : rect.w) * 0.26f;
                MuiIcon icon = itemGetType(item) == ITEM_TYPE_CONTAINER ? MuiIcon::Inventory : MuiIcon::Reload;
                drawDropBadge(ui, rect.x + iconSize * 0.9f, rect.y + iconSize * 0.9f, iconSize, icon);
            }
        }

        if (longPressed) {
            openActionMenu(ui, item, slot);
        } else if (tapped) {
            selected = selected == item ? nullptr : item;
        }
    }

    // Character summary as the game shows it, or the chosen item. While
    // dragging it's where items are dropped on the ground.
    void InventoryScreen::buildInfo(MuiContext& ui, const InventoryView& view, const MuiRect& rect)
    {
        const MuiTheme& theme = muiTheme();
        infoRect = rect;

        if (ui.dragging(nullptr)) {
            muiFillRoundRect(rect, ui.dp(6.0f), kDropZone);
            muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(2.0f), kWarning);
            float iconSize = ui.dp(34.0f);
            muiDrawIcon(MuiIcon::Drop, rect.centerX(), rect.centerY() - iconSize * 0.4f, iconSize, kWarning);
            MuiRect label = { rect.x, rect.centerY() + iconSize * 0.4f, rect.w, muiLineHeight(ui.dp(14.0f)) };
            muiDrawTextAligned(muiDecodeGameText(muiText(kTextDrop, "Drop")), label, ui.dp(14.0f), kWarning, MuiAlign::Center, MuiAlign::Center);
            return;
        }

        ui.panel(rect);
        ui.mark("inventory.info", rect);

        // Examining runs scripts, only when the item or its state changes.
        if (selected != nullptr) {
            std::string key = std::to_string(reinterpret_cast<uintptr_t>(selected))
                + ":" + std::to_string(itemGetType(selected) == ITEM_TYPE_WEAPON ? ammoGetQuantity(selected) : 0)
                + ":" + std::to_string(reinterpret_cast<uintptr_t>(view.leftHand))
                + ":" + std::to_string(reinterpret_cast<uintptr_t>(view.rightHand))
                + ":" + std::to_string(reinterpret_cast<uintptr_t>(view.armor));
            if (key != infoKey) {
                infoKey = key;
                inventoryGetItemInfo(selected, &info);
            }
        } else {
            infoKey.clear();
        }

        MuiRect content = tall ? rect.inset(ui.dp(18.0f), ui.dp(14.0f)) : rect.inset(ui.dp(12.0f), ui.dp(10.0f));

        // Measure (the tall layout's rows spaced out if it all fits), scroll,
        // draw.
        auto measure = [&]() {
            float height = 0.0f;
            if (selected != nullptr) {
                buildItemInfo(ui, content, &height, false);
            } else {
                buildSummary(ui, content, &height, false);
            }
            return height;
        };
        float height = 0.0f;
        for (float step : kInfoRowSteps) {
            infoRowDp = tall ? step : 0.0f;
            height = measure();
            if (height <= content.h || infoRowDp == 0.0f) {
                break;
            }
        }

        std::string scrollId = selected != nullptr ? "inventory.info.item" : "inventory.info.summary";
        float offset = ui.scroll(scrollId, content, height);

        muiPushClip(content);
        float y = content.y - offset;
        if (selected != nullptr) {
            buildItemInfo(ui, content, &y, true);
        } else {
            buildSummary(ui, content, &y, true);
        }
        muiPopClip();
    }

    // Lines of [content] width from [*y]; measuring only adds heights.
    struct InfoWriter {
        const MuiRect& content;
        float* y;
        bool draw;
        // Rows at least this high (the tall layout's spacing).
        float rowHeight = 0.0f;

        void text(const std::u32string& line, float size, MuiColor color)
        {
            if (draw) {
                muiDrawText(line, content.x, *y, size, color);
            }
            *y += muiLineHeight(size);
        }

        void row(const std::u32string& label, const std::u32string& value, float size, MuiColor labelColor, MuiColor valueColor)
        {
            float height = std::max(muiLineHeight(size), rowHeight);
            if (draw) {
                MuiRect rect = { content.x, *y, content.w, height };
                muiDrawTextAligned(label, rect, size, labelColor, MuiAlign::Start, MuiAlign::Center);
                muiDrawTextAligned(value, rect, size, valueColor, MuiAlign::End, MuiAlign::Center);
            }
            *y += height;
        }

        void separator(float thickness, float gap, MuiColor color)
        {
            *y += gap;
            if (draw) {
                muiFillRect({ content.x, *y, content.w, thickness }, color);
            }
            *y += thickness + gap;
        }
    };

    void InventoryScreen::buildSummary(MuiContext& ui, const MuiRect& content, float* y, bool draw)
    {
        const MuiTheme& theme = muiTheme();
        InventorySummary summary;
        if (!inventoryGetSummary(&summary)) {
            return;
        }

        float start = *y;
        if (tall) {
            buildSummaryColumns(ui, summary, content, y, draw);
            if (!draw) {
                *y -= start;
            }
            return;
        }

        float size = ui.dp(13.0f);
        InfoWriter writer = { content, y, draw };
        writer.text(muiDecodeGameText(summary.name.c_str()), ui.dp(15.0f), theme.accent);
        writer.separator(ui.dp(1.0f), ui.dp(4.0f), theme.panelBorder);

        // Primary stats in two columns.
        float columnGap = ui.dp(14.0f);
        float columnWidth = (content.w - columnGap) / 2.0f;
        for (int index = 0; index < 7; index += 2) {
            for (int column = 0; column < 2 && index + column < 7; column++) {
                const InventoryInfoRow& stat = summary.stats[index + column];
                MuiRect rect = { content.x + column * (columnWidth + columnGap), *y, columnWidth, muiLineHeight(size) };
                if (draw) {
                    muiDrawTextAligned(trimmed(stat.label), rect, size, theme.textDim, MuiAlign::Start, MuiAlign::Center);
                    muiDrawTextAligned(trimmed(stat.value), rect, size, theme.text, MuiAlign::End, MuiAlign::Center);
                }
            }
            *y += muiLineHeight(size);
        }

        writer.separator(ui.dp(1.0f), ui.dp(4.0f), theme.panelBorder);
        for (const InventoryInfoRow& row : summary.defense) {
            writer.row(trimmed(row.label), trimmed(row.value), size, theme.textDim, theme.text);
        }

        writer.separator(ui.dp(1.0f), ui.dp(4.0f), theme.panelBorder);
        for (const std::vector<InventoryTextPart>* hand : { summary.handParts[0], summary.handParts[1] }) {
            for (int line = 0; line < 3; line++) {
                *y += muiDrawTextParts(hand[line], content.x, *y, content.w, muiLineHeight(size), size, false, draw);
            }
            *y += ui.dp(4.0f);
        }

        writer.separator(ui.dp(1.0f), ui.dp(2.0f), theme.panelBorder);
        writer.text(muiDecodeGameText(summary.weight.c_str()), size, summary.encumbered ? kEncumbered : theme.text);

        if (!draw) {
            *y -= start;
        }
    }

    // Tall layout (the summary's columns): SPECIAL with the full names, as on
    // the character screen (the game's two letters are for its narrow pane),
    // next to hit points, armor class and resistances; each hand's weapon
    // under its slot.
    void InventoryScreen::buildSummaryColumns(MuiContext& ui, const InventorySummary& summary, const MuiRect& content, float* y, bool draw)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(15.0f);
        float lineHeight = std::max(muiLineHeight(size), ui.dp(infoRowDp));
        float separatorGap = ui.dp(infoRowDp != 0.0f ? infoRowDp - 14.0f : 4.0f);
        InfoWriter writer = { content, y, draw };
        writer.text(muiDecodeGameText(summary.name.c_str()), ui.dp(20.0f), theme.accent);
        writer.separator(ui.dp(1.0f), separatorGap, theme.panelBorder);

        float columnGap = ui.dp(14.0f);
        float divider = ui.dp(1.0f);
        float columnWidth = (content.w - columnGap * 2.0f - divider) / 2.0f;
        float dividerX = content.x + columnWidth + columnGap;
        float rightX = dividerX + divider + columnGap;

        std::u32string labels[2][7];
        std::u32string values[2][7];
        float widest = 0.0f;
        for (int row = 0; row < 7; row++) {
            labels[0][row] = muiDecodeGameText(statGetName(static_cast<Stat>(STAT_STRENGTH + row)));
            values[0][row] = trimmed(summary.stats[row].value);
            labels[1][row] = trimmed(summary.defense[row].label);
            values[1][row] = trimmed(summary.defense[row].value);
            for (int column = 0; column < 2; column++) {
                widest = std::max(widest, muiTextWidth(labels[column][row], size) + muiTextWidth(values[column][row], size));
            }
        }

        // Smaller where the longest name and its value don't fit a column
        // (7" tablets).
        float room = columnWidth - ui.dp(8.0f);
        float rowSize = widest > room ? size * room / widest : size;

        float top = *y;
        for (int row = 0; row < 7; row++) {
            if (draw) {
                for (int column = 0; column < 2; column++) {
                    MuiRect rect = { column == 0 ? content.x : rightX, *y, columnWidth, lineHeight };
                    muiDrawTextAligned(labels[column][row], rect, rowSize, theme.textDim, MuiAlign::Start, MuiAlign::Center);
                    muiDrawTextAligned(values[column][row], rect, rowSize, theme.text, MuiAlign::End, MuiAlign::Center);
                }
            }
            *y += lineHeight;
        }
        if (draw) {
            muiFillRect({ dividerX, top, divider, *y - top }, theme.panelBorder);
        }
        writer.separator(ui.dp(1.0f), separatorGap, theme.panelBorder);

        top = *y;
        float bottom = top;
        for (int hand = 0; hand < 2; hand++) {
            float x = hand == 0 ? content.x : rightX;
            float lineY = top;
            for (int line = 0; line < 3; line++) {
                lineY += muiDrawTextParts(summary.handParts[hand][line], x, lineY, columnWidth, lineHeight, size, true, draw);
            }
            bottom = std::max(bottom, lineY);
        }
        if (draw) {
            muiFillRect({ dividerX, top, divider, bottom - top }, theme.panelBorder);
        }
        *y = bottom;

        writer.separator(ui.dp(1.0f), separatorGap, theme.panelBorder);
        writer.text(muiDecodeGameText(summary.weight.c_str()), size, summary.encumbered ? kEncumbered : theme.text);
    }

    void InventoryScreen::buildItemInfo(MuiContext& ui, const MuiRect& content, float* y, bool draw)
    {
        const MuiTheme& theme = muiTheme();
        float size = ui.dp(tall ? 15.0f : 13.0f);
        float titleSize = ui.dp(tall ? 20.0f : 15.0f);
        float start = *y;
        InfoWriter writer = { content, y, draw, ui.dp(infoRowDp) };

        for (const std::u32string& line : muiWrapText(muiDecodeGameText(info.name.c_str()), content.w, titleSize)) {
            writer.text(line, titleSize, theme.accent);
        }
        writer.separator(ui.dp(1.0f), ui.dp(4.0f), theme.panelBorder);

        for (const std::string& paragraph : info.text) {
            for (const std::u32string& line : muiWrapText(muiDecodeGameText(paragraph.c_str()), content.w, size)) {
                writer.text(line, size, theme.text);
            }
            *y += ui.dp(4.0f);
        }

        if (!info.rows.empty() || info.actionPoints >= 0) {
            writer.separator(ui.dp(1.0f), ui.dp(2.0f), theme.panelBorder);
        }

        for (const InventoryInfoRow& row : info.rows) {
            writer.row(trimmed(row.label), trimmed(row.value), size, theme.textDim, theme.text);
        }

        // Action points of the primary attack: lightning, as on the HUD.
        if (info.actionPoints >= 0) {
            float lineHeight = std::max(muiLineHeight(size), writer.rowHeight);
            if (draw) {
                float iconSize = size * 0.9f;
                muiDrawIcon(MuiIcon::ActionPoints, content.x + iconSize / 2.0f, *y + lineHeight / 2.0f, iconSize, theme.textDim);
                muiDrawTextAligned(number(info.actionPoints), { content.x, *y, content.w, lineHeight }, size, theme.text, MuiAlign::End, MuiAlign::Center);
            }
            *y += lineHeight;
        }

        if (!draw) {
            *y -= start;
        }
    }

    // Glow fading out around [rect].
    void InventoryScreen::drawFlash(MuiContext& ui, const MuiRect& rect, bool matches)
    {
        if (!matches || flash.target == FlashTarget::None) {
            return;
        }

        float progress = static_cast<float>(now - flash.time) / kFlashMs;
        if (progress >= 1.0f) {
            flash.target = FlashTarget::None;
            return;
        }

        const MuiTheme& theme = muiTheme();
        float strength = 1.0f - progress * progress;
        MuiColor inner = theme.accent.withAlpha(static_cast<Uint8>(220 * strength));
        MuiColor outer = theme.accent.withAlpha(static_cast<Uint8>(90 * strength));
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(2.5f), inner);
        muiStrokeRoundRect(rect.inset(-ui.dp(3.0f)), ui.dp(9.0f), ui.dp(3.0f), outer);
    }

    Object* InventoryScreen::draggedItem(MuiContext& ui, const InventoryView& view, InventorySlot* slot)
    {
        int payload;
        if (!ui.dragging(&payload)) {
            return nullptr;
        }

        if (payload >= kPayloadSlotBase) {
            *slot = static_cast<InventorySlot>(payload - kPayloadSlotBase);
            return slotItem(view, *slot);
        }

        *slot = InventorySlot::None;
        return payload < static_cast<int>(items.size()) ? items[payload].item : nullptr;
    }

    void InventoryScreen::openActionMenu(MuiContext& ui, Object* item, InventorySlot slot)
    {
        InventoryAction action;
        action.type = InventoryActionType::OpenActionMenu;
        action.item = item;
        action.slot = slot;
        muiToScreen(ui.pointerX(), ui.pointerY(), &(action.x), &(action.y));
        inventoryQueueAction(action);
    }

    void InventoryScreen::move(Object* item, InventorySlot slot, InventoryDropTarget target, Object* targetItem)
    {
        switch (target) {
        case InventoryDropTarget::LeftHand:
            startFlash(FlashTarget::Slot, InventorySlot::LeftHand, nullptr);
            break;
        case InventoryDropTarget::RightHand:
            startFlash(FlashTarget::Slot, InventorySlot::RightHand, nullptr);
            break;
        case InventoryDropTarget::Armor:
            startFlash(FlashTarget::Slot, InventorySlot::Armor, nullptr);
            break;
        case InventoryDropTarget::Backpack:
            // Loaded weapon, container, or the item back in the list.
            startFlash(FlashTarget::Item, InventorySlot::None, targetItem != nullptr ? targetItem : item);
            break;
        case InventoryDropTarget::Body:
            startFlash(FlashTarget::Header, InventorySlot::None, nullptr);
            break;
        }

        InventoryAction action;
        action.type = InventoryActionType::Move;
        action.item = item;
        action.slot = slot;
        action.target = target;
        action.targetItem = targetItem;
        inventoryQueueAction(action);
    }

    void InventoryScreen::handleDrop(MuiContext& ui, const InventoryView& view)
    {
        InventorySlot slot;
        Object* item = draggedItem(ui, view, &slot);
        if (item == nullptr) {
            return;
        }

        if (ui.dropped(infoRect, nullptr)) {
            InventoryAction action;
            action.type = InventoryActionType::Drop;
            action.item = item;
            action.slot = slot;
            inventoryQueueAction(action);
            return;
        }

        const InventoryDropTarget slotTargets[3] = {
            InventoryDropTarget::LeftHand,
            InventoryDropTarget::RightHand,
            InventoryDropTarget::Armor,
        };
        for (int index = 0; index < 3; index++) {
            InventorySlot targetSlot = static_cast<InventorySlot>(index + 1);
            if (targetSlot != slot && ui.dropped(slotRects[index], nullptr)) {
                move(item, slot, slotTargets[index], nullptr);
                return;
            }
        }

        if (view.container == nullptr && view.critter == gDude && inventoryCanUseOnSelf(item) && ui.dropped(figureRect, nullptr)) {
            InventoryAction action;
            action.type = InventoryActionType::UseOnSelf;
            action.item = item;
            action.slot = slot;
            inventoryQueueAction(action);
            return;
        }

        // Out of the open container.
        if (view.container != nullptr && slot == InventorySlot::None && ui.dropped(crumbRect, nullptr)) {
            move(item, slot, InventoryDropTarget::Body, nullptr);
            return;
        }

        // On another item (load ammo, put into a container), or back from a
        // slot.
        for (const Cell& cell : cells) {
            if (cell.item != item && ui.dropped(cell.rect, nullptr)) {
                if (slot != InventorySlot::None || inventoryCanDropOnto(item, cell.item)) {
                    move(item, slot, InventoryDropTarget::Backpack, cell.item);
                }
                return;
            }
        }

        if (slot != InventorySlot::None && ui.dropped(listRect, nullptr)) {
            move(item, slot, InventoryDropTarget::Backpack, nullptr);
        }
    }

    // Dragged item under the finger with its name and weight.
    void InventoryScreen::drawDragged(MuiContext& ui, const InventoryView& view)
    {
        InventorySlot slot;
        Object* item = draggedItem(ui, view, &slot);
        if (item == nullptr) {
            return;
        }

        // A slot's item is one, a listed one its stack.
        int quantity = 1;
        if (slot == InventorySlot::None) {
            for (const MuiListItem& entry : items) {
                if (entry.item == item) {
                    quantity = entry.quantity;
                    break;
                }
            }
        }
        muiDrawDraggedItem(ui, item, quantity, -1);
    }

} // namespace

void muiInventoryInit()
{
    if (!muiIsEnabled() || gInventoryScreenPushed) {
        return;
    }

    muiPush(&gInventoryScreen);
    gInventoryScreenPushed = true;
}

} // namespace fallout
