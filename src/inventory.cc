#include "inventory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "action_log.h"
#include "actions.h"
#include "animation.h"
#include "art.h"
#include "color.h"
#include "combat.h"
#include "combat_ai.h"
#include "content_config.h"
#include "critter.h"
#include "dbox.h"
#include "debug.h"
#include "dev_autotest.h"
#include "dialog.h"
#include "display_monitor.h"
#include "draw.h"
#include "game.h"
#include "game_dialog.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "input.h"
#include "interface.h"
#include "interpreter_extra.h"
#include "item.h"
#include "kb.h"
#include "light.h"
#include "map.h"
#include "message.h"
#include "mouse.h"
#include "mui.h"
#include "object.h"
#include "party_member.h"
#include "perk.h"
#include "platform_compat.h"
#include "proto.h"
#include "proto_instance.h"
#include "random.h"
#include "reaction.h"
#include "scripts.h"
#include "settings.h"
#include "sfall_script_hooks.h"
#include "skill.h"
#include "stat.h"
#include "svga.h"
#include "text_font.h"
#include "tile.h"
#include "touch.h"
#include "touch_controls.h"
#include "window_manager.h"

namespace fallout {

#define INVENTORY_WINDOW_X 80
#define INVENTORY_WINDOW_Y 0

#define INVENTORY_TRADE_WINDOW_X 80
#define INVENTORY_TRADE_WINDOW_Y 0
#define INVENTORY_TRADE_WINDOW_WIDTH 480
#define INVENTORY_TRADE_WINDOW_HEIGHT 180

constexpr int kTradeSlotCount = 3;
constexpr int kPartySlotWidth = 106;
constexpr int kPartySlotHeight = 69;
constexpr int kPartySlotImageHeight = 139;

#define INVENTORY_LARGE_SLOT_WIDTH 90
#define INVENTORY_LARGE_SLOT_HEIGHT 61

#define INVENTORY_LEFT_HAND_SLOT_X 154
#define INVENTORY_LEFT_HAND_SLOT_Y 286
#define INVENTORY_LEFT_HAND_SLOT_MAX_X (INVENTORY_LEFT_HAND_SLOT_X + INVENTORY_LARGE_SLOT_WIDTH)
#define INVENTORY_LEFT_HAND_SLOT_MAX_Y (INVENTORY_LEFT_HAND_SLOT_Y + INVENTORY_LARGE_SLOT_HEIGHT)

#define INVENTORY_RIGHT_HAND_SLOT_X 245
#define INVENTORY_RIGHT_HAND_SLOT_Y 286
#define INVENTORY_RIGHT_HAND_SLOT_MAX_X (INVENTORY_RIGHT_HAND_SLOT_X + INVENTORY_LARGE_SLOT_WIDTH)
#define INVENTORY_RIGHT_HAND_SLOT_MAX_Y (INVENTORY_RIGHT_HAND_SLOT_Y + INVENTORY_LARGE_SLOT_HEIGHT)

#define INVENTORY_ARMOR_SLOT_X 154
#define INVENTORY_ARMOR_SLOT_Y 183
#define INVENTORY_ARMOR_SLOT_MAX_X (INVENTORY_ARMOR_SLOT_X + INVENTORY_LARGE_SLOT_WIDTH)
#define INVENTORY_ARMOR_SLOT_MAX_Y (INVENTORY_ARMOR_SLOT_Y + INVENTORY_LARGE_SLOT_HEIGHT)

#define INVENTORY_TRADE_SCROLLER_Y 30
#define INVENTORY_TRADE_INNER_SCROLLER_Y 20

#define INVENTORY_TRADE_LEFT_SCROLLER_X 29
#define INVENTORY_TRADE_LEFT_SCROLLER_Y INVENTORY_TRADE_SCROLLER_Y

#define INVENTORY_TRADE_RIGHT_SCROLLER_X 388
#define INVENTORY_TRADE_RIGHT_SCROLLER_Y INVENTORY_TRADE_SCROLLER_Y

#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_X 165
#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y INVENTORY_TRADE_INNER_SCROLLER_Y

#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X 250
#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y INVENTORY_TRADE_INNER_SCROLLER_Y

#define INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_X 0
#define INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_Y 10
#define INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_MAX_X (INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_X 165
#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_Y 10
#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_MAX_X (INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_X 250
#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_Y 10
#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_MAX_X (INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_X 395
#define INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_Y 10
#define INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_MAX_X (INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_LOOT_LEFT_SCROLLER_X 180
#define INVENTORY_LOOT_LEFT_SCROLLER_Y 37
#define INVENTORY_LOOT_LEFT_SCROLLER_MAX_X (INVENTORY_LOOT_LEFT_SCROLLER_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_LOOT_RIGHT_SCROLLER_X 297
#define INVENTORY_LOOT_RIGHT_SCROLLER_Y 37
#define INVENTORY_LOOT_RIGHT_SCROLLER_MAX_X (INVENTORY_LOOT_RIGHT_SCROLLER_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_SCROLLER_X 46
#define INVENTORY_SCROLLER_Y 35
#define INVENTORY_SCROLLER_MAX_X (INVENTORY_SCROLLER_X + INVENTORY_SLOT_WIDTH)

#define INVENTORY_BODY_VIEW_WIDTH 60
#define INVENTORY_BODY_VIEW_HEIGHT 100

#define INVENTORY_PC_BODY_VIEW_X 176
#define INVENTORY_PC_BODY_VIEW_Y 37
#define INVENTORY_PC_BODY_VIEW_MAX_X (INVENTORY_PC_BODY_VIEW_X + INVENTORY_BODY_VIEW_WIDTH)
#define INVENTORY_PC_BODY_VIEW_MAX_Y (INVENTORY_PC_BODY_VIEW_Y + INVENTORY_BODY_VIEW_HEIGHT)

#define INVENTORY_BARTER_LEFT_BODY_VIEW_X 18
#define INVENTORY_BARTER_LEFT_BODY_VIEW_Y 25

#define INVENTORY_LOOT_RIGHT_BODY_VIEW_X 422
#define INVENTORY_LOOT_RIGHT_BODY_VIEW_Y 35

#define INVENTORY_LOOT_LEFT_BODY_VIEW_X 44
#define INVENTORY_LOOT_LEFT_BODY_VIEW_Y 35

#define INVENTORY_SUMMARY_X 298
#define INVENTORY_SUMMARY_Y 45
#define INVENTORY_SUMMARY_WIDTH 154
#define INVENTORY_SUMMARY_HEIGHT 190
#define INVENTORY_SUMMARY_MAX_X 440

#define INVENTORY_WINDOW_WIDTH 499
#define INVENTORY_USE_ON_WINDOW_WIDTH 292
#define INVENTORY_LOOT_WINDOW_WIDTH 537
#define INVENTORY_LOOT_WINDOW_WIDTH_EXPANDED 673

#define INVENTORY_TRADE_WINDOW_WIDTH 480
#define INVENTORY_TIMER_WINDOW_WIDTH 259

#define INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH 640
#define INVENTORY_TRADE_BACKGROUND_WINDOW_HEIGHT 480
#define INVENTORY_TRADE_WINDOW_OFFSET ((INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH - INVENTORY_TRADE_WINDOW_WIDTH) / 2)

#define INVENTORY_SLOT_PADDING 4

#define INVENTORY_SCROLLER_X_PAD (INVENTORY_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_SCROLLER_Y_PAD (INVENTORY_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_LOOT_LEFT_SCROLLER_X_PAD (INVENTORY_LOOT_LEFT_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_LOOT_LEFT_SCROLLER_Y_PAD (INVENTORY_LOOT_LEFT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_LOOT_RIGHT_SCROLLER_X_PAD (INVENTORY_LOOT_RIGHT_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_LOOT_RIGHT_SCROLLER_Y_PAD (INVENTORY_LOOT_RIGHT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_LOOT_CRITTER_TOGGLE_Y (INVENTORY_LOOT_LEFT_BODY_VIEW_Y + INVENTORY_BODY_VIEW_HEIGHT + 24)
#define INVENTORY_TRADE_LEFT_SCROLLER_X_PAD (INVENTORY_TRADE_LEFT_SCROLLER_Y + INVENTORY_SLOT_PADDING)
#define INVENTORY_TRADE_LEFT_SCROLLER_Y_PAD (INVENTORY_TRADE_LEFT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_TRADE_RIGHT_SCROLLER_X_PAD (INVENTORY_TRADE_RIGHT_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_TRADE_RIGHT_SCROLLER_Y_PAD (INVENTORY_TRADE_RIGHT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD (INVENTORY_TRADE_INNER_LEFT_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y_PAD (INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD (INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X + INVENTORY_SLOT_PADDING)
#define INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y_PAD (INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y + INVENTORY_SLOT_PADDING)

#define INVENTORY_SLOT_WIDTH_PAD (INVENTORY_SLOT_WIDTH - INVENTORY_SLOT_PADDING * 2)
#define INVENTORY_SLOT_HEIGHT_PAD (INVENTORY_SLOT_HEIGHT - INVENTORY_SLOT_PADDING * 2)

#define INVENTORY_NORMAL_WINDOW_PC_ROTATION_DELAY (1000U / ROTATION_COUNT)
#define INVENTORY_FRM_COUNT 12
#define INVENTORY_ROWS 6

#define INVENTORY_HAND_RIGHT_KEY 2600
#define INVENTORY_HAND_LEFT_KEY 2601
#define INVENTORY_ARMOR_KEY 2602
#define PARTY_WEAPON_SLOT_KEY 2603
#define PARTY_ARMOR_SLOT_KEY 2604

typedef enum InventoryArrowFrm {
    INVENTORY_ARROW_FRM_LEFT_ARROW_UP,
    INVENTORY_ARROW_FRM_LEFT_ARROW_DOWN,
    INVENTORY_ARROW_FRM_RIGHT_ARROW_UP,
    INVENTORY_ARROW_FRM_RIGHT_ARROW_DOWN,
    INVENTORY_ARROW_FRM_COUNT,
} InventoryArrowFrm;

typedef enum InventoryWindowCursor {
    INVENTORY_WINDOW_CURSOR_HAND,
    INVENTORY_WINDOW_CURSOR_ARROW,
    INVENTORY_WINDOW_CURSOR_PICK,
    INVENTORY_WINDOW_CURSOR_MENU,
    INVENTORY_WINDOW_CURSOR_BLANK,
    INVENTORY_WINDOW_CURSOR_COUNT,
} InventoryWindowCursor;

typedef enum InventoryWindowType {
    // Normal inventory window with quick character sheet.
    INVENTORY_WINDOW_TYPE_NORMAL,

    // Narrow inventory window with just an item scroller that's shown when
    // a "Use item on" is selected from context menu.
    INVENTORY_WINDOW_TYPE_USE_ITEM_ON,

    // Looting/strealing interface.
    INVENTORY_WINDOW_TYPE_LOOT,

    // Barter interface.
    INVENTORY_WINDOW_TYPE_TRADE,

    // Supplementary "Move items" window. Used to set quantity of items when
    // moving items between inventories.
    INVENTORY_WINDOW_TYPE_MOVE_ITEMS,

    // Supplementary "Set timer" window. Internally it's implemented as "Move
    // items" window but with timer overlay and slightly different adjustment
    // mechanics.
    INVENTORY_WINDOW_TYPE_SET_TIMER,

    INVENTORY_WINDOW_TYPE_COUNT,
} InventoryWindowType;

typedef struct InventoryWindowConfiguration {
    InterfaceFrameId frmId; // artId
    int width;
    int height;
    int x;
    int y;
} InventoryWindowDescription;

typedef struct InventoryCursorData {
    Art* frm;
    unsigned char* frmData;
    int width;
    int height;
    int offsetX;
    int offsetY;
    CacheEntry* frmHandle;
} InventoryCursorData;

typedef enum InventoryMoveResult {
    INVENTORY_MOVE_RESULT_FAILED,
    INVENTORY_MOVE_RESULT_CAUGHT_STEALING,
    INVENTORY_MOVE_RESULT_SUCCESS,
} InventoryMoveResult;

typedef enum InventoryAmmoMoveResult {
    INVENTORY_AMMO_MOVE_RESULT_FAILED = -1,
    INVENTORY_AMMO_MOVE_RESULT_SUCCESS = 0,
    INVENTORY_AMMO_MOVE_RESULT_BLOCKED = 1,
} InventoryAmmoMoveResult;

// offsets for normal inventory window (not steal, loot, &c.)
typedef struct InventoryNormalLayout {
    int columns;
    int visibleSlots;
    int windowWidth;
    int windowHeight;
    int scrollerX;
    int scrollerY;
    int scrollerWidth;
    int scrollerHeight;
    int leftHandSlotX;
    int rightHandSlotX;
    int armorSlotX;
    int bodyViewX;
    int summaryX;
    int scrollButtonX;
    int doneButtonX;
} InventoryNormalLayout;

typedef struct InventoryLootLayout {
    int columns;
    int visibleSlots;
    int windowWidth;
    int windowHeight;
    int leftScrollerX;
    int leftScrollerY;
    int rightScrollerX;
    int rightScrollerY;
    int scrollerWidth;
    int scrollerHeight;
    int leftBodyViewX;
    int rightBodyViewX;
    int leftScrollButtonX;
    int rightScrollButtonX;
    int takeAllButtonX;
    int doneButtonX;
    int prevCritterButtonX;
    int nextCritterButtonX;
    int partySlotX;
    int partyArmorSlotY;
} InventoryLootLayout;

typedef struct InventoryScroller {
    Rect hitRect;
    int* offsetPtr;
    int step;
    int visibleSlots;
    int totalItems;
    int keyUp;
    int keyDown;
    int keyPageUp;
    int keyPageDown;
    int keyHome;
    int keyEnd;
    void (*redraw)(const InventoryScroller& scroller, void* context);
    void* redrawContext;
    bool refreshWindow;
} InventoryScroller;

typedef struct InventoryScrollerDisplayContext {
    int inventoryWindowType;
    Inventory* inventory;
} InventoryScrollerDisplayContext;

typedef struct InventoryScrollerBarterContext {
    int win;
    Object* playerTable;
    Object* bartererTable;
} InventoryScrollerBarterContext;

static int inventoryMessageListInit();
static int inventoryMessageListFree();
static bool _setup_inventory(int inventoryWindowType);
static void _exit_inventory(bool shouldEnableIso);
static void _display_inventory(int stackOffset, int draggedSlotIndex, int inventoryWindowType);
static void _display_target_inventory(int stackOffset, int dragSlotIndex, Inventory* inventory, int inventoryWindowType);
static void _display_inventory_info(Object* item, int quantity, unsigned char* dest, int pitch, bool isDragged);
static void inventoryLootRenderPaneWeight(unsigned char* windowBuffer, int pitch, bool targetPane, Object* object);
static void inventoryScrollerHandleInput(const InventoryScroller& scroller, int keyCode, MouseButtonEvent mouseEvent);
static void inventoryUpdateScrollButtons(int upButton, int downButton, int offset, int totalItems, int visibleSlots, int step = 1);
static void _display_body(const FrmId& frmId, int inventoryWindowType);
static int inventoryCommonInit();
static void inventoryCommonFree();
static void inventorySetCursor(int cursor);
static void inventoryItemSlotOnMouseEnter(int btn, int keyCode);
static void inventoryItemSlotOnMouseExit(int btn, int keyCode);
static void _inven_update_lighting(Object* activeItem);
static void _inven_pickup(int keyCode, int indexOffset);
static bool inventoryGetDropTargetAtMouse(int indexOffset, InventoryDropTarget* target, Object** targetItem);
static void inventoryPlaceItem(Object* item, Object** itemSlot, int itemIndex, int count, int buttonCode, bool immediate, const InventoryDropTarget* target, Object* targetItem);
static void inventoryAfterItemMove(int indexOffset);
static void inventoryPerformQueuedActions();
static int inventoryGetCritterWeight();
static bool inventoryUsesContextMenu(bool tapIsHandAction = false);
static void _switch_hand(Object* sourceItem, Object** targetSlot, Object** sourceSlot, int itemIndex);
static void _adjust_fid();
static void inventoryRenderSummary();
static void inventoryBuildSummary(InventorySummary* summary);
static int inventoryFormatWeaponDamage(MessageList* messages, Object* critter, Object* item, HitMode hitMode, char* buffer, size_t size, std::string* damageOnly);
static void inventoryWeaponParts(MessageList* messages, Object* critter, Object* item, HitMode hitMode, std::string* damageLine, std::vector<InventoryTextPart>* damage, std::string* ammoLine, std::vector<InventoryTextPart>* ammo);
static int _inven_from_button(int keyCode, Object** outItem, Object*** outItemSlot, Object** outOwner);
static void inventoryRenderItemDescription(const char* string);
static void inventoryDrawCenteredText(unsigned char* buffer, int pitch, int width, int x, int y, const char* text, ColorWithFlags color);
static void inventoryExamineItem(Object* critter, Object* item);
static void inventoryExamineItemText(Object* critter, Object* item, void (*fn)(const char* string));
static bool inventoryCanLoadAmmo(Object* weapon, Object* ammo, bool* replaceAmmo);
static void inventorySetLeftPaneCritter(Object* critter, Object* target, int inventoryWindowType);
static void inventoryWindowOpenContextMenu(int eventCode, int inventoryWindowType);
static void inventoryRunItemActionMenu(int keyCode, int inventoryWindowType, int x, int y);
static void inventoryDropToGround(int keyCode, Object* item, Object** itemSlot, Object* owner, int quantity, bool askQuantity);
static InventoryMoveResult _move_inventory(Object* item, int slotIndex, Object* targetObj, bool isPlanting, int* stealXpOverridePtr);
static InventoryMoveResult inventoryLootTransfer(Object* item, int quantity, Object* targetObj, bool isPlanting, bool askQuantity, int* stealXpOverridePtr);
static int inventoryListQuantity(Inventory* inventory, Object* item);
static int inventoryListKeyCode(Object* item, bool target);
static std::pair<int, int> barterComputeTablesValue(Object* dude, Object* npc, bool offerButton = false);
static void barterGetPriceFactors(Object* dude, Object* npc, double* barterModMult, double* skillRatio);
static std::pair<int, int> barterComputeTablesWeight(Object* dude, Object* npc);
static int barterAttemptTransaction(Object* dude, Object* offerTable, Object* npc, Object* barterTable);
static int barterGetMovedQuantity(Object* item, int maxQuantity, bool fromPlayer, bool fromInventory, bool immediate);
static void barterMoveToTable(Object* item, int quantity, int slotIndex, int indexOffset, Object* npc, Object* sourceTable, bool fromDude);
static void barterMoveFromTable(Object* item, int quantity, int slotIndex, Object* npc, Object* sourceTable, bool fromDude);
static void barterDisplayTables(int win, Object* leftTable, Object* rightTable, int draggedSlotIndex);
static void _container_enter(int keyCode, int inventoryWindowType);
static void _container_exit(int keyCode, int inventoryWindowType);
static int _drop_into_container(Object* container, Object* item, int sourceIndex, Object** itemSlot, int quantity);
static void inventoryUnloadWeaponToOwner(Object* owner, Object* weapon);
static InventoryAmmoMoveResult _drop_ammo_into_weapon(Object* weapon, Object* ammo, Object** ammoItemSlot, int quantity, int keyCode);
static void _draw_amount(int value, int inventoryWindowType);
static int inventoryQuantitySelect(int inventoryWindowType, Object* item, int maximum, int defaultValue = 1, const std::function<int(int)>& price = nullptr);
static int inventoryQuantityWindowInit(int inventoryWindowType, Object* item);
static int inventoryQuantityWindowFree(int inventoryWindowType);
static void inventoryDisplayMessage(int num);
static bool _ctrl_pressed();
static void _drag_item_loop(Object* item, bool immediate);
static void inventoryNormalLayoutUpdate();
static bool inventoryBackgroundLoad(FrmImage& image, InterfaceFrameId col1FrmId, const char* col2Name, int columns);
static int inventoryChooseColumns(FrmImage& image, int expandedWidth, InterfaceFrameId col1FrmId, const char* col2Name);
static void inventoryCreateSlotButtons(int baseKeyCode, int scrollerX, int scrollerY, int columns);
static bool inventoryGetCurrentWindowType(InventoryWindowType* inventoryWindowTypePtr);
static int inventoryGetWindowWidth(int inventoryWindowType);
static int inventoryGetWindowHeight(int inventoryWindowType);
static void inventoryNormalClampStackOffset();
static void inventoryLootLayoutUpdate();
static int inventoryLootGetSlotX(bool targetInventory, int slotIndex);
static int inventoryLootGetSlotY(int slotIndex);
static bool inventoryLootMouseHitTestScroller(bool targetInventory);
static void renderPartySlots();
static void createPartySlotButtons();
static void handlePartySlotPickup(InvenSlot slot);
static bool tryEquipPartyItem(Object* item, bool fromLeftPane);
static bool tryUnequipPartyItem(InvenSlot slot);
static bool tryMovePartyItemToLeftPane(InvenSlot slot, Object* item);
static FrmId buildPartyDisplayFrmId();
static FrmId getTargetDisplayFrmId();
static void lootSetTakenOut();
static void setLootTarget(Object* target, Object* hiddenBox);
static void refreshAfterTargetChange();
static int inventoryWrapIndex(int index, int count, int direction);
static int inventoryComputeAlignedMaxOffset(int length, int visibleSlots, int scrollStep);
static int inventoryGetCenteredWindowY(int windowHeight);
static void displayLootPanePartyName(unsigned char* windowBuffer, int windowPitch, const Rect& rect, int index);

// 0x46E6D0 stats_array0
static const Stat gSummaryStats[7] = {
    STAT_CURRENT_HIT_POINTS,
    STAT_ARMOR_CLASS,
    STAT_DAMAGE_THRESHOLD,
    STAT_DAMAGE_THRESHOLD_LASER,
    STAT_DAMAGE_THRESHOLD_FIRE,
    STAT_DAMAGE_THRESHOLD_PLASMA,
    STAT_DAMAGE_THRESHOLD_EXPLOSION,
};

// 0x46E6EC stats_array1
static const Stat gSummaryStats2[7] = {
    STAT_MAXIMUM_HIT_POINTS,
    STAT_INVALID,
    STAT_DAMAGE_RESISTANCE,
    STAT_DAMAGE_RESISTANCE_LASER,
    STAT_DAMAGE_RESISTANCE_FIRE,
    STAT_DAMAGE_RESISTANCE_PLASMA,
    STAT_DAMAGE_RESISTANCE_EXPLOSION,
};

// 0x46E708
static constexpr InterfaceFrmId kInventoryArrowFrmIds[INVENTORY_ARROW_FRM_COUNT] = {
    InterfaceFrameId::LeftArrowUp,
    InterfaceFrameId::LeftArrowDown,
    InterfaceFrameId::RightArrowUp,
    InterfaceFrameId::RightArrowDown,
};

// The number of items to show in scroller.
//
// 0x519054 inven_cur_disp
static int gInventorySlotsCount = 6;

static InventoryNormalLayout inventoryLayout;
static InventoryLootLayout inventoryLootLayout;

static FrmImage inventoryFrmImage;
static FrmImage inventoryLootFrmImage;
static FrmImage partySlotFrmImage;

// 0x519058 inven_dude
static Object* _inven_dude = nullptr;

// Probably fid of armor to display in inventory dialog.
//
// 0x51905C inven_pid
static ProtoId inventoryProtoId = ProtoId::Empty();

// 0x519060 inven_is_initialized
static bool _inven_is_initialized = false;

// 0x519064 inven_display_msg_line
static int _inven_display_msg_line = 1;

// 0x519068 iscr_data
static const InventoryWindowDescription gInventoryWindowDescriptions[INVENTORY_WINDOW_TYPE_COUNT] = {
    { InterfaceFrameId::InventoryBox, INVENTORY_WINDOW_WIDTH, 377, 80, 0 },
    { InterfaceFrameId::UseItemOnWindow, INVENTORY_USE_ON_WINDOW_WIDTH, 376, 80, 0 },
    { InterfaceFrameId::LootContainerWindow, INVENTORY_LOOT_WINDOW_WIDTH, 376, 80, 0 },
    { InterfaceFrameId::BarterWindow, INVENTORY_TRADE_WINDOW_WIDTH, 180, 80, 290 },
    { InterfaceFrameId::MoveMultipleItemsInterface, INVENTORY_TIMER_WINDOW_WIDTH, 162, 140, 80 },
    { InterfaceFrameId::MoveMultipleItemsInterface, INVENTORY_TIMER_WINDOW_WIDTH, 162, 140, 80 },
};

// 0x5190E0 dropped_explosive
static bool _dropped_explosive = false;

// 0x5190E4 inven_scroll_up_bid
static int gInventoryScrollUpButton = -1;

// 0x5190E8 inven_scroll_dn_bid
static int gInventoryScrollDownButton = -1;

// 0x5190EC loot_scroll_up_bid
static int gSecondaryInventoryScrollUpButton = -1;

// 0x5190F0 loot_scroll_dn_bid
static int gSecondaryInventoryScrollDownButton = -1;

static int playerTableScrollUpButton = -1;
static int playerTableScrollDownButton = -1;
static int bartererTableScrollUpButton = -1;
static int bartererTableScrollDownButton = -1;

// 0x5190F4 ticker
static unsigned int gInventoryWindowDudeRotationTimestamp = 0;

// 0x5190F8 curr_rot
static Rotation gInventoryWindowDudeRotation = ROTATION_NE;

// 0x5190FC num
static constexpr InterfaceFrmId kInventoryWindowCursorFrmIds[INVENTORY_WINDOW_CURSOR_COUNT] = {
    InterfaceFrameId::PointingHand,
    InterfaceFrameId::ActionArrow,
    InterfaceFrameId::ActionPick,
    InterfaceFrameId::ActionMenu,
    InterfaceFrameId::MouseCursorBlank,
};

// 0x519110 last_target
static Object* _last_target = nullptr;

// 0x519114 act_use
static const GameMouseActionMenuItem _act_use[4] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_USE,
    GAME_MOUSE_ACTION_MENU_ITEM_DROP,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// 0x519124 act_no_use
static const GameMouseActionMenuItem _act_no_use[3] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_DROP,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// 0x519130 act_just_use
static const GameMouseActionMenuItem _act_just_use[3] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_USE,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// 0x51913C act_nothing
static const GameMouseActionMenuItem _act_nothing[2] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// 0x519144 act_weap
static const GameMouseActionMenuItem _act_weap[4] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_UNLOAD,
    GAME_MOUSE_ACTION_MENU_ITEM_DROP,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// 0x519154 act_weap2
static const GameMouseActionMenuItem _act_weap2[3] = {
    GAME_MOUSE_ACTION_MENU_ITEM_LOOK,
    GAME_MOUSE_ACTION_MENU_ITEM_UNLOAD,
    GAME_MOUSE_ACTION_MENU_ITEM_CANCEL,
};

// Scroll offsets to target inventory for every container nesting level (stack).
// 0x59E7EC target_stack_offset
static int _target_stack_offset[10];

// inventory.msg
//
// 0x59E814 inventry_message_file
static MessageList gInventoryMessageList;

// inventry.msg for what's shown outside the inventory (the party member
// panel), loaded on first use and kept.
static MessageList gInventoryLabelMessageList;
static bool gInventoryLabelMessageListLoaded = false;

// Current target critter or container for every nesting level (stack).
// 0x59E81C target_stack
static Object* _target_stack[10];

// Scroll offsets to main inventory for every container nesting level (stack).
// 0x59E844 stack_offset
static int _stack_offset[10];

// Current critter or container for every nesting level (stack).
// 0x59E86C stack
static Object* _stack[10];

// 0x59E894 mt_wid
static int _mt_wid;

static constexpr int kDefaultInventoryApCost = 4;
static constexpr int kDefaultQuickPocketsApCostReduction = 2;

// Current inventory AP cost
static int inventoryApCost = kDefaultInventoryApCost;
static int inventoryDefaultApCost = kDefaultInventoryApCost;
static int quickPocketsApCostReduction = kDefaultQuickPocketsApCostReduction;

// Current barter price modifier, based on gGameDialogBarterModifier, with the reaction-based modifier added (in percent).
// 0x59E898 barter_mod
static int gBarterFinalModifier;

// 0x59E89C btable_offset
static int gBartererTableOffset;

// 0x59E8A0 ptable_offset
static int gPlayerTableOffset;

// An inventory containing a subset of Player's items offered to the NPC during barter.
// 0x59E8A4 ptable_pud
static Inventory* gPlayerTableInventory;

// 0x59E8A8 imdata
static InventoryCursorData gInventoryCursorData[INVENTORY_WINDOW_CURSOR_COUNT];

// An object (PID -1) with an inventory containing a subset of Player's items offered to the NPC during barter.
// 0x59E934 ptable
static Object* gPlayerTableObj;

// 0x59E938 display_msg
static InventoryPrintItemDescriptionHandler* gInventoryPrintItemDescriptionHandler;

// 0x59E93C im_value
static int _im_value; // "keyCode" corresponding to an inventory item "button", or -1 if nothing

// 0x59E940 immode
static int gInventoryCursor;

// An object (PID -1) with an inventory containing a subset of NPC's items asked for by the player during barter.
// 0x59E944 btable
static Object* gBartererTableObj;

// Current nesting level for viewing target's bag/backpack contents.
// 0x59E948 target_curr_stack
static int _target_curr_stack;

// 0x59E94C btable_pud
static Inventory* gBartererTableInventory;

// 0x59E950 inven_ui_was_disabled
static bool _inven_ui_was_disabled;

// 0x59E954 i_worn
static Object* gInventoryArmor;

// 0x59E958 i_lhand
static Object* gInventoryLeftHandItem;

// Rotating character's fid.
//
// 0x59E95C i_fid
static FrmId gInventoryWindowDudeFrmId;

// 0x59E960 pud
static Inventory* _pud;

// 0x59E964 i_wid
static int gInventoryWindow = -1;

// CE: Under the mobile UI the inventory, loot and barter screens are its own:
// the game's windows of them aren't made and their loops are the screens'
// (the game's functions do the rest, in the same order).
static bool inventoryScreensWindowless()
{
    return muiIsEnabled();
}

// CE: False for the mobile UI inventory, loot and barter screens, which don't
// make the game's window (known when building the touch-only build: the
// drawing functions are left out of it).
static bool inventoryHasWindow()
{
    return !inventoryScreensWindowless() && windowGetWindow(gInventoryWindow) != nullptr;
}

// item2
// 0x59E968 i_rhand
static Object* gInventoryRightHandItem;

// Current nesting level for viewing bag/backpack contents.
// 0x59E96C curr_stack
static int _curr_stack;

// CE: State of the inventory for the mobile UI inventory screen.
static bool gInventoryViewActive = false;
static std::vector<InventoryAction> gInventoryActions;
static bool gInventoryCloseRequested = false;
static Object* gInventoryExaminedItem = nullptr;
static unsigned int gInventoryExaminedVersion = 0;
static FrmId gInventoryCritterFrmId;

// CE: State of the looting for the mobile UI loot screen.
static bool gLootViewActive = false;
static std::vector<Object*>* gLootPartyTargets = nullptr;
static int* gLootPartyTargetIndex = nullptr;
static bool gLootPartySwitchesTarget = false;
static int gLootTargetCount = 0;
static int gLootTargetIndex = 0;
static std::vector<LootAction> gLootActions;
static bool gLootCloseRequested = false;
static int gInventoryLastMenuAction = -1;
static Object* gInventoryLastMenuItem = nullptr;
static unsigned int gInventoryLastMenuVersion = 0;

// 0x59E970 i_wid_max_y
static int gInventoryWindowMaxY;

// 0x59E974 i_wid_max_x
static int gInventoryWindowMaxX;

// 0x59E978 target_pud
static Inventory* _target_pud;

// 0x59E97C barter_back_win
static int gInventoryBarterBackgroundWindow;

static CritterEquipped* partyTargetEquipped = nullptr; // stripped equipment of party member being "looted"
static Object* partyBaseTarget = nullptr;
// The loot target's hidden items while inventoryOpenLooting is active.
static Object* lootHiddenBox = nullptr;

static FrmImage _inventoryFrmImages[INVENTORY_FRM_COUNT];
static FrmImage _moveFrmImages[8];

static bool inventoryBackgroundLoad(FrmImage& image, InterfaceFrameId col1FrmId, const char* col2Name, int columns)
{
    image.unlock();

    if (columns == 1) {
        return image.lock(col1FrmId);
    }

    if (columns == 2) {
        return image.lock(OBJ_TYPE_INTERFACE, col2Name);
    }

    return false;
}

static int buttonCreateSlot(int win, int x, int y, int width, int height, int keyCode, ButtonCallback* onEnter = nullptr, ButtonCallback* onExit = nullptr)
{
    int btn = buttonCreate(win, x, y, width, height, keyCode, -1, keyCode);
    if (btn != -1) {
        buttonSetMouseCallbacks(btn, onEnter, onExit, nullptr, nullptr);
    }
    return btn;
}

static int buttonCreateAction(int win, int x, int y, int width, int height, int keyCode)
{
    return buttonCreate(win, x, y, width, height, -1, -1, keyCode);
}

static int buttonCreateActionWithFrm(int win, int x, int y, int mouseDownEventCode, int mouseUpEventCode, const InterfaceFrmId& normalId, const InterfaceFrmId& pressedId)
{
    int btn = buttonCreateWithFrm(win,
        x, y, -1, -1,
        mouseDownEventCode, mouseUpEventCode,
        normalId, pressedId,
        {}, BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }
    return btn;
}

static ConstBuffer2D inventoryGetBackgroundBuffer(int inventoryWindowType, InterfaceFrameId fallbackFrmId, FrmImage& fallbackImage, int& sourceXOffset)
{
    sourceXOffset = 0;

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        return inventoryFrmImage.getBuffer();
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        return inventoryLootFrmImage.getBuffer();
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        sourceXOffset = INVENTORY_TRADE_WINDOW_OFFSET;
        return { windowGetBuffer(gInventoryBarterBackgroundWindow),
            INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH,
            windowGetHeight(gInventoryBarterBackgroundWindow) };
    }

    if (fallbackImage.lock(fallbackFrmId)) {
        return fallbackImage.getBuffer();
    }

    return {};
}

static void inventoryCreateSlotButtons(int baseKeyCode, int scrollerX, int scrollerY, int columns)
{
    for (int row = 0; row < INVENTORY_ROWS; row++) {
        for (int column = 0; column < columns; column++) {
            int keyCode = baseKeyCode + row * columns + column;
            buttonCreateSlot(gInventoryWindow,
                scrollerX + column * INVENTORY_SLOT_WIDTH,
                scrollerY + row * INVENTORY_SLOT_HEIGHT,
                INVENTORY_SLOT_WIDTH,
                INVENTORY_SLOT_HEIGHT,
                keyCode,
                inventoryItemSlotOnMouseEnter,
                inventoryItemSlotOnMouseExit);
        }
    }
}

static bool inventoryGetCurrentWindowType(InventoryWindowType* inventoryWindowTypePtr)
{
    if (GameMode::isInGameMode(GameMode::kBarter)) {
        *inventoryWindowTypePtr = INVENTORY_WINDOW_TYPE_TRADE;
        return true;
    }

    if (GameMode::isInGameMode(GameMode::kLoot)) {
        *inventoryWindowTypePtr = INVENTORY_WINDOW_TYPE_LOOT;
        return true;
    }

    if (GameMode::isInGameMode(GameMode::kUseOn)) {
        *inventoryWindowTypePtr = INVENTORY_WINDOW_TYPE_USE_ITEM_ON;
        return true;
    }

    if (GameMode::isInGameMode(GameMode::kInventory)) {
        *inventoryWindowTypePtr = INVENTORY_WINDOW_TYPE_NORMAL;
        return true;
    }

    return false;
}

static int inventoryGetWindowWidth(int inventoryWindowType)
{
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) return inventoryLayout.windowWidth;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) return inventoryLootLayout.windowWidth;
    return gInventoryWindowDescriptions[inventoryWindowType].width;
}

static int inventoryGetWindowHeight(int inventoryWindowType)
{
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) return inventoryLayout.windowHeight;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) return inventoryLootLayout.windowHeight;
    return gInventoryWindowDescriptions[inventoryWindowType].height;
}

static int inventoryGetCenteredWindowY(int windowHeight)
{
    return (screenGetVisibleHeight() - windowHeight) / 2;
}

static int inventoryChooseColumns(FrmImage& image, int expandedWidth, InterfaceFrameId col1FrmId, const char* col2Name)
{
    if (settings.ui.inventory_columns <= 1) {
        return 1;
    }

    if (expandedWidth <= screenGetWidth() && inventoryBackgroundLoad(image, col1FrmId, col2Name, 2)) {
        return 2;
    }

    return 1;
}

static void inventoryNormalApplyLayout(int columns)
{
    int shift = (columns - 1) * INVENTORY_SLOT_WIDTH;
    inventoryLayout.columns = columns;
    inventoryLayout.visibleSlots = INVENTORY_ROWS * columns;
    inventoryLayout.windowWidth = INVENTORY_WINDOW_WIDTH + shift;
    inventoryLayout.windowHeight = gInventoryWindowDescriptions[INVENTORY_WINDOW_TYPE_NORMAL].height;
    inventoryLayout.scrollerX = INVENTORY_SCROLLER_X;
    inventoryLayout.scrollerY = INVENTORY_SCROLLER_Y;
    inventoryLayout.scrollerWidth = columns * INVENTORY_SLOT_WIDTH;
    inventoryLayout.scrollerHeight = INVENTORY_ROWS * INVENTORY_SLOT_HEIGHT;
    inventoryLayout.leftHandSlotX = INVENTORY_LEFT_HAND_SLOT_X + shift;
    inventoryLayout.rightHandSlotX = INVENTORY_RIGHT_HAND_SLOT_X + shift;
    inventoryLayout.armorSlotX = INVENTORY_ARMOR_SLOT_X + shift;
    inventoryLayout.bodyViewX = INVENTORY_PC_BODY_VIEW_X + shift;
    inventoryLayout.summaryX = INVENTORY_SUMMARY_X + shift;
    inventoryLayout.scrollButtonX = 128 + shift;
    inventoryLayout.doneButtonX = 437 + shift;
}

static void inventoryLootApplyLayout(int columns)
{
    int extraColumns = columns - 1;
    int scrollerShift = extraColumns * (367 - INVENTORY_LOOT_RIGHT_SCROLLER_X);
    int leftBodyViewShift = extraColumns * (47 - INVENTORY_LOOT_LEFT_BODY_VIEW_X);
    int rightBodyViewShift = extraColumns * (563 - INVENTORY_LOOT_RIGHT_BODY_VIEW_X);
    int rightPaneShift = extraColumns * (612 - 476);

    inventoryLootLayout.columns = columns;
    inventoryLootLayout.visibleSlots = INVENTORY_ROWS * columns;
    inventoryLootLayout.windowWidth = INVENTORY_LOOT_WINDOW_WIDTH + extraColumns * (INVENTORY_LOOT_WINDOW_WIDTH_EXPANDED - INVENTORY_LOOT_WINDOW_WIDTH);
    inventoryLootLayout.windowHeight = gInventoryWindowDescriptions[INVENTORY_WINDOW_TYPE_LOOT].height;
    inventoryLootLayout.leftScrollerX = INVENTORY_LOOT_LEFT_SCROLLER_X;
    inventoryLootLayout.leftScrollerY = INVENTORY_LOOT_LEFT_SCROLLER_Y;
    inventoryLootLayout.rightScrollerX = INVENTORY_LOOT_RIGHT_SCROLLER_X + scrollerShift;
    inventoryLootLayout.rightScrollerY = INVENTORY_LOOT_RIGHT_SCROLLER_Y;
    inventoryLootLayout.scrollerWidth = columns * INVENTORY_SLOT_WIDTH;
    inventoryLootLayout.scrollerHeight = INVENTORY_ROWS * INVENTORY_SLOT_HEIGHT;
    inventoryLootLayout.leftBodyViewX = INVENTORY_LOOT_LEFT_BODY_VIEW_X + leftBodyViewShift;
    inventoryLootLayout.rightBodyViewX = INVENTORY_LOOT_RIGHT_BODY_VIEW_X + rightBodyViewShift;
    inventoryLootLayout.leftScrollButtonX = 128;
    inventoryLootLayout.rightScrollButtonX = 379 + rightPaneShift;
    inventoryLootLayout.takeAllButtonX = 432 + rightPaneShift;
    inventoryLootLayout.doneButtonX = 476 + rightPaneShift;
    // Center the two 20px-wide critter-nav arrows below the right avatar
    int critterBtnCenterX = inventoryLootLayout.rightBodyViewX + INVENTORY_BODY_VIEW_WIDTH / 2;
    inventoryLootLayout.prevCritterButtonX = critterBtnCenterX - 20;
    inventoryLootLayout.nextCritterButtonX = critterBtnCenterX;
    inventoryLootLayout.partySlotX = inventoryLootLayout.rightBodyViewX - 22;
    inventoryLootLayout.partyArmorSlotY = 181;
}

static void inventoryNormalLayoutUpdate()
{
    int columns = inventoryChooseColumns(inventoryFrmImage, INVENTORY_WINDOW_WIDTH + INVENTORY_SLOT_WIDTH, InterfaceFrameId::InventoryBox, "invbox2.png");
    if (columns == 1) {
        inventoryBackgroundLoad(inventoryFrmImage, InterfaceFrameId::InventoryBox, "invbox2.png", 1);
    }

    inventoryNormalApplyLayout(columns);
}

static void inventoryLootLayoutUpdate()
{
    int columns = inventoryChooseColumns(inventoryLootFrmImage, INVENTORY_LOOT_WINDOW_WIDTH_EXPANDED, InterfaceFrameId::LootContainerWindow, "loot2.png");
    if (columns == 1) {
        inventoryBackgroundLoad(inventoryLootFrmImage, InterfaceFrameId::LootContainerWindow, "loot2.png", 1);
    }

    inventoryLootApplyLayout(columns);
}

static void inventoryNormalClampStackOffset()
{
    int maxOffset = inventoryComputeAlignedMaxOffset(_pud->length, inventoryLayout.visibleSlots, inventoryLayout.columns);
    if (_stack_offset[_curr_stack] > maxOffset) {
        _stack_offset[_curr_stack] = maxOffset;
    }
    if (_stack_offset[_curr_stack] < 0) {
        _stack_offset[_curr_stack] = 0;
    }
}

static void inventoryClampOffset(int* offsetPtr, int inventoryLength, int visibleSlots)
{
    assert(offsetPtr != nullptr);

    int maxOffset = inventoryComputeAlignedMaxOffset(inventoryLength, visibleSlots, 1);
    if (*offsetPtr > maxOffset) {
        *offsetPtr = maxOffset;
    }
    if (*offsetPtr < 0) {
        *offsetPtr = 0;
    }
}

static void barterClampTableOffsets()
{
    if (gPlayerTableInventory != nullptr) {
        inventoryClampOffset(&gPlayerTableOffset, gPlayerTableInventory->length, gInventorySlotsCount);
    }

    if (gBartererTableInventory != nullptr) {
        inventoryClampOffset(&gBartererTableOffset, gBartererTableInventory->length, gInventorySlotsCount);
    }
}

static InventoryItem* inventoryGetReversedSlot(Inventory* inventory, int index)
{
    if (inventory == nullptr || index < 0 || index >= inventory->length) {
        return nullptr;
    }

    return &(inventory->items[inventory->length - (index + 1)]);
}

static int inventoryLootGetSlotX(bool targetInventory, int slotIndex)
{
    int scrollerX = targetInventory ? inventoryLootLayout.rightScrollerX : inventoryLootLayout.leftScrollerX;
    return scrollerX + slotIndex % inventoryLootLayout.columns * INVENTORY_SLOT_WIDTH;
}

static int inventoryLootGetSlotY(int slotIndex)
{
    return inventoryLootLayout.leftScrollerY + slotIndex / inventoryLootLayout.columns * INVENTORY_SLOT_HEIGHT;
}

static bool inventoryLootMouseHitTestScroller(bool targetInventory)
{
    int scrollerX = targetInventory ? inventoryLootLayout.rightScrollerX : inventoryLootLayout.leftScrollerX;
    int scrollerY = targetInventory ? inventoryLootLayout.rightScrollerY : inventoryLootLayout.leftScrollerY;
    return mouseHitTestInWindow(gInventoryWindow,
        scrollerX,
        scrollerY,
        scrollerX + inventoryLootLayout.scrollerWidth,
        scrollerY + inventoryLootLayout.scrollerHeight);
}

static void inventoryLootRenderPaneWeight(unsigned char* windowBuffer, int pitch, bool targetPane, Object* object)
{
    int loot_weight_indicator = settings.ui.loot_weight_indicator;

    bool showLabel = inventoryLootLayout.columns > 1;
    bool showDetailed = loot_weight_indicator == 2 && showLabel;
    bool showSize = loot_weight_indicator == 3;
    bool showSimple = loot_weight_indicator == 1 || (loot_weight_indicator == 2 && !showLabel) || showSize;

    if (!showDetailed && !showSimple) {
        return;
    }

    int containerSizeThreshold = settings.ui.loot_container_size_indicator_threshold;

    // Wt.
    MessageListItem messageListItem;
    messageListItem.num = 30;

    if (showLabel) {
        if (!messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            showSimple = true;
            showDetailed = false;
            showLabel = false;
        }
    }

    char formattedText[30];
    formattedText[0] = '\0';

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    if (inventoryLootFrmImage.isLocked()) {
        int x = targetPane ? inventoryLootLayout.rightScrollerX : inventoryLootLayout.leftScrollerX;
        int y = (targetPane ? inventoryLootLayout.rightScrollerY : inventoryLootLayout.leftScrollerY) + inventoryLootLayout.scrollerHeight + 2;
        blitBufferToBuffer(inventoryLootFrmImage.getData() + inventoryLootFrmImage.getWidth() * y + x,
            inventoryLootLayout.scrollerWidth,
            fontGetLineHeight(),
            inventoryLootFrmImage.getWidth(),
            windowBuffer + pitch * y + x,
            pitch);
    }

    ColorWithFlags color = COLOR_GREEN | DRAW_TEXT_FLAG_NONE;
    int inventoryWeight = objectGetInventoryWeight(object);
    if (ProtoId(object).objectType() == OBJ_TYPE_CRITTER) {
        int currentWeight = inventoryWeight;
        int maxWeight = critterGetStat(object, STAT_CARRY_WEIGHT);
        int weightPercentage = maxWeight < 1 ? 0 : (int)std::ceil(currentWeight * 100 / (float)maxWeight);

        if (showSimple) {
            if (showLabel && !showSize) {
                snprintf(formattedText, sizeof(formattedText), "%s %d/%d", messageListItem.text, currentWeight, maxWeight);
            } else {
                snprintf(formattedText, sizeof(formattedText), "%d/%d", currentWeight, maxWeight);
            }
        } else if (showDetailed) {
            snprintf(formattedText, sizeof(formattedText), "%s %d/%d (%d%%)", messageListItem.text, currentWeight, maxWeight, weightPercentage);
        }

        if (currentWeight > maxWeight) {
            color = COLOR_RED | DRAW_TEXT_FLAG_NONE;
        }
    } else if (targetPane && ProtoId(object).objectType() == OBJ_TYPE_ITEM && itemGetType(object) == ITEM_TYPE_CONTAINER) {
        int currentSize = containerGetTotalSize(object);
        int maxSize = containerGetMaxSize(object);
        int sizePercentage = maxSize < 1 ? 0 : (int)std::ceil(currentSize * 100 / (float)maxSize);

        if (sizePercentage < containerSizeThreshold) {
            fontSetCurrent(oldFont);
            return;
        }

        if (showSize) {
            snprintf(formattedText, sizeof(formattedText), "%d/%d", currentSize, maxSize);
        } else if (showSimple) {
            snprintf(formattedText, sizeof(formattedText), "%d%%", sizePercentage);
        } else if (showDetailed) {
            snprintf(formattedText, sizeof(formattedText), "%s %d (%d%%)", messageListItem.text, inventoryWeight, sizePercentage);
        }
    } else {
        if (showLabel && !showSize) {
            snprintf(formattedText, sizeof(formattedText), "%s %d", messageListItem.text, inventoryWeight);
        } else {
            snprintf(formattedText, sizeof(formattedText), "%d", inventoryWeight);
        }
    }

    if (strlen(formattedText) > 0) {
        int x = targetPane ? inventoryLootLayout.rightScrollerX : inventoryLootLayout.leftScrollerX;
        int y = (targetPane ? inventoryLootLayout.rightScrollerY : inventoryLootLayout.leftScrollerY) + inventoryLootLayout.scrollerHeight + 2;
        inventoryDrawCenteredText(windowBuffer, pitch, inventoryLootLayout.scrollerWidth, x, y, formattedText, color);
    }

    fontSetCurrent(oldFont);
}

static void inventoryScrollerRedrawInventory(const InventoryScroller& scroller, void* context)
{
    InventoryScrollerDisplayContext* displayContext = static_cast<InventoryScrollerDisplayContext*>(context);
    _display_inventory(*scroller.offsetPtr, -1, displayContext->inventoryWindowType);
}

static void inventoryScrollerRedrawTargetInventory(const InventoryScroller& scroller, void* context)
{
    InventoryScrollerDisplayContext* displayContext = static_cast<InventoryScrollerDisplayContext*>(context);
    _display_target_inventory(*scroller.offsetPtr, -1, displayContext->inventory, displayContext->inventoryWindowType);
}

static void inventoryScrollerRedrawBarterTables(const InventoryScroller& scroller, void* context)
{
    InventoryScrollerBarterContext* barterContext = static_cast<InventoryScrollerBarterContext*>(context);
    barterDisplayTables(barterContext->win, barterContext->playerTable, barterContext->bartererTable, -1);
}

static void inventoryScrollerJumpTo(const InventoryScroller& scroller, int offset)
{
    assert(scroller.offsetPtr != nullptr);
    assert(scroller.redraw != nullptr);

    int maxOffset = inventoryComputeAlignedMaxOffset(scroller.totalItems, scroller.visibleSlots, scroller.step);
    int nextOffset = std::clamp(offset, 0, maxOffset);
    if (nextOffset == *scroller.offsetPtr) {
        return;
    }

    *scroller.offsetPtr = nextOffset;
    scroller.redraw(scroller, scroller.redrawContext);
    if (scroller.refreshWindow) {
        windowRefresh(gInventoryWindow);
    }
}

static void inventoryScrollerScroll(const InventoryScroller& scroller, int direction)
{
    assert(scroller.offsetPtr != nullptr);
    assert(scroller.step > 0);
    assert(scroller.redraw != nullptr);

    inventoryScrollerJumpTo(scroller, *scroller.offsetPtr + direction * scroller.step);
}

static void inventoryScrollerPage(const InventoryScroller& scroller, int direction)
{
    assert(scroller.offsetPtr != nullptr);
    assert(scroller.visibleSlots > 0);
    assert(scroller.redraw != nullptr);

    inventoryScrollerJumpTo(scroller, *scroller.offsetPtr + direction * scroller.visibleSlots);
}

static void inventoryScrollerHandleInput(const InventoryScroller& scroller, int keyCode, MouseButtonEvent mouseEvent)
{
    if (scroller.keyUp != -1 && keyCode == scroller.keyUp) {
        inventoryScrollerScroll(scroller, -1);
    } else if (scroller.keyDown != -1 && keyCode == scroller.keyDown) {
        inventoryScrollerScroll(scroller, 1);
    } else if (scroller.keyPageUp != -1 && keyCode == scroller.keyPageUp) {
        inventoryScrollerPage(scroller, -1);
    } else if (scroller.keyPageDown != -1 && keyCode == scroller.keyPageDown) {
        inventoryScrollerPage(scroller, 1);
    } else if (scroller.keyHome != -1 && keyCode == scroller.keyHome) {
        inventoryScrollerJumpTo(scroller, 0);
    } else if (scroller.keyEnd != -1 && keyCode == scroller.keyEnd) {
        inventoryScrollerJumpTo(scroller, scroller.totalItems - scroller.visibleSlots);
    }
    if ((mouseEvent & MouseButtonEvent::Wheel) == MouseButtonEvent::None) {
        return;
    }
    if (!mouseHitTestInWindow(gInventoryWindow, scroller.hitRect.left, scroller.hitRect.top, scroller.hitRect.right, scroller.hitRect.bottom)) {
        return;
    }

    int wheelX;
    int wheelY;
    mouseGetWheel(&wheelX, &wheelY);
    if (wheelY > 0) {
        inventoryScrollerScroll(scroller, -1);
    } else if (wheelY < 0) {
        inventoryScrollerScroll(scroller, 1);
    }
}

static void inventoryUpdateScrollButtons(int upButton, int downButton, int offset, int totalItems, int visibleSlots, int step)
{
    int maxOffset = inventoryComputeAlignedMaxOffset(totalItems, visibleSlots, step);

    if (upButton != -1) {
        if (offset <= 0) {
            buttonDisable(upButton);
        } else {
            buttonEnable(upButton);
        }
    }

    if (downButton != -1) {
        if (offset >= maxOffset) {
            buttonDisable(downButton);
        } else {
            buttonEnable(downButton);
        }
    }
}

static bool hasPartySlots()
{
    return inventoryLootLayout.columns == 2
        && _gIsSteal
        && _target_curr_stack == 0
        && partyTargetEquipped != nullptr
        && partyBaseTarget != nullptr
        && _target_stack[0] == partyBaseTarget
        && ProtoId(partyBaseTarget).objectType() == OBJ_TYPE_CRITTER
        && objectIsPartyMember(partyBaseTarget);
}

static Object** getPartySlotItemPtr(InvenSlot slot)
{
    if (!hasPartySlots()) {
        return nullptr;
    }

    switch (slot) {
    case InvenSlot::RightHand:
        return &(partyTargetEquipped->rightHand);
    case InvenSlot::Armor:
        return &(partyTargetEquipped->armor);
    default:
        assert(false);
        return nullptr;
    }
}

static Object* getPartySlotItem(InvenSlot slot)
{
    Object** itemSlot = getPartySlotItemPtr(slot);
    return itemSlot != nullptr ? *itemSlot : nullptr;
}

static Rect getPartySlotRect(InvenSlot slot)
{
    Rect rect {};

    switch (slot) {
    case InvenSlot::RightHand:
        rect.left = inventoryLootLayout.partySlotX;
        rect.top = inventoryLootLayout.partyArmorSlotY + kPartySlotHeight;
        break;
    case InvenSlot::Armor:
        rect.left = inventoryLootLayout.partySlotX;
        rect.top = inventoryLootLayout.partyArmorSlotY;
        break;
    default:
        assert(false);
        return rect;
    }

    rect.bottom = rect.top + kPartySlotHeight - 1;
    rect.right = rect.left + kPartySlotWidth - 1;
    return rect;
}

static void renderPartySlots()
{
    if (!partySlotFrmImage.isLocked() || !inventoryHasWindow()) {
        return;
    }

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    int pitch = inventoryLootLayout.windowWidth;

    Rect armorRect = getPartySlotRect(InvenSlot::Armor);
    Rect weaponRect = getPartySlotRect(InvenSlot::RightHand);
    blitBufferToBuffer(partySlotFrmImage.getData(),
        kPartySlotWidth,
        kPartySlotImageHeight,
        kPartySlotWidth,
        windowBuffer + pitch * armorRect.top + armorRect.left,
        pitch);

    Object* armor = getPartySlotItem(InvenSlot::Armor);
    if (armor != nullptr) {
        int itemX = armorRect.left + (kPartySlotWidth - INVENTORY_LARGE_SLOT_WIDTH) / 2;
        int itemY = armorRect.top + (kPartySlotHeight - INVENTORY_LARGE_SLOT_HEIGHT) / 2 + 3;
        artRender(itemGetInventoryFrmId(armor),
            windowBuffer + pitch * itemY + itemX,
            INVENTORY_LARGE_SLOT_WIDTH,
            INVENTORY_LARGE_SLOT_HEIGHT,
            pitch);
    }

    Object* weapon = getPartySlotItem(InvenSlot::RightHand);
    if (weapon != nullptr) {
        int itemX = weaponRect.left + (kPartySlotWidth - INVENTORY_LARGE_SLOT_WIDTH) / 2;
        int itemY = weaponRect.top + (kPartySlotHeight - INVENTORY_LARGE_SLOT_HEIGHT) / 2 - 3;
        artRender(itemGetInventoryFrmId(weapon),
            windowBuffer + pitch * itemY + itemX,
            INVENTORY_LARGE_SLOT_WIDTH,
            INVENTORY_LARGE_SLOT_HEIGHT,
            pitch);
    }
}

static void createPartySlotButtons()
{
    if (!hasPartySlots()) {
        return;
    }

    bool slotImageLoaded = partySlotFrmImage.lock(OBJ_TYPE_INTERFACE, "partyslot.png");
    if (!slotImageLoaded) {
        return;
    }

    buttonCreateSlot(gInventoryWindow,
        inventoryLootLayout.partySlotX,
        inventoryLootLayout.partyArmorSlotY,
        kPartySlotWidth,
        kPartySlotHeight,
        PARTY_ARMOR_SLOT_KEY,
        inventoryItemSlotOnMouseEnter,
        inventoryItemSlotOnMouseExit);
    buttonCreateSlot(gInventoryWindow,
        inventoryLootLayout.partySlotX,
        inventoryLootLayout.partyArmorSlotY + kPartySlotHeight,
        kPartySlotWidth,
        kPartySlotHeight,
        PARTY_WEAPON_SLOT_KEY,
        inventoryItemSlotOnMouseEnter,
        inventoryItemSlotOnMouseExit);
}

static FrmId buildPartyDisplayFrmId()
{
    WeaponAnimation weaponAnimationCode = WeaponAnimation::None;
    Object* rightHandItem = partyTargetEquipped->rightHand;
    if (rightHandItem != nullptr && itemGetType(rightHandItem) == ITEM_TYPE_WEAPON) {
        weaponAnimationCode = weaponGetAnimationCode(rightHandItem);
    }

    return FrmId(partyBaseTarget, ANIM_STAND, weaponAnimationCode, ROTATION_NE);
}

static FrmId getTargetDisplayFrmId()
{
    if (!hasPartySlots()) {
        return FrmId(_target_stack[_target_curr_stack]->fid);
    }

    return buildPartyDisplayFrmId();
}

// SFALL: Fix for not counting in the weight/size of equipped items on NPC when
// stealing or bartering: what the loot screen has taken out of its target (the
// equipped items while stealing, the hidden ones) still counts in the
// target's weight (nothing can be put into a critter past what it can carry
// with them).
static void lootSetTakenOut()
{
    Object* leftHand = nullptr;
    Object* rightHand = nullptr;
    Object* armor = nullptr;
    if (partyTargetEquipped != nullptr) {
        leftHand = partyTargetEquipped->leftHand;
        rightHand = partyTargetEquipped->rightHand != leftHand ? partyTargetEquipped->rightHand : nullptr;
        armor = partyTargetEquipped->armor;
    }

    itemsSetTakenOut(partyBaseTarget, { leftHand, rightHand, armor }, lootHiddenBox);
}

static void setLootTarget(Object* target, Object* hiddenBox)
{
    assert(target != nullptr);
    assert(hiddenBox != nullptr);

    if (partyBaseTarget != nullptr && partyBaseTarget != target) {
        if (partyTargetEquipped != nullptr) {
            critterRestoreEquipped(partyBaseTarget, *partyTargetEquipped);
        }
        itemMoveAll(hiddenBox, partyBaseTarget);
    }

    itemMoveAllHidden(target, hiddenBox);
    if (partyTargetEquipped != nullptr) {
        *partyTargetEquipped = critterStripEquipped(target);
    }

    partyBaseTarget = target;
    lootHiddenBox = hiddenBox;
    lootSetTakenOut();
    _target_pud = &(target->data.inventory);
    _target_curr_stack = 0;
    _target_stack[0] = target;
    _target_stack_offset[0] = 0;
    gInventoryWindowDudeRotationTimestamp = 0;
}

static void refreshAfterTargetChange()
{
    _display_target_inventory(0, -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
    _display_body(getTargetDisplayFrmId(), INVENTORY_WINDOW_TYPE_LOOT);
}

static int inventoryWrapIndex(int index, int count, int direction)
{
    assert(count > 0);
    return (index + direction + count) % count;
}
static void refreshPartyEquippedAfterAction()
{
    assert(hasPartySlots());

    *partyTargetEquipped = critterStripEquipped(partyBaseTarget);
    lootSetTakenOut();
    gInventoryWindowDudeRotationTimestamp = 0;
}

static int unequipLootArmorFunc(Object* critter)
{
    Object* armor = critterGetArmor(critter);
    if (armor == nullptr) {
        return -1;
    }

    if (!scriptHooks_InvenWield(critter, armor, InvenSlot::Armor, 0, 0)) {
        return -1;
    }

    armor->flags &= ~OBJECT_WORN;
    adjustCritterStatsOnArmorChange(critter, armor, nullptr);
    return 0;
}

static bool tryUnequipPartyItem(InvenSlot slot)
{
    assert(hasPartySlots());

    Object* item = getPartySlotItem(slot);
    if (item == nullptr) {
        return false;
    }

    critterRestoreEquipped(partyBaseTarget, *partyTargetEquipped);
    lootSetTakenOut();

    int rc = -1;
    if (slot == InvenSlot::RightHand) {
        rc = inventoryUnequipFunc(partyBaseTarget, HAND_RIGHT, false);
    } else if (slot == InvenSlot::Armor) {
        rc = unequipLootArmorFunc(partyBaseTarget);
    }

    if (rc == 0) {
        Object* unequippedItem = inventoryFindById(partyBaseTarget, item->id);
        if (unequippedItem != nullptr) {
            // fix stacking
            itemRemoveQuietly(partyBaseTarget, unequippedItem, 1);
            itemAdd(partyBaseTarget, unequippedItem, 1);
        }
    }

    refreshPartyEquippedAfterAction();
    return rc == 0;
}

static bool tryEquipPartyItem(Object* item, bool fromLeftPane)
{
    if (!hasPartySlots() || !partySlotFrmImage.isLocked() || item == nullptr) {
        return false;
    }

    InvenSlot partySlot;
    if (itemGetType(item) == ITEM_TYPE_WEAPON) {
        partySlot = InvenSlot::RightHand;
    } else if (itemGetType(item) == ITEM_TYPE_ARMOR) {
        partySlot = InvenSlot::Armor;
    } else {
        return false;
    }

    Rect rect = getPartySlotRect(partySlot);
    if (!mouseHitTestInWindow(gInventoryWindow, rect.left, rect.top, rect.right + 1, rect.bottom + 1)) {
        return false;
    }

    if (partySlot == InvenSlot::RightHand) {
        if (!scriptHooks_CanUseWeapon(
                critterCanUseWeapon(partyBaseTarget, item, HIT_MODE_RIGHT_WEAPON_PRIMARY),
                partyBaseTarget,
                item,
                HIT_MODE_RIGHT_WEAPON_PRIMARY)) {
            displayMonitorAddMessage("I can't use that."); // TODO: translate
            return false;
        }
    } else if (partySlot == InvenSlot::Armor && !partyMemberProtoIdCanEquipArmor(partyBaseTarget)) {
        displayMonitorAddMessage("I can't use that."); // TODO: translate
        return false;
    }

    critterRestoreEquipped(partyBaseTarget, *partyTargetEquipped);
    lootSetTakenOut();

    // move from player -> party member
    if (fromLeftPane && itemMove(_inven_dude, partyBaseTarget, item, 1) != 0) {
        refreshPartyEquippedAfterAction();
        return false;
    }

    // equip from target's inventory
    int rc = inventoryEquipFunc(partyBaseTarget, item, HAND_RIGHT, false);
    if (rc != 0 && fromLeftPane) {
        itemMove(partyBaseTarget, _inven_dude, item, 1);
    }

    refreshPartyEquippedAfterAction();
    return rc == 0;
}

static bool tryMovePartyItemToLeftPane(InvenSlot slot, Object* item)
{
    if (!hasPartySlots() || item == nullptr) {
        return false;
    }

    if (!tryUnequipPartyItem(slot)) {
        return false;
    }

    Object* movedItem = inventoryFindById(partyBaseTarget, item->id);
    if (movedItem == nullptr) {
        return false;
    }

    if (itemMove(partyBaseTarget, _inven_dude, movedItem, 1) == 0) {
        return true;
    }

    inventoryDisplayMessage(25); // You cannot pick that up. You are at your maximum weight capacity.
    return false;
}

static void handlePartySlotPickup(InvenSlot slot)
{
    Object* item = getPartySlotItem(slot);
    if (item == nullptr) {
        return;
    }

    if (!inventoryHasWindow()) {
        return;
    }

    Rect rect = getPartySlotRect(slot);
    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    assert(partySlotFrmImage.isLocked());
    int sourceY = slot == InvenSlot::RightHand ? kPartySlotHeight : 0;
    blitBufferToBuffer(partySlotFrmImage.getData() + kPartySlotWidth * sourceY,
        kPartySlotWidth,
        kPartySlotHeight,
        kPartySlotWidth,
        windowBuffer + inventoryLootLayout.windowWidth * rect.top + rect.left,
        inventoryLootLayout.windowWidth);
    windowRefreshRect(gInventoryWindow, &rect);

    bool immediate = _ctrl_pressed();
    _drag_item_loop(item, immediate);

    if (!immediate && inventoryLootMouseHitTestScroller(false)) {
        tryMovePartyItemToLeftPane(slot, item);
    } else if (immediate || inventoryLootMouseHitTestScroller(true)) {
        tryUnequipPartyItem(slot);
    }

    _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
    _display_body(getTargetDisplayFrmId(), INVENTORY_WINDOW_TYPE_LOOT);
    windowRefresh(gInventoryWindow);
    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
}

static int inventoryComputeAlignedMaxOffset(int length, int visibleSlots, int scrollStep)
{
    int maxOffset = length - visibleSlots;
    if (maxOffset <= 0) {
        return 0;
    }

    return (maxOffset + scrollStep - 1) / scrollStep * scrollStep;
}

// 0x46E724
void inventoryResetDude()
{
    _inven_dude = gDude;
    inventoryProtoId = CritterProtoTypeId::Dude;
}

int inventoryGetInvenApCost()
{
    int quickPockets = 0;
    if (gDude != nullptr) {
        quickPockets = perkGetRank(gDude, PERK_QUICK_POCKETS);
    }

    return std::max(inventoryApCost - quickPocketsApCostReduction * quickPockets, 0);
}

void inventorySetInvenApCost(int cost)
{
    inventoryApCost = std::max(cost, 0);
}

void inventoryResetInvenApCost()
{
    configGetInt(&gContentConfig, CONTENT_CONFIG_COMBAT_SECTION, "inventory_ap_cost", &inventoryDefaultApCost, kDefaultInventoryApCost);
    configGetInt(&gContentConfig, CONTENT_CONFIG_COMBAT_SECTION, "quick_pockets_ap_cost_reduction", &quickPocketsApCostReduction, kDefaultQuickPocketsApCostReduction);

    inventoryApCost = inventoryDefaultApCost;
}

// inven_set_dude
void inventorySetDude(Object* obj, const ProtoId& protoId)
{
    _inven_dude = obj;
    inventoryProtoId = protoId;
}

void inventoryGetHeldEquipment(InventoryHeldEquipment* held)
{
    held->critter = _inven_dude;
    held->armor = gInventoryArmor;
    held->rightHand = gInventoryRightHandItem;
    held->leftHand = gInventoryLeftHandItem;
}

// TODO(CE): move to more generic location
FrmId inventoryComputeCritterFrmId(Object* critter, const ProtoId& baseProtoId, Object* rightHandItem, Object* leftHandItem, Object* armor, Hand activeHand, AnimationType anim, Rotation rotation)
{
    if (FrmId(critter).objectType() != OBJ_TYPE_CRITTER) {
        return FrmId(critter);
    }

    Proto* proto = nullptr;

    CritterFrameId inventoryFrameId = _art_vault_guy_num;
    if (protoGetProto(baseProtoId, &proto) != -1) {
        inventoryFrameId = FrmId(proto).frameId<CritterFrameId>();
    }

    if (armor != nullptr) {
        if (protoGetProto(armor, &proto) != -1 && proto != nullptr) {
            if (critterGetStat(critter, STAT_GENDER) == GENDER_FEMALE) {
                inventoryFrameId = FrmId(proto->item.data.armor.femaleFid).frameId<CritterFrameId>();
            } else {
                inventoryFrameId = FrmId(proto->item.data.armor.maleFid).frameId<CritterFrameId>();
            }

            if (inventoryFrameId == CritterFrameId::Invalid) {
                inventoryFrameId = _art_vault_guy_num;
            }
        }
    }

    WeaponAnimation animationCode = WeaponAnimation::None;
    Object* itemInHand = activeHand == HAND_RIGHT ? rightHandItem : leftHandItem;
    if (itemInHand != nullptr) {
        if (protoGetProto(itemInHand, &proto) != -1
            && proto != nullptr
            && proto->item.type == ITEM_TYPE_WEAPON) {
            animationCode = proto->item.data.weapon.animationCode;
        }
    }

    // SFALL: Hero Appearance: the dude wears the hero's own art.
    const FrmId frmId = CritterFrmId(inventoryFrameId, anim, animationCode, rotation);
    return critter == gDude ? artHeroFrmId(frmId) : frmId;
}

// inventory_msg_init
// 0x46E73C
static int inventoryMessageListInit()
{
    char path[COMPAT_MAX_PATH];

    if (!messageListInit(&gInventoryMessageList))
        return -1;

    snprintf(path, sizeof(path), "%s%s", gGameMsgPath, "inventry.msg");
    if (!messageListLoad(&gInventoryMessageList, path))
        return -1;

    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_INVENTORY, &gInventoryMessageList);
    return 0;
}

static void inventoryDisplayMessage(int num)
{
    MessageListItem messageListItem;
    messageListItem.num = num;
    if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
        displayMonitorAddMessage(messageListItem.text);
    }
}

// inventory_msg_free
// 0x46E7A0
static int inventoryMessageListFree()
{
    messageListRepositorySetStandardMessageList(STANDARD_MESSAGE_LIST_INVENTORY, nullptr);
    messageListFree(&gInventoryMessageList);
    return 0;
}

// 0x46E7B0
InventoryOpenCheck inventoryCheckOpen(Object* critter, int* actionPoints)
{
    *actionPoints = 0;

    if (!isInCombat()) {
        return InventoryOpenCheck::Ok;
    }

    if (_combat_whose_turn() != critter) {
        return InventoryOpenCheck::NotYourTurn;
    }

    if (critter == gDude) {
        *actionPoints = inventoryGetInvenApCost();
        if (*actionPoints > 0 && *actionPoints > gDude->data.critter.combat.ap) {
            return InventoryOpenCheck::NoActionPoints;
        }
    }

    return InventoryOpenCheck::Ok;
}

void inventoryReportNoActionPoints()
{
    // The inventory's texts are loaded while it's open.
    if (inventoryMessageListInit() == 0) {
        inventoryDisplayMessage(19); // You don't have enough action points to use inventory.
    }
    inventoryMessageListFree();
}

// The game's inventory: its window, the mouse and the keyboard.
static void inventoryRunWindowLoop()
{
    for (;;) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        MouseButtonEvent mouseEvent = mouseGetEvent();

        devAutotestTick();

        InventoryScrollerDisplayContext inventoryScrollerContext { INVENTORY_WINDOW_TYPE_NORMAL, nullptr };
        Rect normalRect = { inventoryLayout.scrollerX, inventoryLayout.scrollerY, inventoryLayout.scrollerX + inventoryLayout.scrollerWidth, inventoryLayout.scrollerY + inventoryLayout.scrollerHeight };
        InventoryScroller normalScroller {
            normalRect,
            &(_stack_offset[_curr_stack]),
            inventoryLayout.columns,
            inventoryLayout.visibleSlots,
            _pud->length,
            KEY_ARROW_UP,
            KEY_ARROW_DOWN,
            KEY_PAGE_UP,
            KEY_PAGE_DOWN,
            KEY_HOME,
            KEY_END,
            inventoryScrollerRedrawInventory,
            &inventoryScrollerContext,
            false,
        };

        // SFALL: Close with 'I'.
        if (keyCode == KEY_ESCAPE || keyCode == KEY_UPPERCASE_I || keyCode == KEY_LOWERCASE_I) {
            break;
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        _display_body(FrmId::Empty(), INVENTORY_WINDOW_TYPE_NORMAL);

        if (gameGetState() == GameState::DialogRequested) {
            break;
        }

        if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X) {
            showQuitConfirmationDialog();
        } else if (keyCode == 2500) {
            _container_exit(keyCode, INVENTORY_WINDOW_TYPE_NORMAL);
        } else {
            inventoryScrollerHandleInput(normalScroller, keyCode, mouseEvent);

            if ((mouseEvent & MouseButtonEvent::RightButtonDown) != MouseButtonEvent::None) {
                if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_HAND) {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
                } else if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
                    inventoryRenderSummary();
                    windowRefresh(gInventoryWindow);
                }
            } else if ((mouseEvent & MouseButtonEvent::LeftButtonDown) != MouseButtonEvent::None) {
                if ((keyCode >= 1000 && keyCode < 1000 + inventoryLayout.visibleSlots) || keyCode == INVENTORY_HAND_RIGHT_KEY || keyCode == INVENTORY_HAND_LEFT_KEY || keyCode == INVENTORY_ARMOR_KEY) {
                    if (inventoryUsesContextMenu()) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_NORMAL);
                    } else {
                        _inven_pickup(keyCode, _stack_offset[_curr_stack]);
                    }
                }
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }
}

// CE: The mobile UI's inventory screen shows the items (see
// `inventoryGetView`); the loop runs its actions (`inventoryQueueAction`)
// with the functions the window loop runs.
static void inventoryRunTouchLoop()
{
    for (;;) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();

        devAutotestTick();

        // A desktop keyboard: Esc and I close, as in the game.
        if (keyCode == KEY_ESCAPE || keyCode == KEY_UPPERCASE_I || keyCode == KEY_LOWERCASE_I || gInventoryCloseRequested) {
            break;
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        if (gameGetState() == GameState::DialogActive) {
            break;
        }

        if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X || keyCode == KEY_F10) {
            showQuitConfirmationDialog();
        } else if (!gInventoryActions.empty()) {
            inventoryPerformQueuedActions();
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }
}

void inventoryOpen()
{
    int actionPointsRequired;
    InventoryOpenCheck check = inventoryCheckOpen(_inven_dude, &actionPointsRequired);
    if (check == InventoryOpenCheck::NotYourTurn) {
        return;
    }

    if (inventoryCommonInit() == -1) {
        return;
    }

    muiInventoryInit();

    if (isInCombat()) {
        if (_inven_dude == gDude) {
            if (check == InventoryOpenCheck::NoActionPoints) {
                inventoryDisplayMessage(19); // You don't have enough action points to use inventory.

                // NOTE: Uninline.
                inventoryCommonFree();

                return;
            }

            if (actionPointsRequired > 0) {
                if (actionPointsRequired > gDude->data.critter.combat.ap) {
                    gDude->data.critter.combat.ap = 0;
                } else {
                    gDude->data.critter.combat.ap -= actionPointsRequired;
                }
                interfaceRenderActionPoints(gDude->data.critter.combat.ap, _combat_free_move);
            }
        }
    }

    Object* oldArmor = critterGetArmor(_inven_dude);
    bool isoWasEnabled = _setup_inventory(INVENTORY_WINDOW_TYPE_NORMAL);
    reg_anim_clear(_inven_dude);
    if (!inventoryScreensWindowless()) {
        inventoryRenderSummary();
        _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_NORMAL);
        inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
    }
    ScopedGameMode gm(GameMode::kInventory);

    gInventoryViewActive = true;
    gInventoryActions.clear();
    gInventoryCloseRequested = false;
    gInventoryExaminedItem = nullptr;
    gInventoryCritterFrmId = gInventoryWindowDudeFrmId;
    if (inventoryScreensWindowless()) {
        // Facing the player, turned by swipes.
        gInventoryWindowDudeRotation = ROTATION_SE;
    }

    if (inventoryScreensWindowless()) {
        inventoryRunTouchLoop();
    } else {
        inventoryRunWindowLoop();
    }

    gInventoryViewActive = false;
    gInventoryActions.clear();
    gInventoryCloseRequested = false;
    gInventoryExaminedItem = nullptr;

    _inven_dude = _stack[0];
    _adjust_fid();

    if (_inven_dude == gDude) {
        Rect rect;
        objectSetFrmId(_inven_dude, gInventoryWindowDudeFrmId, &rect);
        tileWindowRefreshRect(&rect, _inven_dude->elevation);
    }

    Object* newArmor = critterGetArmor(_inven_dude);
    if (_inven_dude == gDude) {
        if (oldArmor != newArmor) {
            interfaceRenderArmorClass(true);
        }
    }

    _exit_inventory(isoWasEnabled);

    // NOTE: Uninline.
    inventoryCommonFree();

    if (_inven_dude == gDude) {
        interfaceUpdateItems(false, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT);
    }
}

// The buttons of the game's inventory window of [inventoryWindowType] (and of
// the dialog's barter window around the trade one).
static void inventoryCreateButtons(int inventoryWindowType)
{
    bool isNormalWindow = inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL;

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        inventoryCreateSlotButtons(1000, inventoryLootLayout.leftScrollerX, inventoryLootLayout.leftScrollerY, inventoryLootLayout.columns);
        inventoryCreateSlotButtons(2000, inventoryLootLayout.rightScrollerX, inventoryLootLayout.rightScrollerY, inventoryLootLayout.columns);
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        int y1 = INVENTORY_TRADE_SCROLLER_Y;
        int y2 = INVENTORY_TRADE_INNER_SCROLLER_Y;

        for (int index = 0; index < gInventorySlotsCount; index++) {
            buttonCreateSlot(gInventoryWindow, INVENTORY_TRADE_LEFT_SCROLLER_X, y1,
                INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, 1000 + index,
                inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
            buttonCreateSlot(gInventoryWindow, INVENTORY_TRADE_RIGHT_SCROLLER_X, y1,
                INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, 2000 + index,
                inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
            buttonCreateSlot(gInventoryWindow, INVENTORY_TRADE_INNER_LEFT_SCROLLER_X, y2,
                INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, 2300 + index,
                inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
            buttonCreateSlot(gInventoryWindow, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X, y2,
                INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, 2400 + index,
                inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
            y1 += INVENTORY_SLOT_HEIGHT;
            y2 += INVENTORY_SLOT_HEIGHT;
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        inventoryCreateSlotButtons(1000, inventoryLayout.scrollerX, inventoryLayout.scrollerY, inventoryLayout.columns);
    } else {
        // Create invisible buttons representing item slots.
        for (int index = 0; index < gInventorySlotsCount; index++) {
            buttonCreateSlot(gInventoryWindow,
                INVENTORY_SCROLLER_X,
                INVENTORY_SLOT_HEIGHT * (gInventorySlotsCount - index - 1) + INVENTORY_SCROLLER_Y,
                INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT,
                999 + gInventorySlotsCount - index,
                inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
        }
    }

    if (isNormalWindow) {
        // Item2 slot
        buttonCreateSlot(gInventoryWindow, inventoryLayout.rightHandSlotX, INVENTORY_RIGHT_HAND_SLOT_Y,
            INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, INVENTORY_HAND_RIGHT_KEY,
            inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);

        // Item1 slot
        buttonCreateSlot(gInventoryWindow, inventoryLayout.leftHandSlotX, INVENTORY_LEFT_HAND_SLOT_Y,
            INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, INVENTORY_HAND_LEFT_KEY,
            inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);

        // Armor slot
        buttonCreateSlot(gInventoryWindow, inventoryLayout.armorSlotX, INVENTORY_ARMOR_SLOT_Y,
            INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, INVENTORY_ARMOR_KEY,
            inventoryItemSlotOnMouseEnter, inventoryItemSlotOnMouseExit);
    }

    std::pair<int, int> doneButtonXY = { -1, -1 };
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        doneButtonXY = { inventoryLayout.doneButtonX, 329 };
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_USE_ITEM_ON) {
        doneButtonXY = { 233, 328 };
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        doneButtonXY = { inventoryLootLayout.doneButtonX, 331 };
    }

    if (doneButtonXY.first != -1) {
        buttonCreateActionWithFrm(gInventoryWindow,
            doneButtonXY.first, doneButtonXY.second,
            -1, KEY_ESCAPE,
            InterfaceFrameId::LittleRedButtonUp,
            InterfaceFrameId::LittleRedButtonDown);
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        // Left inventory up button.
        gInventoryScrollUpButton = buttonCreateActionWithFrm(gInventoryWindow,
            109, 56,
            KEY_ARROW_UP, -1,
            InterfaceFrameId::DialogUpButtonUp,
            InterfaceFrameId::DialogUpButtonDown);
        if (gInventoryScrollUpButton != -1) {
            buttonSetDisabledFrm(gInventoryScrollUpButton,
                "di_upds.png", "di_upds.png", "di_upds.png");
        }

        // Right inventory up button.
        gSecondaryInventoryScrollUpButton = buttonCreateActionWithFrm(gInventoryWindow,
            342, 56,
            KEY_CTRL_ARROW_UP, -1,
            InterfaceFrameId::DialogUpButtonUp,
            InterfaceFrameId::DialogUpButtonDown);
        if (gSecondaryInventoryScrollUpButton != -1) {
            buttonSetDisabledFrm(gSecondaryInventoryScrollUpButton,
                "di_upds.png", "di_upds.png", "di_upds.png");
        }
    } else {
        int scrollUpX = isNormalWindow                          ? inventoryLayout.scrollButtonX
            : inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT ? inventoryLootLayout.leftScrollButtonX
                                                                : 128;
        // Left inventory up button.
        gInventoryScrollUpButton = buttonCreateActionWithFrm(gInventoryWindow,
            scrollUpX, 39,
            KEY_ARROW_UP, -1,
            InterfaceFrameId::InventoryButtonOutUp,
            InterfaceFrameId::InventoryButtonInUp);
        if (gInventoryScrollUpButton != -1) {
            buttonSetDisabledFrm(gInventoryScrollUpButton,
                InterfaceFrameId::InventoryButtonUpDisabled,
                InterfaceFrameId::InventoryButtonUpDisabled,
                InterfaceFrameId::InventoryButtonUpDisabled);
            buttonDisable(gInventoryScrollUpButton);
        }

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            // Right inventory up button.
            gSecondaryInventoryScrollUpButton = buttonCreateActionWithFrm(gInventoryWindow,
                inventoryLootLayout.rightScrollButtonX, 39,
                KEY_CTRL_ARROW_UP, -1,
                InterfaceFrameId::InventoryButtonOutUp,
                InterfaceFrameId::InventoryButtonInUp);
            if (gSecondaryInventoryScrollUpButton != -1) {
                buttonSetDisabledFrm(gSecondaryInventoryScrollUpButton,
                    InterfaceFrameId::InventoryButtonUpDisabled,
                    InterfaceFrameId::InventoryButtonUpDisabled,
                    InterfaceFrameId::InventoryButtonUpDisabled);
                buttonDisable(gSecondaryInventoryScrollUpButton);
            }
        }
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        // Left inventory down button.
        gInventoryScrollDownButton = buttonCreateActionWithFrm(gInventoryWindow,
            109, 82,
            KEY_ARROW_DOWN, -1,
            InterfaceFrameId::DialogDownButtonUp,
            InterfaceFrameId::DialogDownButtonDown);
        if (gInventoryScrollDownButton != -1) {
            buttonSetDisabledFrm(gInventoryScrollDownButton,
                "di_dnds.png", "di_dnds.png", "di_dnds.png");
        }

        // Right inventory down button
        gSecondaryInventoryScrollDownButton = buttonCreateActionWithFrm(gInventoryWindow,
            342, 82,
            KEY_CTRL_ARROW_DOWN, -1,
            InterfaceFrameId::DialogDownButtonUp,
            InterfaceFrameId::DialogDownButtonDown);
        if (gSecondaryInventoryScrollDownButton != -1) {
            buttonSetDisabledFrm(gSecondaryInventoryScrollDownButton,
                "di_dnds.png", "di_dnds.png", "di_dnds.png");
        }

        // Invisible button representing left character.
        buttonCreateAction(gInventoryBarterBackgroundWindow,
            INVENTORY_BARTER_LEFT_BODY_VIEW_X, INVENTORY_BARTER_LEFT_BODY_VIEW_Y, INVENTORY_BODY_VIEW_WIDTH, INVENTORY_BODY_VIEW_HEIGHT, 2500);

        // Invisible button representing right character.
        buttonCreateAction(gInventoryBarterBackgroundWindow,
            560, 25, INVENTORY_BODY_VIEW_WIDTH, INVENTORY_BODY_VIEW_HEIGHT, 2501);
    } else {
        int scrollDownX = isNormalWindow                        ? inventoryLayout.scrollButtonX
            : inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT ? inventoryLootLayout.leftScrollButtonX
                                                                : 128;
        // Left inventory down button.
        gInventoryScrollDownButton = buttonCreateActionWithFrm(gInventoryWindow,
            scrollDownX, 62,
            KEY_ARROW_DOWN, -1,
            InterfaceFrameId::InventoryButtonOutDown,
            InterfaceFrameId::InventoryButtonInDown);
        if (gInventoryScrollDownButton != -1) {
            buttonSetDisabledFrm(gInventoryScrollDownButton,
                InterfaceFrameId::InventoryButtonDownDisabled,
                InterfaceFrameId::InventoryButtonDownDisabled,
                InterfaceFrameId::InventoryButtonDownDisabled);
            buttonDisable(gInventoryScrollDownButton);
        }

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            // Invisible button representing left character.
            buttonCreateAction(gInventoryWindow,
                inventoryLootLayout.leftBodyViewX, INVENTORY_LOOT_LEFT_BODY_VIEW_Y,
                INVENTORY_BODY_VIEW_WIDTH, INVENTORY_BODY_VIEW_HEIGHT, 2500);

            // Right inventory down button.
            gSecondaryInventoryScrollDownButton = buttonCreateActionWithFrm(gInventoryWindow,
                inventoryLootLayout.rightScrollButtonX, 62,
                KEY_CTRL_ARROW_DOWN, -1,
                InterfaceFrameId::InventoryButtonOutDown, InterfaceFrameId::InventoryButtonInDown);
            if (gSecondaryInventoryScrollDownButton != -1) {
                buttonSetDisabledFrm(gSecondaryInventoryScrollDownButton,
                    InterfaceFrameId::InventoryButtonDownDisabled, InterfaceFrameId::InventoryButtonDownDisabled, InterfaceFrameId::InventoryButtonDownDisabled);
                buttonDisable(gSecondaryInventoryScrollDownButton);
            }

            // Invisible button representing right character.
            buttonCreateAction(gInventoryWindow,
                inventoryLootLayout.rightBodyViewX, INVENTORY_LOOT_RIGHT_BODY_VIEW_Y,
                INVENTORY_BODY_VIEW_WIDTH, INVENTORY_BODY_VIEW_HEIGHT, 2501);
        } else {
            // Invisible button representing character (in inventory and use on dialogs).
            buttonCreateAction(gInventoryWindow,
                isNormalWindow ? inventoryLayout.bodyViewX : INVENTORY_PC_BODY_VIEW_X,
                INVENTORY_PC_BODY_VIEW_Y,
                INVENTORY_BODY_VIEW_WIDTH, INVENTORY_BODY_VIEW_HEIGHT, 2500);
        }
    }

    if (inventoryWindowType != INVENTORY_WINDOW_TYPE_TRADE) {
        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            if (!_gIsSteal) {
                // Take all button.
                buttonCreateActionWithFrm(gInventoryWindow,
                    inventoryLootLayout.takeAllButtonX, 204, 2502, -1,
                    InterfaceFrameId::InventoryLootAllUp, InterfaceFrameId::InventoryLootAllDown);
            }
        }
    } else {
        // Left offered inventory up button.
        playerTableScrollUpButton = buttonCreateActionWithFrm(gInventoryWindow,
            128, 113, KEY_PAGE_UP, -1,
            InterfaceFrameId::InventoryButtonOutUp, InterfaceFrameId::InventoryButtonInUp);
        if (playerTableScrollUpButton != -1) {
            buttonSetDisabledFrm(playerTableScrollUpButton,
                InterfaceFrameId::InventoryButtonUpDisabled,
                InterfaceFrameId::InventoryButtonUpDisabled,
                InterfaceFrameId::InventoryButtonUpDisabled);
        }

        // Left offered inventory down button.
        playerTableScrollDownButton = buttonCreateActionWithFrm(gInventoryWindow,
            128, 136, KEY_PAGE_DOWN, -1,
            InterfaceFrameId::InventoryButtonOutDown, InterfaceFrameId::InventoryButtonInDown);
        if (playerTableScrollDownButton != -1) {
            buttonSetDisabledFrm(playerTableScrollDownButton,
                InterfaceFrameId::InventoryButtonDownDisabled,
                InterfaceFrameId::InventoryButtonDownDisabled,
                InterfaceFrameId::InventoryButtonDownDisabled);
        }

        // Right offered inventory up button.
        bartererTableScrollUpButton = buttonCreateActionWithFrm(gInventoryWindow,
            333, 113, KEY_CTRL_PAGE_UP, -1,
            InterfaceFrameId::InventoryButtonOutUp, InterfaceFrameId::InventoryButtonInUp);
        if (bartererTableScrollUpButton != -1) {
            buttonSetDisabledFrm(bartererTableScrollUpButton,
                InterfaceFrameId::InventoryButtonUpDisabled,
                InterfaceFrameId::InventoryButtonUpDisabled,
                InterfaceFrameId::InventoryButtonUpDisabled);
        }

        // Right offered inventory down button.
        bartererTableScrollDownButton = buttonCreateActionWithFrm(gInventoryWindow,
            333, 136, KEY_CTRL_PAGE_DOWN, -1,
            InterfaceFrameId::InventoryButtonOutDown, InterfaceFrameId::InventoryButtonInDown);
        if (bartererTableScrollDownButton != -1) {
            buttonSetDisabledFrm(bartererTableScrollDownButton,
                InterfaceFrameId::InventoryButtonDownDisabled,
                InterfaceFrameId::InventoryButtonDownDisabled,
                InterfaceFrameId::InventoryButtonDownDisabled);
        }
    }
}

// 0x46EC90
static bool _setup_inventory(int inventoryWindowType)
{
    _dropped_explosive = 0;
    _curr_stack = 0;
    _stack_offset[0] = 0;
    gInventorySlotsCount = 6;
    _pud = &(_inven_dude->data.inventory);
    _stack[0] = _inven_dude;
    gInventoryScrollUpButton = -1;
    gInventoryScrollDownButton = -1;
    gSecondaryInventoryScrollUpButton = -1;
    gSecondaryInventoryScrollDownButton = -1;
    playerTableScrollUpButton = -1;
    playerTableScrollDownButton = -1;
    bartererTableScrollUpButton = -1;
    bartererTableScrollDownButton = -1;
    bool isNormalWindow = inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL;

    if (inventoryWindowType <= INVENTORY_WINDOW_TYPE_LOOT) {
        if (isNormalWindow) {
            inventoryNormalLayoutUpdate();
        } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            inventoryLootLayoutUpdate();
            gInventorySlotsCount = inventoryLootLayout.visibleSlots;
        }

        const InventoryWindowDescription* windowDescription = &(gInventoryWindowDescriptions[inventoryWindowType]);
        int windowWidth = inventoryGetWindowWidth(inventoryWindowType);
        int windowHeight = inventoryGetWindowHeight(inventoryWindowType);

        // Maintain original position in original resolution, otherwise center it.
        bool preserveVanillaX = screenGetWidth() == 640
            && windowWidth == windowDescription->width
            && inventoryWindowType != INVENTORY_WINDOW_TYPE_LOOT;
        bool preserveVanillaY = screenGetHeight() == 480;
        int inventoryWindowX = preserveVanillaX
            ? INVENTORY_WINDOW_X
            : (screenGetWidth() - windowWidth) / 2;
        int inventoryWindowY = preserveVanillaY
            ? INVENTORY_WINDOW_Y
            : inventoryGetCenteredWindowY(windowHeight);
        // CE: The mobile UI inventory and loot screens draw everything
        // themselves, the game's window isn't made (its drawing functions do
        // nothing without it, the game logic stays). The use-item-on window
        // isn't set up under it (`inventoryOpenUseItemOn`).
        if (!inventoryScreensWindowless()) {
            gInventoryWindow = windowCreate(
                inventoryWindowX, inventoryWindowY, windowWidth, windowHeight,
                static_cast<ColorWithFlags>(257), WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
        }
        gInventoryWindowMaxX = windowWidth + inventoryWindowX;
        gInventoryWindowMaxY = windowHeight + inventoryWindowY;

        Buffer2D destBuf { windowGetBuffer(gInventoryWindow), windowWidth, windowHeight };

        if (!inventoryHasWindow()) {
            // Nothing to draw into.
        } else if (isNormalWindow) {
            assert(inventoryFrmImage.isLocked());
            blitBuffer2D(inventoryFrmImage.getBuffer(), destBuf);
        } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            assert(inventoryLootFrmImage.isLocked());
            blitBuffer2D(inventoryLootFrmImage.getBuffer(), destBuf);
        } else {
            FrmImage backgroundFrmImage;
            if (backgroundFrmImage.lock(windowDescription->frmId)) {
                blitBuffer2D(backgroundFrmImage.getBuffer(), destBuf);
            }
        }

        gInventoryPrintItemDescriptionHandler = displayMonitorAddMessage;
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        int extraSlots = gameDialogIsBarterWindowExpanded() ? kExpandedBarterExtraSlots : 0;
        gInventorySlotsCount = kTradeSlotCount + extraSlots;

        // CE: The mobile UI barter screen draws everything itself, the
        // dialog has no barter window then (see `gameDialogCreateBarterWindow`).
        if (!inventoryScreensWindowless()) {
            if (gInventoryBarterBackgroundWindow == -1) {
                exit(1);
            }

            int tradeWindowHeight = INVENTORY_TRADE_WINDOW_HEIGHT + extraSlots * INVENTORY_SLOT_HEIGHT;

            // Trade inventory window is a part of game dialog, which is 640x480.
            Rect bgWindowRect;
            windowGetRect(gInventoryBarterBackgroundWindow, &bgWindowRect);
            int tradeWindowX = bgWindowRect.left + INVENTORY_TRADE_WINDOW_X;
            int tradeWindowY = bgWindowRect.top + INVENTORY_TRADE_WINDOW_Y;
            gInventoryWindow = windowCreate(tradeWindowX, tradeWindowY, INVENTORY_TRADE_WINDOW_WIDTH, tradeWindowHeight, static_cast<ColorWithFlags>(257), WINDOW_NONE);

            Buffer2D dest { windowGetBuffer(gInventoryWindow), INVENTORY_TRADE_WINDOW_WIDTH, tradeWindowHeight };
            ConstBuffer2D src { windowGetBuffer(gInventoryBarterBackgroundWindow), INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH, windowGetHeight(gInventoryBarterBackgroundWindow) };
            blitBuffer2D(src, INVENTORY_TRADE_WINDOW_X, 0, INVENTORY_TRADE_WINDOW_WIDTH, tradeWindowHeight, dest);

            gInventoryWindowMaxX = tradeWindowX + INVENTORY_TRADE_WINDOW_WIDTH;
            gInventoryWindowMaxY = tradeWindowY + tradeWindowHeight;
        }

        gInventoryPrintItemDescriptionHandler = gameDialogRenderSupplementaryMessage;
    }

    // CE: The mobile UI's screens have their own controls.
    if (inventoryHasWindow()) {
        inventoryCreateButtons(inventoryWindowType);
    }

    gInventoryRightHandItem = nullptr;
    gInventoryArmor = nullptr;
    gInventoryLeftHandItem = nullptr;
    partyTargetEquipped = nullptr;
    partyBaseTarget = nullptr;

    for (int index = 0; index < _pud->length; index++) {
        InventoryItem* inventoryItem = &(_pud->items[index]);
        Object* item = inventoryItem->item;
        if ((item->flags & OBJECT_IN_LEFT_HAND) != OBJECT_NONE) {
            if ((item->flags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
                gInventoryRightHandItem = item;
            }
            gInventoryLeftHandItem = item;
        } else if ((item->flags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
            gInventoryRightHandItem = item;
        } else if ((item->flags & OBJECT_WORN) != OBJECT_NONE) {
            gInventoryArmor = item;
        }
    }

    if (gInventoryLeftHandItem != nullptr) {
        itemRemoveWithReason(_inven_dude, gInventoryLeftHandItem, 1, RemoveInventoryObjectHookReason::LeftHandEquipped);
    }

    if (gInventoryRightHandItem != nullptr && gInventoryRightHandItem != gInventoryLeftHandItem) {
        itemRemoveWithReason(_inven_dude, gInventoryRightHandItem, 1, RemoveInventoryObjectHookReason::RightHandEquipped);
    }

    if (gInventoryArmor != nullptr) {
        itemRemoveWithReason(_inven_dude, gInventoryArmor, 1, RemoveInventoryObjectHookReason::ArmorEquipped);
    }

    _adjust_fid();

    bool isoWasEnabled = isoDisable();

    _gmouse_disable(0);
    touch_set_touchscreen_mode(true);
    touch_set_pan_mode(true);

    return isoWasEnabled;
}

// 0x46FBD8
static void _exit_inventory(bool shouldEnableIso)
{
    _inven_dude = _stack[0];

    if (gInventoryLeftHandItem != nullptr) {
        gInventoryLeftHandItem->flags |= OBJECT_IN_LEFT_HAND;
        if (gInventoryLeftHandItem == gInventoryRightHandItem) {
            gInventoryLeftHandItem->flags |= OBJECT_IN_RIGHT_HAND;
        }

        itemAdd(_inven_dude, gInventoryLeftHandItem, 1);
    }

    if (gInventoryRightHandItem != nullptr && gInventoryRightHandItem != gInventoryLeftHandItem) {
        gInventoryRightHandItem->flags |= OBJECT_IN_RIGHT_HAND;
        itemAdd(_inven_dude, gInventoryRightHandItem, 1);
    }

    if (gInventoryArmor != nullptr) {
        gInventoryArmor->flags |= OBJECT_WORN;
        itemAdd(_inven_dude, gInventoryArmor, 1);
    }

    gInventoryRightHandItem = nullptr;
    gInventoryArmor = nullptr;
    gInventoryLeftHandItem = nullptr;

    for (int index = 0; index < INVENTORY_FRM_COUNT; index++) {
        _inventoryFrmImages[index].unlock();
    }

    if (shouldEnableIso) {
        isoEnable();
    }

    windowDestroy(gInventoryWindow);
    gInventoryWindow = -1;
    inventoryFrmImage.unlock();
    inventoryLootFrmImage.unlock();
    partySlotFrmImage.unlock();

    _gmouse_enable();
    touch_set_touchscreen_mode(false);
    touch_set_pan_mode(false);

    if (_dropped_explosive) {
        Attack attack;
        attackInit(&attack, gDude, nullptr, HIT_MODE_PUNCH, HIT_LOCATION_TORSO);
        attack.attackerFlags = DAM_HIT;
        attack.tile = gDude->tile;
        _compute_explosion_on_extras(&attack, 0, 0, 1);

        Object* watcher = nullptr;
        for (int index = 0; index < attack.extrasLength; index++) {
            Object* critter = attack.extras[index];
            if (critter != gDude
                && critter->data.critter.combat.team != gDude->data.critter.combat.team
                && rollIsSuccess(statRoll(critter, STAT_PERCEPTION, 0, nullptr))) {
                critterSetWhoHitMe(critter, gDude);

                if (watcher == nullptr) {
                    watcher = critter;
                }
            }
        }

        if (watcher != nullptr) {
            if (!isInCombat()) {
                CombatStartData combat;
                combat.attacker = watcher;
                combat.defender = gDude;
                combat.actionPointsBonus = 0;
                combat.accuracyBonus = 0;
                combat.damageBonus = 0;
                combat.minDamage = 0;
                combat.maxDamage = INT_MAX;
                combat.overrideAttackResults = 0;
                scriptsRequestCombat(&combat);
            }
        }

        _dropped_explosive = false;
    }
}

// 0x46FDF4 display_inventory
static void _display_inventory(int stackOffset, int dragSlotIndex, int inventoryWindowType)
{
    if (!inventoryHasWindow()) {
        return;
    }

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    int pitch;

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        pitch = inventoryLayout.windowWidth;

        unsigned char* backgroundData = inventoryFrmImage.getData();
        int backgroundWidth = inventoryFrmImage.getWidth();
        if (backgroundData != nullptr) {
            // Clear scroll view background.
            blitBufferToBuffer(backgroundData + backgroundWidth * inventoryLayout.scrollerY + inventoryLayout.scrollerX,
                inventoryLayout.scrollerWidth,
                inventoryLayout.scrollerHeight,
                backgroundWidth,
                windowBuffer + pitch * inventoryLayout.scrollerY + inventoryLayout.scrollerX,
                pitch);

            // Clear armor button background.
            blitBufferToBuffer(backgroundData + backgroundWidth * INVENTORY_ARMOR_SLOT_Y + inventoryLayout.armorSlotX,
                INVENTORY_LARGE_SLOT_WIDTH,
                INVENTORY_LARGE_SLOT_HEIGHT,
                backgroundWidth,
                windowBuffer + pitch * INVENTORY_ARMOR_SLOT_Y + inventoryLayout.armorSlotX,
                pitch);

            if (gInventoryLeftHandItem != nullptr && gInventoryLeftHandItem == gInventoryRightHandItem) {
                blitBufferToBuffer(backgroundData + backgroundWidth * INVENTORY_LEFT_HAND_SLOT_Y + inventoryLayout.leftHandSlotX,
                    INVENTORY_LARGE_SLOT_WIDTH * 2,
                    INVENTORY_LARGE_SLOT_HEIGHT,
                    backgroundWidth,
                    windowBuffer + pitch * INVENTORY_LEFT_HAND_SLOT_Y + inventoryLayout.leftHandSlotX,
                    pitch);
            } else {
                // Clear both items in one go.
                blitBufferToBuffer(backgroundData + backgroundWidth * INVENTORY_LEFT_HAND_SLOT_Y + inventoryLayout.leftHandSlotX,
                    INVENTORY_LARGE_SLOT_WIDTH * 2,
                    INVENTORY_LARGE_SLOT_HEIGHT,
                    backgroundWidth,
                    windowBuffer + pitch * INVENTORY_LEFT_HAND_SLOT_Y + inventoryLayout.leftHandSlotX,
                    pitch);
            }
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_USE_ITEM_ON) {
        pitch = INVENTORY_USE_ON_WINDOW_WIDTH;

        FrmImage backgroundFrmImage;
        if (backgroundFrmImage.lock(InterfaceFrameId::UseItemOnWindow)) {
            // Clear scroll view background.
            blitBufferToBuffer(backgroundFrmImage.getData() + pitch * INVENTORY_SCROLLER_Y + INVENTORY_SCROLLER_X,
                INVENTORY_SLOT_WIDTH,
                gInventorySlotsCount * INVENTORY_SLOT_HEIGHT,
                pitch,
                windowBuffer + pitch * INVENTORY_SCROLLER_Y + INVENTORY_SCROLLER_X,
                pitch);
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        pitch = inventoryLootLayout.windowWidth;

        unsigned char* backgroundData = inventoryLootFrmImage.getData();
        int backgroundWidth = inventoryLootFrmImage.getWidth();
        if (backgroundData != nullptr) {
            // Clear scroll view background.
            blitBufferToBuffer(backgroundData + backgroundWidth * inventoryLootLayout.leftScrollerY + inventoryLootLayout.leftScrollerX,
                inventoryLootLayout.scrollerWidth,
                inventoryLootLayout.scrollerHeight,
                backgroundWidth,
                windowBuffer + pitch * inventoryLootLayout.leftScrollerY + inventoryLootLayout.leftScrollerX,
                pitch);
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        pitch = INVENTORY_TRADE_WINDOW_WIDTH;

        windowBuffer = windowGetBuffer(gInventoryWindow);

        blitBufferToBuffer(windowGetBuffer(gInventoryBarterBackgroundWindow) + INVENTORY_TRADE_LEFT_SCROLLER_Y * INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH + INVENTORY_TRADE_LEFT_SCROLLER_X + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount, INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH, windowBuffer + pitch * INVENTORY_TRADE_LEFT_SCROLLER_Y + INVENTORY_TRADE_LEFT_SCROLLER_X, pitch);
    } else {
        assert(false && "Should be unreachable");
    }

    int visibleSlots = inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL ? inventoryLayout.visibleSlots : gInventorySlotsCount;
    int step = inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL ? inventoryLayout.columns : 1;
    inventoryUpdateScrollButtons(gInventoryScrollUpButton, gInventoryScrollDownButton, stackOffset, _pud->length, visibleSlots, step);

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        for (int slotIndex = 0; slotIndex + stackOffset < _pud->length && slotIndex < inventoryLayout.visibleSlots; slotIndex += 1) {
            int itemIndex = slotIndex + stackOffset + 1;
            int row = slotIndex / inventoryLayout.columns;
            int column = slotIndex % inventoryLayout.columns;
            int itemPaddingY = INVENTORY_SLOT_PADDING + (inventoryLayout.columns > 1 ? INVENTORY_SLOT_PADDING : 0);
            int offset = pitch * (inventoryLayout.scrollerY + row * INVENTORY_SLOT_HEIGHT + itemPaddingY)
                + inventoryLayout.scrollerX + column * INVENTORY_SLOT_WIDTH + INVENTORY_SLOT_PADDING;

            InventoryItem* inventoryItem = &(_pud->items[_pud->length - itemIndex]);

            const FrmId inventoryFrmId = itemGetInventoryFrmId(inventoryItem->item);
            artRender(inventoryFrmId, windowBuffer + offset, INVENTORY_SLOT_WIDTH_PAD, INVENTORY_SLOT_HEIGHT_PAD, pitch);
            _display_inventory_info(inventoryItem->item, inventoryItem->quantity, windowBuffer + offset, pitch, slotIndex == dragSlotIndex);
        }
    } else {
        int y = 0;
        for (int slotIndex = 0; slotIndex + stackOffset < _pud->length && slotIndex < gInventorySlotsCount; slotIndex += 1) {
            int itemIndex = slotIndex + stackOffset + 1;

            int offset;
            if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
                offset = pitch * (y + INVENTORY_TRADE_LEFT_SCROLLER_Y_PAD) + INVENTORY_TRADE_LEFT_SCROLLER_X_PAD;
            } else {
                if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
                    offset = pitch * (inventoryLootGetSlotY(slotIndex) + INVENTORY_SLOT_PADDING) + inventoryLootGetSlotX(false, slotIndex) + INVENTORY_SLOT_PADDING;
                } else {
                    offset = pitch * (y + INVENTORY_SCROLLER_Y_PAD) + INVENTORY_SCROLLER_X_PAD;
                }
            }

            InventoryItem* inventoryItem = &(_pud->items[_pud->length - itemIndex]);

            const FrmId inventoryFrmId = itemGetInventoryFrmId(inventoryItem->item);
            artRender(inventoryFrmId, windowBuffer + offset, INVENTORY_SLOT_WIDTH_PAD, INVENTORY_SLOT_HEIGHT_PAD, pitch);
            _display_inventory_info(inventoryItem->item, inventoryItem->quantity, windowBuffer + offset, pitch, slotIndex == dragSlotIndex);

            if (inventoryWindowType != INVENTORY_WINDOW_TYPE_LOOT) {
                y += INVENTORY_SLOT_HEIGHT;
            }
        }
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        if (gInventoryRightHandItem != nullptr) {
            int width = gInventoryRightHandItem == gInventoryLeftHandItem ? INVENTORY_LARGE_SLOT_WIDTH * 2 : INVENTORY_LARGE_SLOT_WIDTH;
            const FrmId inventoryFrmId = itemGetInventoryFrmId(gInventoryRightHandItem);
            artRender(inventoryFrmId, windowBuffer + pitch * INVENTORY_RIGHT_HAND_SLOT_Y + inventoryLayout.rightHandSlotX, width, INVENTORY_LARGE_SLOT_HEIGHT, pitch);
        }

        if (gInventoryLeftHandItem != nullptr && gInventoryLeftHandItem != gInventoryRightHandItem) {
            const FrmId inventoryFrmId = itemGetInventoryFrmId(gInventoryLeftHandItem);
            artRender(inventoryFrmId, windowBuffer + pitch * INVENTORY_LEFT_HAND_SLOT_Y + inventoryLayout.leftHandSlotX, INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, pitch);
        }

        if (gInventoryArmor != nullptr) {
            const FrmId inventoryFrmId = itemGetInventoryFrmId(gInventoryArmor);
            artRender(inventoryFrmId, windowBuffer + pitch * INVENTORY_ARMOR_SLOT_Y + inventoryLayout.armorSlotX, INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, pitch);
        }
    }

    // CE: Show items weight.
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        inventoryLootRenderPaneWeight(windowBuffer, pitch, false, _stack[0]);
    }

    windowRefresh(gInventoryWindow);
}

// Render inventory item.
//
// [stackOffset] is an index of the first visible item in the scrolling view.
// [dragSlotIndex] is an index of item being dragged (it decreases displayed number of items in inner functions).
//
// 0x47036C
static void _display_target_inventory(int stackOffset, int dragSlotIndex, Inventory* inventory, int inventoryWindowType)
{
    if (!inventoryHasWindow()) {
        return;
    }

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);

    int pitch;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        pitch = inventoryLootLayout.windowWidth;

        unsigned char* backgroundData = inventoryLootFrmImage.getData();
        int backgroundWidth = inventoryLootFrmImage.getWidth();
        if (backgroundData != nullptr) {
            blitBufferToBuffer(backgroundData + backgroundWidth * inventoryLootLayout.rightScrollerY + inventoryLootLayout.rightScrollerX,
                inventoryLootLayout.scrollerWidth,
                inventoryLootLayout.scrollerHeight,
                backgroundWidth,
                windowBuffer + pitch * inventoryLootLayout.rightScrollerY + inventoryLootLayout.rightScrollerX,
                pitch);
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        pitch = INVENTORY_TRADE_WINDOW_WIDTH;

        unsigned char* src = windowGetBuffer(gInventoryBarterBackgroundWindow);
        blitBufferToBuffer(src + INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH * INVENTORY_TRADE_RIGHT_SCROLLER_Y + INVENTORY_TRADE_RIGHT_SCROLLER_X + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount, INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH, windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_TRADE_RIGHT_SCROLLER_Y + INVENTORY_TRADE_RIGHT_SCROLLER_X, INVENTORY_TRADE_WINDOW_WIDTH);
    } else {
        assert(false && "Should be unreachable");
    }

    int y = 0;
    for (int slotIndex = 0; slotIndex < gInventorySlotsCount; slotIndex++) {
        int itemIndex = stackOffset + slotIndex;
        if (itemIndex >= inventory->length) {
            break;
        }

        int offset;
        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
            offset = pitch * (inventoryLootGetSlotY(slotIndex) + INVENTORY_SLOT_PADDING) + inventoryLootGetSlotX(true, slotIndex) + INVENTORY_SLOT_PADDING;
        } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
            offset = pitch * (y + INVENTORY_TRADE_RIGHT_SCROLLER_Y_PAD) + INVENTORY_TRADE_RIGHT_SCROLLER_X_PAD;
        } else {
            assert(false && "Should be unreachable");
        }

        InventoryItem* inventoryItem = &(inventory->items[inventory->length - (itemIndex + 1)]);
        const FrmId inventoryFrmId = itemGetInventoryFrmId(inventoryItem->item);
        artRender(inventoryFrmId, windowBuffer + offset, INVENTORY_SLOT_WIDTH_PAD, INVENTORY_SLOT_HEIGHT_PAD, pitch);
        _display_inventory_info(inventoryItem->item, inventoryItem->quantity, windowBuffer + offset, pitch, slotIndex == dragSlotIndex);

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
            y += INVENTORY_SLOT_HEIGHT;
        }
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT || inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        inventoryUpdateScrollButtons(gSecondaryInventoryScrollUpButton, gSecondaryInventoryScrollDownButton, stackOffset, inventory->length, gInventorySlotsCount);
    }

    // CE: Show items weight.
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
        inventoryLootRenderPaneWeight(windowBuffer, pitch, true, _target_stack[_target_curr_stack]);
        if (hasPartySlots()) {
            renderPartySlots();
        }
    }
}

// Renders inventory item quantity.
//
// 0x4705A0
static void _display_inventory_info(Object* item, int quantity, unsigned char* dest, int pitch, bool isDragged)
{
    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    char formattedText[12];

    // NOTE: Original code is slightly different and probably used goto.
    int displayQuantity = 0;
    if (itemGetType(item) == ITEM_TYPE_AMMO) {
        displayQuantity = ammoGetCapacity(item) * (quantity - 1);
        if (!isDragged) {
            displayQuantity += ammoGetQuantity(item);
        }
    } else {
        if (quantity > 1) {
            displayQuantity = quantity;
            if (isDragged) {
                // Note: can display "x1" during drag, which is otherwise not possible.
                displayQuantity -= 1;
            }
        }
    }

    if (displayQuantity > 0) {
        snprintf(formattedText, sizeof(formattedText), "x%d", std::clamp(displayQuantity, 0, 99999));
        fontDrawText(dest, formattedText, 80, pitch, COLOR_WHITE);
    }

    fontSetCurrent(oldFont);
}

// 0x470650 display_body displays both left and right character portraits
static void _display_body(const FrmId& frmId, int inventoryWindowType)
{
    if (!inventoryHasWindow()) {
        return;
    }

    if (getTicksSince(gInventoryWindowDudeRotationTimestamp) < INVENTORY_NORMAL_WINDOW_PC_ROTATION_DELAY) {
        return;
    }

    // CE: The mobile UI inventory screen turns the character by swipes.
    if (!gInventoryViewActive || !muiIsEnabled()) {
        gInventoryWindowDudeRotation = gInventoryWindowDudeRotation + 1;

        if (gInventoryWindowDudeRotation == ROTATION_COUNT) {
            gInventoryWindowDudeRotation = ROTATION_FIRST;
        }
    }

    Rotation rotations[2];
    if (!frmId.valid()) {
        rotations[0] = gInventoryWindowDudeRotation;
        rotations[1] = ROTATION_SE;
    } else {
        rotations[0] = ROTATION_SW;
        rotations[1] = inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT && hasPartySlots()
            ? ROTATION_SW
            : _target_stack[_target_curr_stack]->rotation;
    }

    const FrmId frmIds[2] = {
        gInventoryWindowDudeFrmId,
        frmId,
    };

    for (int index = 0; index < 2; index += 1) {
        FrmId fid = frmIds[index];
        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT && index == 1) {
            fid = getTargetDisplayFrmId();
        }
        if (!fid.valid()) {
            continue;
        }

        CacheEntry* handle;
        Art* art = artLock(fid, &handle);
        if (art == nullptr) {
            continue;
        }

        int frame = 0;
        if (index == 1) {
            frame = artGetFrameCount(art) - 1;
        }

        Rotation rotation = rotations[index];

        unsigned char* frameData = artGetFrameData(art, frame, rotation);

        int framePitch = artGetWidth(art, frame, rotation);
        int frameWidth = std::min(framePitch, INVENTORY_BODY_VIEW_WIDTH);
        int frameHeight = std::min(artGetHeight(art, frame, rotation), INVENTORY_BODY_VIEW_HEIGHT);

        int win;
        Rect rect;
        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
            unsigned char* windowBuffer = windowGetBuffer(gInventoryBarterBackgroundWindow);
            int windowPitch = windowGetWidth(gInventoryBarterBackgroundWindow);

            if (index == 1) {
                rect.left = 560;
                rect.top = 25;
            } else {
                rect.left = INVENTORY_BARTER_LEFT_BODY_VIEW_X;
                rect.top = INVENTORY_BARTER_LEFT_BODY_VIEW_Y;
            }

            rect.right = rect.left + INVENTORY_BODY_VIEW_WIDTH - 1;
            rect.bottom = rect.top + INVENTORY_BODY_VIEW_HEIGHT - 1;

            FrmImage backgroundFrmImage;
            const InterfaceFrmId backgroundFid = gGameDialogSpeakerIsPartyMember ? InterfaceFrameId::TradeWindow : InterfaceFrameId::BarterWindow;
            if (backgroundFrmImage.lock(backgroundFid)) {
                blitBufferToBuffer(backgroundFrmImage.getData() + rect.top * 640 + rect.left,
                    INVENTORY_BODY_VIEW_WIDTH,
                    INVENTORY_BODY_VIEW_HEIGHT,
                    640,
                    windowBuffer + windowPitch * rect.top + rect.left,
                    windowPitch);
            }

            blitBufferToBufferTrans(frameData, frameWidth, frameHeight, framePitch,
                windowBuffer + windowPitch * (rect.top + (INVENTORY_BODY_VIEW_HEIGHT - frameHeight) / 2) + (INVENTORY_BODY_VIEW_WIDTH - frameWidth) / 2 + rect.left,
                windowPitch);

            displayLootPanePartyName(windowBuffer, windowPitch, rect, index);

            win = gInventoryBarterBackgroundWindow;
        } else {
            unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
            int windowPitch = windowGetWidth(gInventoryWindow);

            FrmImage backgroundFrmImage;
            InterfaceFrameId Fid = InterfaceFrameId::LootContainerWindow;
            int sourceXOffset = 0;

            if (index == 1) {
                if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
                    rect.left = inventoryLootLayout.rightBodyViewX;
                    rect.top = INVENTORY_LOOT_RIGHT_BODY_VIEW_Y;
                } else {
                    rect.left = 297; // inventory data window? ?not used?
                    rect.top = 37;
                    Fid = InterfaceFrameId::InventoryBox;
                    sourceXOffset = 229;
                }
            } else {
                if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
                    rect.left = inventoryLootLayout.leftBodyViewX;
                    rect.top = INVENTORY_LOOT_LEFT_BODY_VIEW_Y;
                } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_USE_ITEM_ON) {
                    rect.left = 176; // Use item cha window
                    rect.top = 37;
                    Fid = InterfaceFrameId::UseItemOnWindow;
                    sourceXOffset = 292;
                } else {
                    rect.left = inventoryLayout.bodyViewX; // inventory cha window
                    rect.top = 37;
                }
            }

            rect.right = rect.left + INVENTORY_BODY_VIEW_WIDTH - 1;
            rect.bottom = rect.top + INVENTORY_BODY_VIEW_HEIGHT - 1;

            Buffer2D dst { windowBuffer, windowPitch, windowGetHeight(gInventoryWindow) };
            ConstBuffer2D background = inventoryGetBackgroundBuffer(inventoryWindowType, Fid, backgroundFrmImage, sourceXOffset);
            if (background) {
                blitBuffer2D(background,
                    rect.left + sourceXOffset,
                    rect.top,
                    INVENTORY_BODY_VIEW_WIDTH,
                    INVENTORY_BODY_VIEW_HEIGHT,
                    dst,
                    rect.left,
                    rect.top);
            }

            blitBufferToBufferTrans(frameData, frameWidth, frameHeight, framePitch,
                windowBuffer + windowPitch * (rect.top + (INVENTORY_BODY_VIEW_HEIGHT - frameHeight) / 2) + (INVENTORY_BODY_VIEW_WIDTH - frameWidth) / 2 + rect.left,
                windowPitch);

            if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT) {
                displayLootPanePartyName(windowBuffer, windowPitch, rect, index);
            }

            win = gInventoryWindow;
        }
        windowRefreshRect(win, &rect);

        artUnlock(handle);
    }

    gInventoryWindowDudeRotationTimestamp = getTicks();
}

// 0x470A2C
static int inventoryCommonInit()
{
    if (inventoryMessageListInit() == -1) {
        return -1;
    }

    _inven_ui_was_disabled = gameUiIsDisabled();

    if (_inven_ui_was_disabled) {
        gameUiEnable();
    }

    gameMouseObjectsHide();

    gameMouseSetCursor(MOUSE_CURSOR_ARROW);

    int index;
    for (index = 0; index < INVENTORY_WINDOW_CURSOR_COUNT; index++) {
        InventoryCursorData* cursorData = &(gInventoryCursorData[index]);

        Art* frm = artLock(kInventoryWindowCursorFrmIds[index], &(cursorData->frmHandle));
        if (frm == nullptr) {
            break;
        }

        cursorData->frm = frm;
        cursorData->frmData = artGetFrameData(frm);
        cursorData->width = artGetWidth(frm);
        cursorData->height = artGetHeight(frm);
        artGetFrameOffsets(frm, 0, ROTATION_NE, &(cursorData->offsetX), &(cursorData->offsetY));
    }

    if (index != INVENTORY_WINDOW_CURSOR_COUNT) {
        for (; index >= 0; index--) {
            artUnlock(gInventoryCursorData[index].frmHandle);
        }

        if (_inven_ui_was_disabled) {
            gameUiDisable(0);
        }

        inventoryMessageListFree();

        return -1;
    }

    _inven_is_initialized = true;
    _im_value = -1;

    return 0;
}

// NOTE: Inlined.
//
// 0x470B8C
static void inventoryCommonFree()
{
    for (int index = 0; index < INVENTORY_WINDOW_CURSOR_COUNT; index++) {
        artUnlock(gInventoryCursorData[index].frmHandle);
    }

    if (_inven_ui_was_disabled) {
        gameUiDisable(0);
    }

    // NOTE: Uninline.
    inventoryMessageListFree();

    _inven_is_initialized = 0;
}

// 0x470BCC
static void inventorySetCursor(int cursor)
{
    // CE: With touch controls the gesture decides what happens to an item
    // (see `inventoryUsesContextMenu`), arrow cursor mode would only change
    // cursor image while dragging items.
    if (touchControlsIsEnabled() && cursor == INVENTORY_WINDOW_CURSOR_ARROW) {
        cursor = INVENTORY_WINDOW_CURSOR_HAND;
    }

    gInventoryCursor = cursor;

    if (cursor != INVENTORY_WINDOW_CURSOR_ARROW || _im_value == -1) {
        InventoryCursorData* cursorData = &(gInventoryCursorData[cursor]);
        mouseSetFrame(cursorData->frmData, cursorData->width, cursorData->height, cursorData->width, cursorData->offsetX, cursorData->offsetY, 0);
    } else {
        inventoryItemSlotOnMouseEnter(-1, _im_value);
    }
}

// 0x470C2C
static void inventoryItemSlotOnMouseEnter(int btn, int keyCode)
{
    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
        int x;
        int y;
        mouseGetPositionInWindow(gInventoryWindow, &x, &y);

        Object* item = nullptr;
        if (_inven_from_button(keyCode, &item, nullptr, nullptr) != 0) {
            gameMouseRenderPrimaryAction(x, y, GAME_MOUSE_ACTION_MENU_ITEM_LOOK, gInventoryWindowMaxX, gInventoryWindowMaxY);

            int cursorHotspotX = 0;
            int cursorHotspotY = 0;
            _gmouse_3d_pick_frame_hot(&cursorHotspotX, &cursorHotspotY);

            InventoryCursorData* cursorData = &(gInventoryCursorData[INVENTORY_WINDOW_CURSOR_PICK]);
            mouseSetFrame(cursorData->frmData, cursorData->width, cursorData->height, cursorData->width, cursorHotspotX, cursorHotspotY, 0);

            if (item != _last_target) {
                objectLookAtFunc(_stack[0], item, gInventoryPrintItemDescriptionHandler);
            }
        } else {
            InventoryCursorData* cursorData = &(gInventoryCursorData[INVENTORY_WINDOW_CURSOR_ARROW]);
            mouseSetFrame(cursorData->frmData, cursorData->width, cursorData->height, cursorData->width, cursorData->offsetX, cursorData->offsetY, 0);
        }

        _last_target = item;
    }

    _im_value = keyCode;
}

// 0x470D1C
static void inventoryItemSlotOnMouseExit(int btn, int keyCode)
{
    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW) {
        InventoryCursorData* cursorData = &(gInventoryCursorData[INVENTORY_WINDOW_CURSOR_ARROW]);
        mouseSetFrame(cursorData->frmData, cursorData->width, cursorData->height, cursorData->width, cursorData->offsetX, cursorData->offsetY, 0);
    }

    _im_value = -1;
}

// 0x470D5C
static void _inven_update_lighting(Object* activeItem)
{
    if (gDude == _inven_dude) {
        int lightDistance;
        if (activeItem != nullptr && activeItem->lightDistance > 4) {
            lightDistance = activeItem->lightDistance;
        } else {
            lightDistance = 4;
        }

        Rect rect;
        objectSetLight(_inven_dude, lightDistance, 0x10000, &rect);
        tileWindowRefreshRect(&rect, gElevation);
    }
}

// 0x470DB8
// CE: With touch controls the gesture decides instead of cursor mode:
// dragging an item moves it, tap and long press open context menu (tap shows
// item description, long press shows actions). When `tapIsHandAction` is set
// (use item on) tap acts like hand cursor too.
static bool inventoryUsesContextMenu(bool tapIsHandAction)
{
    if (touchControlsIsEnabled()) {
        if (mouseIsTouchDrag()) {
            return false;
        }

        if (tapIsHandAction && !mouseIsTouchLongPress()) {
            return false;
        }

        return true;
    }

    return gInventoryCursor == INVENTORY_WINDOW_CURSOR_ARROW;
}

static void _inven_pickup(int buttonCode, int indexOffset)
{
    Object* item;
    Object** itemSlot = nullptr;
    int count = _inven_from_button(buttonCode, &item, &itemSlot, nullptr);
    if (count == 0) {
        return;
    }

    int itemIndex = -1;
    Object* itemInHand = nullptr;
    Rect rect;

    switch (buttonCode) {
    case INVENTORY_HAND_RIGHT_KEY:
        rect.left = inventoryLayout.rightHandSlotX;
        rect.top = INVENTORY_RIGHT_HAND_SLOT_Y;
        if (_inven_dude == gDude && interfaceGetCurrentHand() != HAND_LEFT) {
            itemInHand = item;
        }
        break;
    case INVENTORY_HAND_LEFT_KEY:
        rect.left = inventoryLayout.leftHandSlotX;
        rect.top = INVENTORY_LEFT_HAND_SLOT_Y;
        if (_inven_dude == gDude && interfaceGetCurrentHand() == HAND_LEFT) {
            itemInHand = item;
        }
        break;
    case INVENTORY_ARMOR_KEY:
        rect.left = inventoryLayout.armorSlotX;
        rect.top = INVENTORY_ARMOR_SLOT_Y;
        break;
    default:
        // Normal inventory slot buttons use 1000-based key codes; convert
        // the button code to a zero-based slot index for grid positioning.
        itemIndex = buttonCode - 1000;
        rect.left = inventoryLayout.scrollerX + itemIndex % inventoryLayout.columns * INVENTORY_SLOT_WIDTH;
        rect.top = INVENTORY_SLOT_HEIGHT * (itemIndex / inventoryLayout.columns) + inventoryLayout.scrollerY;
        break;
    }

    bool pickUpFromSlot = itemIndex == -1; // true if item was picked up from armor or weapon slots
    if (inventoryHasWindow() && (pickUpFromSlot || _pud->items[_pud->length - (itemIndex + indexOffset + 1)].quantity <= 1)) {
        // erase background unless item is part of a 2+ quantity stack
        unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
        int width, height;
        if (gInventoryRightHandItem != gInventoryLeftHandItem || item != gInventoryLeftHandItem) {
            if (pickUpFromSlot) {
                height = INVENTORY_LARGE_SLOT_HEIGHT;
                width = INVENTORY_LARGE_SLOT_WIDTH;
            } else {
                height = INVENTORY_SLOT_HEIGHT;
                width = INVENTORY_SLOT_WIDTH;
            }
        } else {
            // seems to wipe both hand slots at once, but I don't know how to trigger this in game
            height = INVENTORY_LARGE_SLOT_HEIGHT;
            width = 180;
            rect.left = inventoryLayout.leftHandSlotX;
            rect.top = INVENTORY_LEFT_HAND_SLOT_Y;
        }
        rect.right = rect.left + width - 1;
        rect.bottom = rect.top + height - 1;
        unsigned char* backgroundData = inventoryFrmImage.getData();
        int backgroundWidth = inventoryFrmImage.getWidth();
        if (backgroundData != nullptr) {
            blitBufferToBuffer(backgroundData + backgroundWidth * rect.top + rect.left,
                width,
                height,
                backgroundWidth,
                windowBuffer + inventoryLayout.windowWidth * rect.top + rect.left,
                inventoryLayout.windowWidth);
        }

        windowRefreshRect(gInventoryWindow, &rect);
    } else {
        _display_inventory(indexOffset, itemIndex, INVENTORY_WINDOW_TYPE_NORMAL);
    }

    if (itemInHand != nullptr) {
        _inven_update_lighting(nullptr);
    }

    // allow ctrl-click to quick unequip or equip item
    bool immediate = _ctrl_pressed();
    _drag_item_loop(item, immediate);

    InventoryDropTarget target;
    Object* targetItem = nullptr;
    bool hasTarget = !immediate && inventoryGetDropTargetAtMouse(indexOffset, &target, &targetItem);

    inventoryPlaceItem(item, itemSlot, itemIndex, count, buttonCode, immediate, hasTarget ? &target : nullptr, targetItem);
    inventoryAfterItemMove(indexOffset);
}

// CE: Drop target under the mouse after dragging an item, [targetItem] - list
// item under it.
static bool inventoryGetDropTargetAtMouse(int indexOffset, InventoryDropTarget* target, Object** targetItem)
{
    *targetItem = nullptr;

    if (mouseHitTestInWindow(gInventoryWindow, inventoryLayout.scrollerX, inventoryLayout.scrollerY, inventoryLayout.scrollerX + inventoryLayout.scrollerWidth, inventoryLayout.scrollerY + inventoryLayout.scrollerHeight)) {
        int x;
        int y;
        mouseGetPositionInWindow(gInventoryWindow, &x, &y);

        int row = (y - inventoryLayout.scrollerY) / INVENTORY_SLOT_HEIGHT;
        int column = (x - inventoryLayout.scrollerX) / INVENTORY_SLOT_WIDTH;
        if (row < 0) {
            row = 0;
        } else if (row >= INVENTORY_ROWS) {
            row = INVENTORY_ROWS - 1;
        }
        if (column < 0) {
            column = 0;
        } else if (column >= inventoryLayout.columns) {
            column = inventoryLayout.columns - 1;
        }

        int targetIndex = row * inventoryLayout.columns + column + indexOffset;
        if (targetIndex < _pud->length) {
            *targetItem = _pud->items[_pud->length - (targetIndex + 1)].item;
        }

        *target = InventoryDropTarget::Backpack;
        return true;
    }

    if (mouseHitTestInWindow(gInventoryWindow, inventoryLayout.leftHandSlotX, INVENTORY_LEFT_HAND_SLOT_Y, inventoryLayout.leftHandSlotX + INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LEFT_HAND_SLOT_MAX_Y)) {
        *target = InventoryDropTarget::LeftHand;
        return true;
    }

    if (mouseHitTestInWindow(gInventoryWindow, inventoryLayout.rightHandSlotX, INVENTORY_RIGHT_HAND_SLOT_Y, inventoryLayout.rightHandSlotX + INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_RIGHT_HAND_SLOT_MAX_Y)) {
        *target = InventoryDropTarget::RightHand;
        return true;
    }

    if (mouseHitTestInWindow(gInventoryWindow, inventoryLayout.armorSlotX, INVENTORY_ARMOR_SLOT_Y, inventoryLayout.armorSlotX + INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_ARMOR_SLOT_MAX_Y)) {
        *target = InventoryDropTarget::Armor;
        return true;
    }

    if (mouseHitTestInWindow(gInventoryWindow, inventoryLayout.bodyViewX, INVENTORY_PC_BODY_VIEW_Y, inventoryLayout.bodyViewX + INVENTORY_BODY_VIEW_WIDTH, INVENTORY_PC_BODY_VIEW_MAX_Y)) {
        *target = InventoryDropTarget::Body;
        return true;
    }

    return false;
}

// CE: Puts [item] picked up from list slot [itemIndex] (-1 - from [itemSlot])
// to [target] (nullptr with [immediate] ctrl-click). Mouse dragging and the
// mobile UI inventory screen share it.
static void inventoryPlaceItem(Object* item, Object** itemSlot, int itemIndex, int count, int buttonCode, bool immediate, const InventoryDropTarget* target, Object* targetItem)
{
    bool pickUpFromSlot = itemIndex == -1; // true if item was picked up from armor or weapon slots
    bool toBackpack = target != nullptr && *target == InventoryDropTarget::Backpack;
    bool toLeftHand = target != nullptr && *target == InventoryDropTarget::LeftHand;
    bool toRightHand = target != nullptr && *target == InventoryDropTarget::RightHand;
    bool toArmor = target != nullptr && *target == InventoryDropTarget::Armor;
    bool toBody = target != nullptr && *target == InventoryDropTarget::Body;

    // drag into inventory list, or ctrl-click from slot
    // CE: Items dragged within the list may be dropped on another item too
    // (the check was limited to items from slots).
    if ((pickUpFromSlot && immediate) || toBackpack) {
        if (!immediate && targetItem != nullptr && targetItem != item) {
            // Dropping item on top of another item.
            if (itemGetType(targetItem) == ITEM_TYPE_CONTAINER) {
                if (_drop_into_container(targetItem, item, itemIndex, itemSlot, count) == 0) {
                    itemIndex = 0;
                }
            } else {
                if (_drop_ammo_into_weapon(targetItem, item, itemSlot, count, buttonCode) == INVENTORY_AMMO_MOVE_RESULT_SUCCESS) {
                    itemIndex = 0;
                }
            }
        }

        if (immediate || itemIndex == -1) {
            if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_MAIN_BACKPACK, item, nullptr)) {
                return;
            }

            // TODO: Holy shit, needs refactoring.
            *itemSlot = nullptr;
            if (itemAdd(_inven_dude, item, 1)) {
                *itemSlot = item;
            } else if (itemSlot == &gInventoryArmor) {
                adjustCritterStatsOnArmorChange(_stack[0], item, nullptr);
            } else if (gInventoryRightHandItem == gInventoryLeftHandItem) {
                gInventoryLeftHandItem = nullptr;
                gInventoryRightHandItem = nullptr;
            }
        }

    } else if (!pickUpFromSlot && immediate && itemGetType(item) == ITEM_TYPE_AMMO) {
        InventoryAmmoMoveResult ammoMoveResult = _drop_ammo_into_weapon(gInventoryLeftHandItem, item, itemSlot, count, buttonCode);
        if (ammoMoveResult == INVENTORY_AMMO_MOVE_RESULT_FAILED) {
            ammoMoveResult = _drop_ammo_into_weapon(gInventoryRightHandItem, item, itemSlot, count, buttonCode);
        }
    } else if (!pickUpFromSlot && immediate && itemGetType(item) != ITEM_TYPE_ARMOR) {
        // ctrl-click non-armor to quick-equip:
        // default to first empty hand, or left hand if both are full
        bool left = gInventoryLeftHandItem == nullptr || gInventoryRightHandItem != nullptr;
        if (left) {
            _switch_hand(item, &gInventoryLeftHandItem, itemSlot, itemIndex);
        } else {
            _switch_hand(item, &gInventoryRightHandItem, itemSlot, itemIndex);
        }

        // drop in left hand slot
    } else if (toLeftHand) {
        if (gInventoryLeftHandItem != nullptr && itemGetType(gInventoryLeftHandItem) == ITEM_TYPE_CONTAINER && gInventoryLeftHandItem != item) {
            _drop_into_container(gInventoryLeftHandItem, item, itemIndex, itemSlot, count);
        } else if (gInventoryLeftHandItem == nullptr) {
            _switch_hand(item, &gInventoryLeftHandItem, itemSlot, itemIndex);
        } else if (_drop_ammo_into_weapon(gInventoryLeftHandItem, item, itemSlot, count, buttonCode) == INVENTORY_AMMO_MOVE_RESULT_FAILED) {
            _switch_hand(item, &gInventoryLeftHandItem, itemSlot, itemIndex);
        }

        // drop in right hand slot
    } else if (toRightHand) {
        if (gInventoryRightHandItem != nullptr && itemGetType(gInventoryRightHandItem) == ITEM_TYPE_CONTAINER && gInventoryRightHandItem != item) {
            _drop_into_container(gInventoryRightHandItem, item, itemIndex, itemSlot, count);
        } else if (gInventoryRightHandItem == nullptr) {
            _switch_hand(item, &gInventoryRightHandItem, itemSlot, itemIndex);
        } else if (_drop_ammo_into_weapon(gInventoryRightHandItem, item, itemSlot, count, buttonCode) == INVENTORY_AMMO_MOVE_RESULT_FAILED) {
            _switch_hand(item, &gInventoryRightHandItem, itemSlot, itemIndex);
        }

    } else if ((immediate && itemGetType(item) == ITEM_TYPE_ARMOR) || toArmor) {
        if (itemGetType(item) == ITEM_TYPE_ARMOR) {
            Object* currentArmor = gInventoryArmor;
            if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_ARMOR_SLOT, item, currentArmor)) {
                return;
            }

            int itemAddResult = 0;
            if (itemIndex != -1) {
                itemRemoveWithReason(_inven_dude, item, 1, RemoveInventoryObjectHookReason::EquipArmor);
            }

            if (gInventoryArmor != nullptr) {
                if (itemSlot != nullptr) {
                    *itemSlot = gInventoryArmor;
                } else {
                    gInventoryArmor = nullptr;
                    itemAddResult = itemAdd(_inven_dude, currentArmor, 1);
                }
            } else {
                if (itemSlot != nullptr) {
                    *itemSlot = gInventoryArmor;
                }
            }

            if (itemAddResult != 0) {
                gInventoryArmor = currentArmor;
                if (itemIndex != -1) {
                    itemAdd(_inven_dude, item, 1);
                }
            } else {
                adjustCritterStatsOnArmorChange(_stack[0], currentArmor, item);
                gInventoryArmor = item;
            }
        }
    } else if (toBody) {
        if (_curr_stack == 0) {
            // Call the hook when dropping item on the PC portrait when not in a container.  Return value is irrelevant.
            scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_CHARACTER_PORTRAIT, item, nullptr);
        } else {
            // If we are looking inside nested inventory (such as backpack item), we see this item in the PC Body View instead of the player.
            // So we drop item into it.
            _drop_into_container(_stack[_curr_stack - 1], item, itemIndex, itemSlot, count);
        }
    }
}

// CE: Redraws the inventory window after an item moved.
static void inventoryAfterItemMove(int indexOffset)
{
    _adjust_fid();
    inventoryRenderSummary();
    _display_inventory(indexOffset, -1, INVENTORY_WINDOW_TYPE_NORMAL);
    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
    if (_inven_dude == gDude) {
        Object* item;
        if (interfaceGetCurrentHand() == HAND_LEFT) {
            item = critterGetItem1(_inven_dude);
        } else {
            item = critterGetItem2(_inven_dude);
        }

        if (item != nullptr) {
            _inven_update_lighting(item);
        }
    }
}

// 0x4714E0
static void _switch_hand(Object* sourceItem, Object** targetSlot, Object** sourceSlot, int itemIndex)
{
    if (*targetSlot != nullptr) {
        if (itemGetType(*targetSlot) == ITEM_TYPE_WEAPON && itemGetType(sourceItem) == ITEM_TYPE_AMMO) {
            return;
        }
    }

    HookInventoryMoveType targetSlotType = targetSlot == &gInventoryLeftHandItem
        ? HOOK_INVENTORYMOVE_LEFT_HAND
        : HOOK_INVENTORYMOVE_RIGHT_HAND;
    if (!scriptHooks_InventoryMove(targetSlotType, sourceItem, *targetSlot)) {
        return;
    }

    if (*targetSlot != nullptr) {
        if (sourceSlot != nullptr && (sourceSlot != &gInventoryArmor || itemGetType(*targetSlot) == ITEM_TYPE_ARMOR)) {
            if (sourceSlot == &gInventoryArmor) {
                adjustCritterStatsOnArmorChange(_stack[0], gInventoryArmor, *targetSlot);
            }
            *sourceSlot = *targetSlot;
        } else {
            if (itemIndex != -1) {
                itemRemoveWithReason(_inven_dude, sourceItem, 1, RemoveInventoryObjectHookReason::EquipWeapon);
            }

            Object* existingItem = *targetSlot;
            *targetSlot = nullptr;
            if (itemAdd(_inven_dude, existingItem, 1) != 0) {
                itemAdd(_inven_dude, sourceItem, 1);
                return;
            }

            itemIndex = -1;

            if (sourceSlot != nullptr) {
                if (sourceSlot == &gInventoryArmor) {
                    adjustCritterStatsOnArmorChange(_stack[0], gInventoryArmor, nullptr);
                }
                *sourceSlot = nullptr;
            }
        }
    } else {
        if (sourceSlot != nullptr) {
            if (sourceSlot == &gInventoryArmor) {
                adjustCritterStatsOnArmorChange(_stack[0], gInventoryArmor, nullptr);
            }
            *sourceSlot = nullptr;
        }
    }

    *targetSlot = sourceItem;

    if (itemIndex != -1) {
        itemRemoveWithReason(_inven_dude, sourceItem, 1, RemoveInventoryObjectHookReason::EquipWeapon);
    }
}

// This function removes armor bonuses and effects granted by [oldArmor] and
// adds appropriate bonuses and effects granted by [newArmor]. Both [oldArmor]
// and [newArmor] can be NULL.
//
// 0x4715F8
void adjustCritterStatsOnArmorChange(Object* critter, Object* oldArmor, Object* newArmor)
{
    int armorClassBonus = critterGetBonusStat(critter, STAT_ARMOR_CLASS);
    int oldArmorClass = armorGetArmorClass(oldArmor);
    int newArmorClass = armorGetArmorClass(newArmor);
    critterSetBonusStat(critter, STAT_ARMOR_CLASS, armorClassBonus - oldArmorClass + newArmorClass);

    Stat damageResistanceStat = STAT_DAMAGE_RESISTANCE;
    Stat damageThresholdStat = STAT_DAMAGE_THRESHOLD;
    for (DamageType damageType = DAMAGE_TYPE_FIRST; damageType < DAMAGE_TYPE_COUNT; damageType++) {
        int damageResistanceBonus = critterGetBonusStat(critter, damageResistanceStat);
        int oldArmorDamageResistance = armorGetDamageResistance(oldArmor, damageType);
        int newArmorDamageResistance = armorGetDamageResistance(newArmor, damageType);
        critterSetBonusStat(critter, damageResistanceStat, damageResistanceBonus - oldArmorDamageResistance + newArmorDamageResistance);

        int damageThresholdBonus = critterGetBonusStat(critter, damageThresholdStat);
        int oldArmorDamageThreshold = armorGetDamageThreshold(oldArmor, damageType);
        int newArmorDamageThreshold = armorGetDamageThreshold(newArmor, damageType);
        critterSetBonusStat(critter, damageThresholdStat, damageThresholdBonus - oldArmorDamageThreshold + newArmorDamageThreshold);

        damageResistanceStat++;
        damageThresholdStat++;
    }

    if (objectIsPartyMember(critter)) {
        if (oldArmor != nullptr) {
            Perk perk = armorGetPerk(oldArmor);
            perkRemoveEffect(critter, perk);
        }

        if (newArmor != nullptr) {
            Perk perk = armorGetPerk(newArmor);
            perkAddEffect(critter, perk);
        }
    }
}

// 0x4716E8 adjust_fid
static void _adjust_fid()
{
    const FrmId frmId = inventoryComputeCritterFrmId(_inven_dude,
        inventoryProtoId,
        gInventoryRightHandItem,
        gInventoryLeftHandItem,
        gInventoryArmor,
        interfaceGetCurrentHand(),
        ANIM_STAND,
        ROTATION_NE);
    // SFALL: HOOK_ADJUSTFID's first argument without Hero Appearance's art.
    gInventoryWindowDudeFrmId = scriptHooks_AdjustFid(artNormalFrmId(frmId), frmId);

    // CE: The mobile UI shows the critter inside open containers too.
    if (gInventoryViewActive) {
        if (_inven_dude == _stack[0]) {
            gInventoryCritterFrmId = gInventoryWindowDudeFrmId;
        } else {
            const FrmId critterFrmId = inventoryComputeCritterFrmId(_stack[0],
                inventoryProtoId,
                gInventoryRightHandItem,
                gInventoryLeftHandItem,
                gInventoryArmor,
                interfaceGetCurrentHand(),
                ANIM_STAND,
                ROTATION_NE);
            gInventoryCritterFrmId = scriptHooks_AdjustFid(artNormalFrmId(critterFrmId), critterFrmId);
        }
    }
}

// 0x4717E4 use_inventory_on
// Dude uses [item] on [targetObj] (2 AP in combat).
static void inventoryUseItemOnObject(Object* targetObj, Object* item)
{
    if (isInCombat()) {
        if (gDude->data.critter.combat.ap >= 2) {
            if (_action_use_an_item_on_object(gDude, targetObj, item) != -1) {
                int actionPoints = gDude->data.critter.combat.ap;
                if (actionPoints < 2) {
                    gDude->data.critter.combat.ap = 0;
                } else {
                    gDude->data.critter.combat.ap = actionPoints - 2;
                }
                interfaceRenderActionPoints(gDude->data.critter.combat.ap, _combat_free_move);
            }
        }
    } else {
        _action_use_an_item_on_object(gDude, targetObj, item);
    }
}

void inventoryOpenUseItemOn(Object* targetObj)
{
    // CE: The mobile UI shows dude's items in a panel next to the target.
    if (muiIsEnabled()) {
        ScopedGameMode gm(GameMode::kUseOn);
        Object* item = muiChooseItemToUse(targetObj);
        if (item != nullptr) {
            inventoryUseItemOnObject(targetObj, item);
            // fix for click through bug
            gBlockMouseUpEvent = true;
        }
        return;
    }

    if (inventoryCommonInit() == -1) {
        return;
    }

    bool isoWasEnabled = _setup_inventory(INVENTORY_WINDOW_TYPE_USE_ITEM_ON);
    _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_USE_ITEM_ON);
    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
    ScopedGameMode gm(GameMode::kUseOn);
    for (;;) {
        sharedFpsLimiter.mark();

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        _display_body(FrmId::Empty(), INVENTORY_WINDOW_TYPE_USE_ITEM_ON);

        int keyCode = inputGetInput();
        MouseButtonEvent mouseEvent = mouseGetEvent();
        InventoryScrollerDisplayContext inventoryScrollerContext { INVENTORY_WINDOW_TYPE_USE_ITEM_ON, nullptr };
        InventoryScroller useItemOnScroller {
            { INVENTORY_SCROLLER_X, INVENTORY_SCROLLER_Y, INVENTORY_SCROLLER_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_SCROLLER_Y },
            &(_stack_offset[_curr_stack]),
            1,
            gInventorySlotsCount,
            _pud->length,
            KEY_ARROW_UP,
            KEY_ARROW_DOWN,
            KEY_PAGE_UP,
            KEY_PAGE_DOWN,
            KEY_HOME,
            KEY_END,
            inventoryScrollerRedrawInventory,
            &inventoryScrollerContext,
            false,
        };
        switch (keyCode) {
        case 2500:
            _container_exit(keyCode, INVENTORY_WINDOW_TYPE_USE_ITEM_ON);
            break;
        default:
            inventoryScrollerHandleInput(useItemOnScroller, keyCode, mouseEvent);

            if ((mouseEvent & MouseButtonEvent::RightButtonDown) != MouseButtonEvent::None) {
                if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_HAND) {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
                } else {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
                }
            } else if ((mouseEvent & MouseButtonEvent::LeftButtonDown) != MouseButtonEvent::None) {
                if (keyCode >= 1000 && keyCode < 1000 + gInventorySlotsCount) {
                    if (inventoryUsesContextMenu(true)) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_USE_ITEM_ON);
                    } else {
                        int inventoryItemIndex = _pud->length - (_stack_offset[_curr_stack] + keyCode - 1000 + 1);
                        // SFALL: Fix crash when clicking on empty space in the inventory list
                        // opened by "Use Inventory Item On" (backpack) action icon
                        if (inventoryItemIndex < _pud->length && inventoryItemIndex >= 0) {
                            InventoryItem* inventoryItem = &(_pud->items[inventoryItemIndex]);
                            inventoryUseItemOnObject(targetObj, inventoryItem->item);
                            // fix for click through bug
                            gBlockMouseUpEvent = true;
                            keyCode = KEY_ESCAPE;
                        } else {
                            keyCode = -1;
                        }
                    }
                }
            }
        }

        if (keyCode == KEY_ESCAPE) {
            break;
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    _exit_inventory(isoWasEnabled);

    // NOTE: Uninline.
    inventoryCommonFree();
}

// 0x471B70
Object* critterGetItem2(Object* critter)
{
    int i;
    Inventory* inventory;
    Object* item;

    if (gInventoryRightHandItem != nullptr && critter == _inven_dude) {
        return gInventoryRightHandItem;
    }

    inventory = &(critter->data.inventory);
    for (i = 0; i < inventory->length; i++) {
        item = inventory->items[i].item;
        if (item->flags & OBJECT_IN_RIGHT_HAND) {
            return item;
        }
    }

    return nullptr;
}

// 0x471BBC
Object* critterGetItem1(Object* critter)
{
    int i;
    Inventory* inventory;
    Object* item;

    if (gInventoryLeftHandItem != nullptr && critter == _inven_dude) {
        return gInventoryLeftHandItem;
    }

    inventory = &(critter->data.inventory);
    for (i = 0; i < inventory->length; i++) {
        item = inventory->items[i].item;
        if (item->flags & OBJECT_IN_LEFT_HAND) {
            return item;
        }
    }

    return nullptr;
}

// 0x471C08
Object* critterGetArmor(Object* critter)
{
    int i;
    Inventory* inventory;
    Object* item;

    if (gInventoryArmor != nullptr && critter == _inven_dude) {
        return gInventoryArmor;
    }

    inventory = &(critter->data.inventory);
    for (i = 0; i < inventory->length; i++) {
        item = inventory->items[i].item;
        if (item->flags & OBJECT_WORN) {
            return item;
        }
    }

    return nullptr;
}

// Critter inventories are displayed without equipped items in almost all cases.  This is accomplished by
// temporarily removing them when inventory is viewed.
CritterEquipped critterStripEquipped(Object* critter)
{
    CritterEquipped equipped;
    Inventory* inv = &critter->data.inventory;
    for (int i = 0; i < inv->length; i++) {
        Object* item = inv->items[i].item;
        if ((item->flags & OBJECT_IN_LEFT_HAND) != OBJECT_NONE) {
            if ((item->flags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
                equipped.rightHand = item;
            }
            equipped.leftHand = item;
        } else if ((item->flags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
            equipped.rightHand = item;
        } else if ((item->flags & OBJECT_WORN) != OBJECT_NONE) {
            equipped.armor = item;
        }
    }
    if (equipped.leftHand != nullptr) {
        itemRemoveWithReason(critter, equipped.leftHand, 1, RemoveInventoryObjectHookReason::LeftHandEquipped);
    }
    if (equipped.rightHand != nullptr && equipped.rightHand != equipped.leftHand) {
        itemRemoveWithReason(critter, equipped.rightHand, 1, RemoveInventoryObjectHookReason::RightHandEquipped);
    }
    if (equipped.armor != nullptr) {
        itemRemoveWithReason(critter, equipped.armor, 1, RemoveInventoryObjectHookReason::ArmorEquipped);
    }
    return equipped;
}

void critterRestoreEquipped(Object* critter, CritterEquipped& equipped)
{
    if (equipped.leftHand != nullptr) {
        equipped.leftHand->flags |= OBJECT_IN_LEFT_HAND;
        if (equipped.leftHand == equipped.rightHand) equipped.leftHand->flags |= OBJECT_IN_RIGHT_HAND;
        itemAdd(critter, equipped.leftHand, 1);
    }
    if (equipped.rightHand != nullptr && equipped.rightHand != equipped.leftHand) {
        equipped.rightHand->flags |= OBJECT_IN_RIGHT_HAND;
        itemAdd(critter, equipped.rightHand, 1);
    }
    if (equipped.armor != nullptr) {
        equipped.armor->flags |= OBJECT_WORN;
        itemAdd(critter, equipped.armor, 1);
    }
    equipped = {};
}

static void inventoryStripEquippedToGlobals(Object* critter)
{
    CritterEquipped equipped = critterStripEquipped(critter);
    gInventoryLeftHandItem = equipped.leftHand;
    gInventoryRightHandItem = equipped.rightHand;
    gInventoryArmor = equipped.armor;
}

static void inventoryRestoreEquippedFromGlobals(Object* critter)
{
    CritterEquipped equipped { gInventoryLeftHandItem, gInventoryRightHandItem, gInventoryArmor };
    critterRestoreEquipped(critter, equipped);
    gInventoryLeftHandItem = nullptr;
    gInventoryRightHandItem = nullptr;
    gInventoryArmor = nullptr;
}

// CE: `[qol] party_loot_and_barter` offers the party members whose items the
// player gets at in the original game anyway (by bartering or by stealing,
// which always succeeds on a party member): those that barter and may be
// stolen from, as Inventory Filter's party switching. Not the animals and
// robots (no bartering, stealing forbidden), nor the two followers who don't
// barter and turn hostile when caught stealing (Karl, Jonny: the
// always-successful steal is a loophole there).
static bool partyMemberTradesItems(Object* critter)
{
    return critterFlagCheck(critter, CRITTER_BARTER) && !critterFlagCheck(critter, CRITTER_NO_STEAL);
}

// Party members further away are out of reach (Inventory Filter's range; in
// the original the player walks up to them to barter).
static constexpr int kPartyMemberReachTiles = 30;

bool inventoryPartyMemberIsReachable(Object* critter)
{
    if (critter == nullptr || critter == gDude) {
        return critter != nullptr;
    }

    return critter->elevation == gDude->elevation
        && tileDistanceBetween(gDude->tile, critter->tile) <= kPartyMemberReachTiles;
}

// The next reachable party member from [index] in [direction] (the window's
// arrows), [index] itself when there is none.
static int partyTargetStep(const std::vector<Object*>& partyTargets, int index, int direction)
{
    int count = static_cast<int>(partyTargets.size());
    for (int next = index, step = 0; step < count; step++) {
        next = (next + direction + count) % count;
        if (next == index || inventoryPartyMemberIsReachable(partyTargets[next])) {
            return next;
        }
    }
    return index;
}

static void inventorySetLeftPaneCritter(Object* critter, Object* target, int inventoryWindowType)
{
    assert(critter != nullptr);
    assert(target != nullptr);

    // _inven_dude can point at a nested container while browsing bags, but
    // equipped items always need to be restored onto the owning critter.
    inventoryRestoreEquippedFromGlobals(_stack[0]);
    inventoryStripEquippedToGlobals(critter);
    _inven_dude = critter;
    _curr_stack = 0;
    _pud = &(critter->data.inventory);
    _stack[0] = critter;
    _stack_offset[0] = 0;

    WeaponAnimation animationCode = WeaponAnimation::None;
    Object* itemInHand = interfaceGetCurrentHand() == HAND_RIGHT ? gInventoryRightHandItem : gInventoryLeftHandItem;
    if (itemInHand != nullptr) {
        Proto* proto = nullptr;
        if (protoGetProto(itemInHand, &proto) != -1
            && proto != nullptr
            && proto->item.type == ITEM_TYPE_WEAPON) {
            animationCode = proto->item.data.weapon.animationCode;
        }
    }

    gInventoryWindowDudeFrmId = FrmId(critter, ANIM_STAND, animationCode, ROTATION_NE);
    gInventoryWindowDudeRotationTimestamp = 0;
    _display_inventory(0, -1, inventoryWindowType);
    _display_body(FrmId(target), inventoryWindowType);
}

// 0x471CA0
Object* objectGetCarriedObjectByProtoId(Object* obj, const ProtoId& protoId)
{
    Inventory* inventory = &(obj->data.inventory);

    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);
        if (ProtoId(inventoryItem->item) == protoId) {
            return inventoryItem->item;
        }

        Object* found = objectGetCarriedObjectByProtoId(inventoryItem->item, protoId);
        if (found != nullptr) {
            return found;
        }
    }

    return nullptr;
}

// 0x471CDC
int objectGetCarriedQuantityByProtoId(Object* object, const ProtoId& protoId)
{
    int quantity = 0;

    Inventory* inventory = &(object->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);
        if (ProtoId(inventoryItem->item) == protoId) {
            quantity += inventoryItem->quantity;
        }

        quantity += objectGetCarriedQuantityByProtoId(inventoryItem->item, protoId);
    }

    return quantity;
}

// Renders character's summary of SPECIAL stats, equipped armor bonuses,
// and weapon's damage/range.
//
// CE: Summary texts are built separately from drawing, the mobile UI shows
// the same texts.
static void inventoryBuildSummary(InventorySummary* summary)
{
    char formattedText[80];
    MessageListItem messageListItem;

    summary->name = critterGetName(_stack[0]);

    for (Stat stat = STAT_FIRST; stat < PRIMARY_STAT_COUNT; stat++) {
        messageListItem.num = stat;
        summary->stats[stat].label = messageListGetItem(&gInventoryMessageList, &messageListItem) ? messageListItem.text : "";

        int value = critterGetStat(_stack[0], stat);
        snprintf(formattedText, sizeof(formattedText), "%d", value);
        summary->stats[stat].value = formattedText;
    }

    for (int index = 0; index < 7; index += 1) {
        messageListItem.num = 7 + index;
        summary->defense[index].label = messageListGetItem(&gInventoryMessageList, &messageListItem) ? messageListItem.text : "";

        if (gSummaryStats2[index] == STAT_INVALID) {
            int value = critterGetStat(_stack[0], gSummaryStats[index]);
            snprintf(formattedText, sizeof(formattedText), "   %d", value);
        } else {
            int value1 = critterGetStat(_stack[0], gSummaryStats[index]);
            int value2 = critterGetStat(_stack[0], gSummaryStats2[index]);
            const char* format = index != 0 ? "%d/%d%%" : "%d/%d";
            snprintf(formattedText, sizeof(formattedText), format, value1, value2);
        }
        summary->defense[index].value = formattedText;
    }

    Object* itemsInHands[2] = {
        gInventoryLeftHandItem,
        gInventoryRightHandItem,
    };

    const HitMode hitModes[2] = {
        HIT_MODE_LEFT_WEAPON_PRIMARY,
        HIT_MODE_RIGHT_WEAPON_PRIMARY,
    };

    const HitMode secondaryHitModes[2] = {
        HIT_MODE_LEFT_WEAPON_SECONDARY,
        HIT_MODE_RIGHT_WEAPON_SECONDARY,
    };

    const HitMode unarmedHitModes[2] = {
        HIT_MODE_PUNCH,
        HIT_MODE_KICK,
    };

    for (int index = 0; index < 2; index += 1) {
        std::string* lines = summary->hands[index];
        std::vector<InventoryTextPart>* parts = summary->handParts[index];
        for (int line = 0; line < 3; line++) {
            lines[line].clear();
            parts[line].clear();
        }

        Object* item = itemsInHands[index];
        if (item == nullptr) {
            formattedText[0] = '\0';

            // No item
            messageListItem.num = 14;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                lines[0] = messageListItem.text;
                parts[0] = { { lines[0], false } };
            }

            // Unarmed dmg:
            messageListItem.num = 24;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                // SFALL: Display the actual damage values of unarmed attacks.
                // CE: Implementation is different.
                HitMode hitMode = unarmedHitModes[index];
                if (_stack[0] == gDude) {
                    InterfaceItemAction actions[2];
                    interfaceGetItemActions(&(actions[0]), &(actions[1]));

                    bool isSecondary = actions[index] == INTERFACE_ITEM_ACTION_SECONDARY
                        || actions[index] == INTERFACE_ITEM_ACTION_SECONDARY_AIMING;

                    if (index == HAND_LEFT) {
                        hitMode = unarmedGetPunchHitMode(isSecondary);
                    } else {
                        hitMode = unarmedGetKickHitMode(isSecondary);
                    }
                }

                // Formula is the same as in `weaponGetDamage`.
                int minDamage;
                int maxDamage;
                int bonusDamage = unarmedGetDamage(hitMode, &minDamage, &maxDamage);
                int meleeDamage = critterGetStat(_stack[0], STAT_MELEE_DAMAGE);
                // TODO: Localize unarmed attack names.
                snprintf(formattedText, sizeof(formattedText), "%s %d-%d",
                    messageListItem.text,
                    bonusDamage + minDamage,
                    bonusDamage + meleeDamage + maxDamage);
                parts[1] = {
                    { messageListItem.text, true },
                    { std::to_string(bonusDamage + minDamage) + "-" + std::to_string(bonusDamage + meleeDamage + maxDamage), false },
                };
            }

            lines[1] = formattedText;
            continue;
        }

        lines[0] = itemGetName(item);
        parts[0] = { { lines[0], false } };

        ItemType itemType = itemGetType(item);
        if (itemType != ITEM_TYPE_WEAPON) {
            if (itemType == ITEM_TYPE_ARMOR) {
                // (Not worn)
                messageListItem.num = 18;
                if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                    lines[1] = messageListItem.text;
                    parts[1] = { { lines[1], true } };
                }
            }
            continue;
        }

        // SFALL: Fix displaying secondary mode weapon range.
        HitMode hitMode = hitModes[index];
        if (_stack[0] == gDude) {
            InterfaceItemAction actions[2];
            interfaceGetItemActions(&(actions[0]), &(actions[1]));

            bool isSecondary = actions[index] == INTERFACE_ITEM_ACTION_SECONDARY
                || actions[index] == INTERFACE_ITEM_ACTION_SECONDARY_AIMING;

            if (isSecondary) {
                hitMode = secondaryHitModes[index];
            }
        }

        inventoryWeaponParts(&gInventoryMessageList, _stack[0], item, hitMode, &lines[1], &parts[1], &lines[2], &parts[2]);
    }

    // Total wt:
    summary->weight.clear();
    summary->encumbered = false;
    messageListItem.num = 20;
    if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
        int inventoryWeight = inventoryGetCritterWeight();
        if (ProtoId(_stack[0]).objectType() == OBJ_TYPE_CRITTER) {
            int carryWeight = critterGetStat(_stack[0], STAT_CARRY_WEIGHT);
            snprintf(formattedText, sizeof(formattedText), "%s %d/%d", messageListItem.text, inventoryWeight, carryWeight);
            summary->encumbered = critterIsEncumbered(_stack[0]);
        } else {
            snprintf(formattedText, sizeof(formattedText), "%s %d", messageListItem.text, inventoryWeight);
        }
        summary->weight = formattedText;
    }
}

// Weapon damage (and range for ranged attacks) with [hitMode] as the summary
// shows it: "Dmg: 5-12   Rng: 25" (inventry.msg). Returns range, or -1 when
// the text has no range.
static int inventoryFormatWeaponDamage(MessageList* messages, Object* critter, Object* item, HitMode hitMode, char* buffer, size_t size, std::string* damageOnly)
{
    buffer[0] = '\0';

    int range = weaponGetRange(critter, hitMode);

    int damageMin;
    int damageMax;
    weaponGetDamageMinMax(item, &damageMin, &damageMax);

    // CE: Fix displaying secondary mode weapon damage (affects throwable
    // melee weapons - knifes, spears, etc.).
    AttackType attackType = weaponGetAttackTypeForHitMode(item, hitMode);

    int meleeDamage;
    if (attackType == ATTACK_TYPE_MELEE || attackType == ATTACK_TYPE_UNARMED) {
        meleeDamage = critterGetStat(critter, STAT_MELEE_DAMAGE);

        // SFALL: Display melee damage without "Bonus HtH Damage" bonus.
        if (damageModGetBonusHthDamageFix() && !damageModGetDisplayBonusDamage()) {
            meleeDamage -= 2 * perkGetRank(gDude, PERK_BONUS_HTH_DAMAGE);
        }
    } else {
        meleeDamage = 0;
    }

    int shownRange = -1;
    MessageListItem messageListItem;
    messageListItem.num = 15; // Dmg:
    if (messageListGetItem(messages, &messageListItem)) {
        if (attackType != 4 && range <= 1) {
            // SFALL: Display bonus damage.
            if (damageModGetBonusHthDamageFix() && damageModGetDisplayBonusDamage()) {
                // CE: Just in case check for attack type, however it looks
                // like we cannot be here with anything besides melee or
                // unarmed.
                if (critter == gDude && (attackType == ATTACK_TYPE_MELEE || attackType == ATTACK_TYPE_UNARMED)) {
                    // See explanation in `weaponGetDamage`.
                    damageMin += 2 * perkGetRank(gDude, PERK_BONUS_HTH_DAMAGE);
                }
            }
            snprintf(buffer, size, "%s %d-%d", messageListItem.text, damageMin, damageMax + meleeDamage);
        } else {
            MessageListItem rangeMessageListItem;
            rangeMessageListItem.num = 16; // Rng:
            if (messageListGetItem(messages, &rangeMessageListItem)) {
                // SFALL: Display bonus damage.
                if (damageModGetDisplayBonusDamage()) {
                    // CE: There is a bug in Sfall diplaying wrong damage
                    // bonus for melee weapons with range > 1 (spears,
                    // sledgehammers) and throwables (secondary mode).
                    if (critter == gDude && attackType == ATTACK_TYPE_RANGED) {
                        int damageBonus = 2 * perkGetRank(gDude, PERK_BONUS_RANGED_DAMAGE);
                        damageMin += damageBonus;
                        damageMax += damageBonus;
                    }
                }

                snprintf(buffer, size, "%s %d-%d   %s %d", messageListItem.text, damageMin, damageMax + meleeDamage, rangeMessageListItem.text, range);
                shownRange = range;
            }
        }
    }

    if (damageOnly != nullptr) {
        char text[32];
        snprintf(text, sizeof(text), "%d-%d", damageMin, damageMax + meleeDamage);
        *damageOnly = text;
    }

    return shownRange;
}

// The summary's lines of a weapon held by [critter] with [hitMode]: damage
// (and range), ammo (none without ammo) - as text and as labels and
// values.
static void inventoryWeaponParts(MessageList* messages, Object* critter, Object* item, HitMode hitMode, std::string* damageLine, std::vector<InventoryTextPart>* damage, std::string* ammoLine, std::vector<InventoryTextPart>* ammo)
{
    char formattedText[80];
    std::string damageText;
    int range = inventoryFormatWeaponDamage(messages, critter, item, hitMode, formattedText, sizeof(formattedText), &damageText);
    *damageLine = formattedText;
    damage->clear();
    MessageListItem messageListItem;
    if (!damageLine->empty()) {
        messageListItem.num = 15; // Dmg:
        if (messageListGetItem(messages, &messageListItem)) {
            *damage = { { messageListItem.text, true }, { damageText, false } };
        }
        messageListItem.num = 16; // Rng:
        if (range != -1 && messageListGetItem(messages, &messageListItem)) {
            damage->push_back({ messageListItem.text, true });
            damage->push_back({ std::to_string(range), false });
        }
    }

    ammoLine->clear();
    ammo->clear();
    if (ammoGetCapacity(item) > 0) {
        const ProtoId ammoTypeProtoId = weaponGetAmmoTypeProtoId(item);
        int capacity = ammoGetCapacity(item);
        int quantity = ammoGetQuantity(item);
        std::string count = std::to_string(quantity) + "/" + std::to_string(capacity);

        messageListItem.num = 17; // Ammo:
        if (messageListGetItem(messages, &messageListItem)) {
            if (ammoTypeProtoId.valid()) {
                std::string value = quantity != 0 ? count + " " + protoGetName(ammoTypeProtoId) : count;
                *ammoLine = std::string(messageListItem.text) + " " + value;
                *ammo = { { messageListItem.text, true }, { value, false } };
            }
        } else {
            *ammoLine = count;
            *ammo = { { count, false } };
        }
    }
}

static MessageList* inventoryLabelMessageList()
{
    if (!gInventoryLabelMessageListLoaded) {
        char path[COMPAT_MAX_PATH];
        snprintf(path, sizeof(path), "%s%s", gGameMsgPath, "inventry.msg");
        if (!messageListInit(&gInventoryLabelMessageList) || !messageListLoad(&gInventoryLabelMessageList, path)) {
            return nullptr;
        }
        gInventoryLabelMessageListLoaded = true;
    }
    return &gInventoryLabelMessageList;
}

void inventoryGetWeaponParts(Object* critter, Object* item, std::vector<InventoryTextPart>* damage, std::vector<InventoryTextPart>* ammo)
{
    damage->clear();
    ammo->clear();
    MessageList* messages = inventoryLabelMessageList();
    if (messages == nullptr || item == nullptr || itemGetType(item) != ITEM_TYPE_WEAPON) {
        return;
    }

    // As if the critter held the weapon (perks and traits apply), game
    // functions take it from the hand.
    Object* savedDude = _inven_dude;
    Object* savedRightHand = gInventoryRightHandItem;
    _inven_dude = critter;
    gInventoryRightHandItem = item;

    std::string damageLine;
    std::string ammoLine;
    inventoryWeaponParts(messages, critter, item, HIT_MODE_RIGHT_WEAPON_PRIMARY, &damageLine, damage, &ammoLine, ammo);

    gInventoryRightHandItem = savedRightHand;
    _inven_dude = savedDude;
}

void inventoryGetArmorParts(Object* item, std::vector<InventoryTextPart>* parts)
{
    parts->clear();
    MessageList* messages = inventoryLabelMessageList();
    if (messages == nullptr || item == nullptr || itemGetType(item) != ITEM_TYPE_ARMOR) {
        return;
    }

    MessageListItem messageListItem;
    messageListItem.num = 8; // Armor class
    if (messageListGetItem(messages, &messageListItem)) {
        *parts = { { messageListItem.text, true }, { std::to_string(armorGetArmorClass(item)), false } };
    }
}

// 0x471D5C display_stats
static void inventoryRenderSummary()
{
    if (!inventoryHasWindow()) {
        return;
    }

    InventorySummary summary;
    inventoryBuildSummary(&summary);

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    int pitch = inventoryLayout.windowWidth;
    int summaryX = inventoryLayout.summaryX;
    int summaryMaxX = summaryX + (INVENTORY_SUMMARY_MAX_X - INVENTORY_SUMMARY_X);

    unsigned char* backgroundData = inventoryFrmImage.getData();
    int backgroundWidth = inventoryFrmImage.getWidth();
    if (backgroundData != nullptr) {
        blitBufferToBuffer(backgroundData + backgroundWidth * INVENTORY_SUMMARY_Y + summaryX,
            INVENTORY_SUMMARY_WIDTH,
            INVENTORY_SUMMARY_HEIGHT,
            backgroundWidth,
            windowBuffer + pitch * INVENTORY_SUMMARY_Y + summaryX,
            pitch);
    }

    // Render character name.
    fontDrawText(windowBuffer + pitch * INVENTORY_SUMMARY_Y + summaryX, summary.name.c_str(), 80, pitch, COLOR_GREEN);

    bufferDrawLine(windowBuffer,
        pitch,
        summaryX,
        3 * fontGetLineHeight() / 2 + INVENTORY_SUMMARY_Y,
        summaryMaxX,
        3 * fontGetLineHeight() / 2 + INVENTORY_SUMMARY_Y,
        COLOR_GREEN);

    int offset = pitch * 2 * fontGetLineHeight() + pitch * INVENTORY_SUMMARY_Y + summaryX;
    for (int index = 0; index < PRIMARY_STAT_COUNT; index++) {
        fontDrawText(windowBuffer + offset, summary.stats[index].label.c_str(), 80, pitch, COLOR_GREEN);
        fontDrawText(windowBuffer + offset + 24, summary.stats[index].value.c_str(), 80, pitch, COLOR_GREEN);
        offset += pitch * fontGetLineHeight();
    }

    offset -= pitch * 7 * fontGetLineHeight();

    for (int index = 0; index < 7; index += 1) {
        fontDrawText(windowBuffer + offset + 40, summary.defense[index].label.c_str(), 80, pitch, COLOR_GREEN);
        fontDrawText(windowBuffer + offset + 104, summary.defense[index].value.c_str(), 80, pitch, COLOR_GREEN);
        offset += pitch * fontGetLineHeight();
    }

    bufferDrawLine(windowBuffer, pitch, summaryX, 18 * fontGetLineHeight() / 2 + 48, summaryMaxX, 18 * fontGetLineHeight() / 2 + 48, COLOR_GREEN);
    bufferDrawLine(windowBuffer, pitch, summaryX, 26 * fontGetLineHeight() / 2 + 48, summaryMaxX, 26 * fontGetLineHeight() / 2 + 48, COLOR_GREEN);

    offset += pitch * fontGetLineHeight();

    Object* itemsInHands[2] = {
        gInventoryLeftHandItem,
        gInventoryRightHandItem,
    };

    // Four lines per hand: item name, damage, ammo.
    for (int index = 0; index < 2; index += 1) {
        bool weapon = itemsInHands[index] != nullptr && itemGetType(itemsInHands[index]) == ITEM_TYPE_WEAPON;
        const std::string* lines = summary.hands[index];
        fontDrawText(windowBuffer + offset, lines[0].c_str(), itemsInHands[index] != nullptr ? 140 : 120, pitch, COLOR_GREEN);
        fontDrawText(windowBuffer + offset + pitch * fontGetLineHeight(), lines[1].c_str(), weapon ? 140 : 120, pitch, COLOR_GREEN);
        fontDrawText(windowBuffer + offset + 2 * pitch * fontGetLineHeight(), lines[2].c_str(), 140, pitch, COLOR_GREEN);
        offset += 4 * pitch * fontGetLineHeight();
    }

    if (!summary.weight.empty()) {
        if (ProtoId(_stack[0]).objectType() == OBJ_TYPE_CRITTER) {
            fontDrawText(windowBuffer + offset + 15, summary.weight.c_str(), 120, pitch, summary.encumbered ? COLOR_RED : COLOR_GREEN);
        } else {
            fontDrawText(windowBuffer + offset + 30, summary.weight.c_str(), 80, pitch, COLOR_GREEN);
        }
    }

    fontSetCurrent(oldFont);
}

// Finds next item of given [itemType] (can be -1 which means any type of
// item).
//
// The [index] is used to control where to continue the search from, -1 - from
// the beginning.
//
// 0x472698
Object* inventoryFindByType(Object* obj, ItemType itemType, int* indexPtr)
{
    int dummy = -1;
    if (indexPtr == nullptr) {
        indexPtr = &dummy;
    }

    *indexPtr += 1;

    Inventory* inventory = &(obj->data.inventory);

    // TODO: Refactor with for loop.
    if (*indexPtr >= inventory->length) {
        return nullptr;
    }

    while (itemType != -1 && itemGetType(inventory->items[*indexPtr].item) != itemType) {
        *indexPtr += 1;

        if (*indexPtr >= inventory->length) {
            return nullptr;
        }
    }

    return inventory->items[*indexPtr].item;
}

// Searches for an item with a given id inside given obj's inventory.
//
// 0x4726EC
Object* inventoryFindById(Object* obj, int id)
{
    if (obj->id == id) {
        return obj;
    }

    Inventory* inventory = &(obj->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        InventoryItem* inventoryItem = &(inventory->items[index]);
        Object* item = inventoryItem->item;
        if (item->id == id) {
            return item;
        }

        if (itemGetType(item) == ITEM_TYPE_CONTAINER) {
            item = inventoryFindById(item, id);
            if (item != nullptr) {
                return item;
            }
        }
    }

    return nullptr;
}

// Returns inventory item at a given index.
//
// 0x472740
Object* inventoryItemByIndex(Object* obj, int index)
{
    Inventory* inventory;

    inventory = &(obj->data.inventory);

    if (index < 0 || index >= inventory->length) {
        return nullptr;
    }

    return inventory->items[index].item;
}

// inven_wield
// 0x472758
int inventoryEquip(Object* critter, Object* item, Hand hand)
{
    return inventoryEquipFunc(critter, item, hand, true);
}

// 0x472768
int inventoryEquipFunc(Object* critter, Object* item, Hand handIndex, bool animate)
{
    ItemType itemType = itemGetType(item);

    InvenSlot invenSlot = itemType == ITEM_TYPE_ARMOR
        ? InvenSlot::Armor
        : (handIndex == HAND_RIGHT ? InvenSlot::RightHand : InvenSlot::LeftHand);
    if (!scriptHooks_InvenWield(critter, item, invenSlot, 1, 0)) {
        return -1;
    }

    if (animate) {
        if (!isoIsDisabled()) {
            reg_anim_begin(ANIMATION_REQUEST_RESERVED);
        }
    }

    if (itemType == ITEM_TYPE_ARMOR) {
        Object* armor = critterGetArmor(critter);
        if (armor != nullptr) {
            armor->flags &= ~OBJECT_WORN;
        }

        item->flags |= OBJECT_WORN;

        CritterFrameId baseFrameId;
        if (critterGetStat(critter, STAT_GENDER) == GENDER_FEMALE) {
            baseFrameId = armorGetFemaleFrameId(item);
        } else {
            baseFrameId = armorGetMaleFrameId(item);
        }

        if (baseFrameId == CritterFrameId::Invalid) {
            baseFrameId = CritterFrameId::First;
        }

        if (critter == gDude) {
            if (!isoIsDisabled()) {
                const CritterFrmId frmId = CritterFrmId(baseFrameId, ANIM_STAND, FrmId(critter).weaponAnimation(), critter->rotation + 1);
                animationRegisterSetFrmId(critter, frmId, 0);
            }
        } else {
            adjustCritterStatsOnArmorChange(critter, armor, item);
        }
    } else {
        Hand hand;
        if (critter == gDude) {
            hand = interfaceGetCurrentHand();
        } else {
            hand = HAND_RIGHT;
        }

        WeaponAnimation weaponAnimationCode = weaponGetAnimationCode(item);
        AnimationType hitModeAnimationCode = weaponGetAnimationForHitMode(item, HIT_MODE_RIGHT_WEAPON_PRIMARY);
        const FrmId frmId = FrmId(critter, hitModeAnimationCode, weaponAnimationCode, critter->rotation + 1);
        if (!frmId.exist()) {
            debugPrint("\ninven_wield failed!  ERROR ERROR ERROR!");
            return -1;
        }

        Object* equippedItem;
        if (handIndex == HAND_RIGHT) {
            equippedItem = critterGetItem2(critter);
            item->flags |= OBJECT_IN_RIGHT_HAND;
        } else {
            equippedItem = critterGetItem1(critter);
            item->flags |= OBJECT_IN_LEFT_HAND;
        }

        Rect rect;
        if (equippedItem != nullptr) {
            equippedItem->flags &= ~OBJECT_IN_ANY_HAND;

            if (ProtoId(equippedItem) == ItemProtoTypeId::LitFlare) {
                critterRestoreLightWithoutFlare(critter);
            }
        }

        if (ProtoId(item) == ItemProtoTypeId::LitFlare) {
            int lightDistance = item->lightDistance;
            if (lightDistance < critter->lightDistance) {
                lightDistance = critter->lightDistance;
            }

            int lightIntensity = item->lightIntensity;
            if (lightIntensity < critter->lightIntensity) {
                lightIntensity = critter->lightIntensity;
            }

            objectSetLight(critter, lightDistance, lightIntensity, &rect);
            tileWindowRefreshRect(&rect, gElevation);
        }

        // SFALL: Fix for "Unlimited Ammo" exploit: one item of a stack is
        // wielded, the rest stays in the inventory. A whole stack in hand
        // (from `wield_obj_critter`, a party member's equipping) shared one
        // weapon's ammo.
        ObjectFlags handFlags = item->flags & OBJECT_IN_ANY_HAND;
        bool removed = itemRemoveWithReason(critter, item, 1, RemoveInventoryObjectHookReason::WieldStack, nullptr) == 0;

        Object* stackInHand = handIndex == HAND_RIGHT ? critterGetItem2(critter) : critterGetItem1(critter);
        if (stackInHand != nullptr) {
            stackInHand->flags &= ~OBJECT_IN_ANY_HAND;
        }

        item->flags |= handFlags;

        if (removed) {
            itemAdd(critter, item, 1);
        }

        if (itemGetType(item) == ITEM_TYPE_WEAPON) {
            weaponAnimationCode = weaponGetAnimationCode(item);
        } else {
            weaponAnimationCode = WeaponAnimation::None;
        }

        if (hand == handIndex) {
            if (FrmId(critter).weaponAnimation() != WeaponAnimation::None) {
                if (animate) {
                    if (!isoIsDisabled()) {
                        const char* soundEffectName = sfxBuildCharName(critter, ANIM_PUT_AWAY, CharacterSoundEffect::Unused);
                        animationRegisterPlaySoundEffect(critter, soundEffectName, 0);
                        animationRegisterAnimate(critter, ANIM_PUT_AWAY, 0);
                    }
                }
            }

            if (animate && !isoIsDisabled()) {
                if (weaponAnimationCode != WeaponAnimation::None) {
                    animationRegisterTakeOutWeapon(critter, weaponAnimationCode, -1);
                } else {
                    const FrmId frmId = FrmId(critter, ANIM_STAND, WeaponAnimation::None, critter->rotation + 1);
                    animationRegisterSetFrmId(critter, frmId, -1);
                }
            } else {
                const FrmId frmId = FrmId(critter, ANIM_STAND, weaponAnimationCode, critter->rotation + 1);
                _dude_stand(critter, critter->rotation, frmId);
            }
        }
    }

    if (animate) {
        if (!isoIsDisabled()) {
            return reg_anim_end();
        }
    }

    return 0;
}

// inven_unwield
// 0x472A54
int inventoryUnequip(Object* critter_obj, Hand hand)
{
    return inventoryUnequipFunc(critter_obj, hand, true);
}

// CE: Restore a critter's own light unless a lit flare is still in either hand.
void critterRestoreLightWithoutFlare(Object* critter)
{
    Object* leftHandItem = critterGetItem1(critter);
    if (leftHandItem != nullptr && ProtoId(leftHandItem) == ItemProtoTypeId::LitFlare) {
        return;
    }

    Object* rightHandItem = critterGetItem2(critter);
    if (rightHandItem != nullptr && ProtoId(rightHandItem) == ItemProtoTypeId::LitFlare) {
        return;
    }

    int lightDistance;
    int lightIntensity;
    if (critter == gDude) {
        lightDistance = 4;
        lightIntensity = LIGHT_INTENSITY_MAX;
    } else {
        Proto* proto;
        if (protoGetProto(critter, &proto) == -1) {
            return;
        }

        lightDistance = proto->lightDistance;
        lightIntensity = proto->lightIntensity;
    }

    Rect rect;
    if (objectSetLight(critter, lightDistance, lightIntensity, &rect) == 0) {
        tileWindowRefreshRect(&rect, critter->elevation);
    }
}

// 0x472A64
int inventoryUnequipFunc(Object* critter, Hand hand, bool animate)
{
    int activeHand;
    Object* item;

    if (critter == gDude) {
        activeHand = interfaceGetCurrentHand();
    } else {
        activeHand = HAND_RIGHT; // NPC's only ever use right slot
    }

    if (hand) {
        item = critterGetItem2(critter);
    } else {
        item = critterGetItem1(critter);
    }

    // Notify scripts before mutating the OBJECT_IN_ANY_HAND flag..
    if (item != nullptr) {
        InvenSlot invenSlot = hand == HAND_RIGHT ? InvenSlot::RightHand : InvenSlot::LeftHand;
        if (!scriptHooks_InvenWield(critter, item, invenSlot, 0, 0)) {
            return -1;
        }
    }

    if (item) {
        item->flags &= ~OBJECT_IN_ANY_HAND;

        // CE: Restore light when unwielding a lit flare.
        if (ProtoId(item) == ItemProtoTypeId::LitFlare) {
            critterRestoreLightWithoutFlare(critter);
        }
    }

    if (activeHand == hand && (FrmId(critter).weaponAnimation() != WeaponAnimation::None)) {
        if (animate && !isoIsDisabled()) {
            reg_anim_begin(ANIMATION_REQUEST_RESERVED);

            const char* sfx = sfxBuildCharName(critter, ANIM_PUT_AWAY, CharacterSoundEffect::Unused);
            animationRegisterPlaySoundEffect(critter, sfx, 0);

            animationRegisterAnimate(critter, ANIM_PUT_AWAY, 0);

            const FrmId frmId = FrmId(critter, ANIM_STAND, WeaponAnimation::None, critter->rotation + 1);
            animationRegisterSetFrmId(critter, frmId, -1);

            return reg_anim_end();
        }

        const FrmId frmId = FrmId(critter, ANIM_STAND, WeaponAnimation::None, critter->rotation + 1);
        _dude_stand(critter, critter->rotation, frmId);
    }

    return 0;
}

// 0x472B54
static int _inven_from_button(int keyCode, Object** outItem, Object*** outItemSlot, Object** outOwner)
{
    Object** itemSlot;
    Object* owner;
    Object* item;
    int quantity = 0;

    switch (keyCode) {
    case PARTY_WEAPON_SLOT_KEY:
        itemSlot = getPartySlotItemPtr(InvenSlot::RightHand);
        owner = partyBaseTarget;
        item = itemSlot != nullptr ? *itemSlot : nullptr;
        break;
    case PARTY_ARMOR_SLOT_KEY:
        itemSlot = getPartySlotItemPtr(InvenSlot::Armor);
        owner = partyBaseTarget;
        item = itemSlot != nullptr ? *itemSlot : nullptr;
        break;
    case INVENTORY_HAND_RIGHT_KEY:
        itemSlot = &gInventoryRightHandItem;
        owner = _stack[0];
        item = gInventoryRightHandItem;
        break;
    case INVENTORY_HAND_LEFT_KEY:
        itemSlot = &gInventoryLeftHandItem;
        owner = _stack[0];
        item = gInventoryLeftHandItem;
        break;
    case INVENTORY_ARMOR_KEY:
        itemSlot = &gInventoryArmor;
        owner = _stack[0];
        item = gInventoryArmor;
        break;
    default:
        itemSlot = nullptr;
        owner = nullptr;
        item = nullptr;

        InventoryItem* inventoryItem = nullptr;
        if (keyCode < 2000) {
            int index = _stack_offset[_curr_stack] + keyCode - 1000;
            if (index >= _pud->length) {
                break;
            }

            inventoryItem = &(_pud->items[_pud->length - (index + 1)]);
            item = inventoryItem->item;
            owner = _stack[_curr_stack];
        } else if (keyCode < 2300) {
            int index = _target_stack_offset[_target_curr_stack] + keyCode - 2000;
            if (index >= _target_pud->length) {
                break;
            }

            inventoryItem = &(_target_pud->items[_target_pud->length - (index + 1)]);
            item = inventoryItem->item;
            owner = _target_stack[_target_curr_stack];
        } else if (keyCode < 2400) {
            int index = gPlayerTableOffset + keyCode - 2300;
            if (index >= gPlayerTableInventory->length) {
                break;
            }

            inventoryItem = &(gPlayerTableInventory->items[gPlayerTableInventory->length - (index + 1)]);
            item = inventoryItem->item;
            owner = gPlayerTableObj;
        } else {
            int index = gBartererTableOffset + keyCode - 2400;
            if (index >= gBartererTableInventory->length) {
                break;
            }

            inventoryItem = &(gBartererTableInventory->items[gBartererTableInventory->length - (index + 1)]);
            item = inventoryItem->item;
            owner = gBartererTableObj;
        }

        if (inventoryItem != nullptr) {
            quantity = inventoryItem->quantity;
        }
    }

    if (outItemSlot != nullptr) {
        *outItemSlot = itemSlot;
    }

    if (outItem != nullptr) {
        *outItem = item;
    }

    if (outOwner != nullptr) {
        *outOwner = owner;
    }

    if (quantity == 0 && item != nullptr) {
        quantity = 1;
    }

    return quantity;
}

static void inventoryDrawCenteredText(unsigned char* buffer, int pitch, int width, int x, int y, const char* text, ColorWithFlags color)
{
    if (text == nullptr) {
        return;
    }

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);
    int textWidth = std::min(fontGetStringWidth(text), width);
    int textX = x + width / 2 - textWidth / 2;
    fontDrawText(buffer + pitch * y + textX, text, width, pitch, color);
    fontSetCurrent(oldFont);
}

static void displayLootPanePartyName(unsigned char* windowBuffer, int windowPitch, const Rect& rect, int index)
{
    Object* critter = nullptr;
    if (index == 0) {
        if (_stack[0] != gDude) {
            critter = _stack[0];
        }
    } else if (index == 1) {
        if (hasPartySlots()) {
            critter = partyBaseTarget;
        }
    }

    if (critter == nullptr) {
        return;
    }

    const char* name = critterGetName(critter);
    if (name == nullptr) {
        return;
    }

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);
    int nameY = rect.bottom - fontGetLineHeight();
    fontSetCurrent(oldFont);

    inventoryDrawCenteredText(windowBuffer, windowPitch, INVENTORY_BODY_VIEW_WIDTH, rect.left, nameY, name, COLOR_GREEN | DRAW_TEXT_FLAG_NONE);
}

// Displays item description.
//
// inven_display_msg
// 0x472D24
static void inventoryRenderItemDescription(const char* string)
{
    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    if (string == nullptr || !inventoryHasWindow()) {
        fontSetCurrent(oldFont);
        return;
    }

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    int pitch = inventoryLayout.windowWidth;
    windowBuffer += pitch * INVENTORY_SUMMARY_Y + inventoryLayout.summaryX;

    std::string mutableString(string);

    char* c = mutableString.data();
    while (c != nullptr && *c != '\0') {
        _inven_display_msg_line += 1;
        if (_inven_display_msg_line > 17) {
            debugPrint("\nError: inven_display_msg: out of bounds!");
            goto end;
        }

        char* space = nullptr;
        if (fontGetStringWidth(c) > 152) {
            // Look for next space.
            space = c + 1;
            while (*space != '\0' && *space != ' ') {
                space += 1;
            }

            if (*space == '\0') {
                // This was the last line containing very long word. Text
                // drawing routine will silently truncate it after reaching
                // desired length.
                fontDrawText(windowBuffer + pitch * _inven_display_msg_line * fontGetLineHeight(), c, 152, pitch, COLOR_GREEN);
                goto end;
            }

            char* nextSpace = space + 1;
            while (true) {
                while (*nextSpace != '\0' && *nextSpace != ' ') {
                    nextSpace += 1;
                }

                if (*nextSpace == '\0') {
                    break;
                }

                // Break string and measure it.
                *nextSpace = '\0';
                if (fontGetStringWidth(c) >= 152) {
                    // Next space is too far to fit in one line. Restore next
                    // space's character and stop.
                    *nextSpace = ' ';
                    break;
                }

                space = nextSpace;

                // Restore next space's character and continue looping from the
                // next character.
                *nextSpace = ' ';
                nextSpace += 1;
            }

            if (*space == ' ') {
                *space = '\0';
            }
        }

        if (fontGetStringWidth(c) > 152) {
            debugPrint("\nError: inven_display_msg: word too long!");
            goto end;
        }

        fontDrawText(windowBuffer + pitch * _inven_display_msg_line * fontGetLineHeight(), c, 152, pitch, COLOR_GREEN);

        if (space != nullptr) {
            c = space + 1;
            if (*space == '\0') {
                *space = ' ';
            }
        } else {
            c = nullptr;
        }
    }

end:
    fontSetCurrent(oldFont);
}

// Examines inventory item.
//
// 0x472EB8
static void inventoryExamineItem(Object* critter, Object* item)
{
    // Mobile UI: its info panel shows the examined item.
    if (!inventoryHasWindow()) {
        gInventoryExaminedItem = item;
        gInventoryExaminedVersion++;
        return;
    }

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);
    int pitch = inventoryLayout.windowWidth;
    int summaryX = inventoryLayout.summaryX;
    int summaryMaxX = summaryX + (INVENTORY_SUMMARY_MAX_X - INVENTORY_SUMMARY_X);

    // Clear item description area.
    unsigned char* backgroundData = inventoryFrmImage.getData();
    int backgroundWidth = inventoryFrmImage.getWidth();
    if (backgroundData != nullptr) {
        blitBufferToBuffer(backgroundData + backgroundWidth * INVENTORY_SUMMARY_Y + summaryX,
            INVENTORY_SUMMARY_WIDTH,
            INVENTORY_SUMMARY_HEIGHT,
            backgroundWidth,
            windowBuffer + pitch * INVENTORY_SUMMARY_Y + summaryX,
            pitch);
    }

    // Reset item description lines counter.
    _inven_display_msg_line = 0;

    // Render item's name.
    char* itemName = objectGetName(item);
    inventoryRenderItemDescription(itemName);

    // Increment line counter to accomodate separator below.
    _inven_display_msg_line += 1;

    int lineHeight = fontGetLineHeight();

    // Draw separator.
    // SFALL: Fix separator position when item name is longer than one line.
    bufferDrawLine(windowBuffer,
        pitch,
        summaryX,
        (_inven_display_msg_line - 1) * lineHeight + lineHeight / 2 + 49,
        summaryMaxX,
        (_inven_display_msg_line - 1) * lineHeight + lineHeight / 2 + 49,
        COLOR_GREEN);

    inventoryExamineItemText(critter, item, inventoryRenderItemDescription);

    fontSetCurrent(oldFont);

    gInventoryExaminedItem = item;
    gInventoryExaminedVersion++;
}

// What looking at the item tells and its weight.
static void inventoryExamineItemText(Object* critter, Object* item, void (*fn)(const char* string))
{
    // Examine item.
    objectExamineFunc(critter, item, fn);

    // Add weight if neccessary.
    int weight = itemGetWeight(item);
    if (weight != 0) {
        MessageListItem messageListItem;
        messageListItem.num = 540;

        if (weight == 1) {
            messageListItem.num = 541;
        }

        if (!messageListGetItem(&gProtoMessageList, &messageListItem)) {
            debugPrint("\nError: Couldn't find message!");
        }

        char formattedText[40];
        snprintf(formattedText, sizeof(formattedText), messageListItem.text, weight);
        fn(formattedText);
    }
}

// 0x47304C
static void inventoryWindowOpenContextMenu(int keyCode, int inventoryWindowType)
{
    Object* item;
    Object** itemSlot;
    Object* owner;

    int quantity = _inven_from_button(keyCode, &item, &itemSlot, &owner);
    if (quantity == 0) {
        return;
    }

    ItemType itemType = itemGetType(item);

    MouseButtonEvent mouseEvent;
    do {
        sharedFpsLimiter.mark();

        inputGetInput();

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
            _display_body(FrmId::Empty(), INVENTORY_WINDOW_TYPE_NORMAL);
        }

        mouseEvent = mouseGetEvent();
        if ((mouseEvent & MouseButtonEvent::LeftButtonUp) != MouseButtonEvent::None) {
            if (inventoryWindowType != INVENTORY_WINDOW_TYPE_NORMAL) {
                objectLookAtFunc(_stack[0], item, gInventoryPrintItemDescriptionHandler);
            } else {
                inventoryExamineItem(_stack[0], item);
            }
            windowRefresh(gInventoryWindow);
            return;
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    } while ((mouseEvent & MouseButtonEvent::LeftButtonDownRepeat) != MouseButtonEvent::LeftButtonDownRepeat);

    int x;
    int y;
    mouseGetPosition(&x, &y);
    inventoryRunItemActionMenu(keyCode, inventoryWindowType, x, y);
}

// Shared by the action menu and the touch figure drop so both take drugs
// through the same game effects, hooks and inventory removal path.
static void inventoryUseDrug(Object* item, Object** itemSlot, Object* owner)
{
    if (drugItemTakeDrug(_stack[0], item)) {
        if (itemSlot != nullptr) {
            *itemSlot = nullptr;
        } else {
            itemRemoveWithReason(owner, item, 1, RemoveInventoryObjectHookReason::ConsumeDrug);
        }

        _obj_connect(item, gDude->tile, gDude->elevation, nullptr);
        objectDestroy(item);
    }

    interfaceRenderHitPoints(true);
}

// CE: Action menu of the item at [keyCode] opened at screen point ([x], [y])
// and the chosen action; the mobile UI inventory screen opens it by long
// press.
static void inventoryRunItemActionMenu(int keyCode, int inventoryWindowType, int x, int y)
{
    Object* item;
    Object** itemSlot;
    Object* owner;

    int quantity = _inven_from_button(keyCode, &item, &itemSlot, &owner);
    if (quantity == 0) {
        return;
    }

    ItemType itemType = itemGetType(item);

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_BLANK);

    int actionMenuItemsLength;
    const GameMouseActionMenuItem* actionMenuItems;
    if (itemType == ITEM_TYPE_WEAPON && weaponCanBeUnloaded(item)) {
        if (inventoryWindowType != INVENTORY_WINDOW_TYPE_NORMAL && objectGetOwner(item) != gDude) {
            actionMenuItemsLength = 3;
            actionMenuItems = _act_weap2;
        } else {
            actionMenuItemsLength = 4;
            actionMenuItems = _act_weap;
        }
    } else {
        if (inventoryWindowType != INVENTORY_WINDOW_TYPE_NORMAL) {
            // SFALL: Fix crash when trying to open bag/backpack on the table
            // in the bartering interface.
            Object* owner = objectGetOwner(item);
            if (owner != gDude) {
                if (itemType == ITEM_TYPE_CONTAINER && (owner == _stack[_curr_stack] || owner == _target_stack[_target_curr_stack])) {
                    actionMenuItemsLength = 3;
                    actionMenuItems = _act_just_use;
                } else {
                    actionMenuItemsLength = 2;
                    actionMenuItems = _act_nothing;
                }
            } else {
                if (itemType == ITEM_TYPE_CONTAINER) {
                    actionMenuItemsLength = 4;
                    actionMenuItems = _act_use;
                } else {
                    actionMenuItemsLength = 3;
                    actionMenuItems = _act_no_use;
                }
            }
        } else {
            if (itemType == ITEM_TYPE_CONTAINER && itemSlot != nullptr) {
                actionMenuItemsLength = 3;
                actionMenuItems = _act_no_use;
            } else {
                if (_obj_action_can_use(item) || _proto_action_can_use_on(item)) {
                    actionMenuItemsLength = 4;
                    actionMenuItems = _act_use;
                } else {
                    actionMenuItemsLength = 3;
                    actionMenuItems = _act_no_use;
                }
            }
        }
    }

    int menuItemIndex = 0;

    // CE: With touch controls actions are shown in radial menu around the
    // finger, like on the map (always in the touch-only build: no mouse).
    if (FALLOUT_TOUCH_ONLY || touchControlsIsEnabled()) {
        menuItemIndex = touchControlsChooseActionMenuItem(actionMenuItems, actionMenuItemsLength, x, y);
        if (menuItemIndex == -1) {
            inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
            windowRefresh(gInventoryWindow);
            return;
        }
    } else {
#if !FALLOUT_TOUCH_ONLY
        const InventoryWindowDescription* windowDescription = &(gInventoryWindowDescriptions[inventoryWindowType]);
        int windowWidth = inventoryGetWindowWidth(inventoryWindowType);
        int windowHeight = inventoryGetWindowHeight(inventoryWindowType);

        Rect windowRect;
        windowGetRect(gInventoryWindow, &windowRect);
        int inventoryWindowX = windowRect.left;
        int inventoryWindowY = windowRect.top;

        if (gameMouseRenderActionMenuItems(x, y, actionMenuItems, actionMenuItemsLength,
                windowWidth + inventoryWindowX,
                windowHeight + inventoryWindowY)
            == -1) {
            inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
            return;
        }

        InventoryCursorData* cursorData = &(gInventoryCursorData[INVENTORY_WINDOW_CURSOR_MENU]);

        int offsetX;
        int offsetY;
        artGetRotationOffsets(cursorData->frm, ROTATION_NE, &offsetX, &offsetY);

        Rect rect;
        rect.left = x - inventoryWindowX - cursorData->width / 2 + offsetX;
        rect.top = y - inventoryWindowY - cursorData->height + 1 + offsetY;
        rect.right = rect.left + cursorData->width - 1;
        rect.bottom = rect.top + cursorData->height - 1;

        int menuButtonHeight = cursorData->height;
        if (rect.top + menuButtonHeight > windowHeight) {
            menuButtonHeight = windowHeight - rect.top;
        }

        int btn = buttonCreate(gInventoryWindow,
            rect.left, rect.top, cursorData->width, menuButtonHeight,
            -1, -1, -1, -1,
            cursorData->frmData, cursorData->frmData,
            nullptr, BUTTON_FLAG_TRANSPARENT);
        windowRefreshRect(gInventoryWindow, &rect);

        int previousMouseY = y;
        while ((mouseGetEvent() & MouseButtonEvent::LeftButtonUp) == MouseButtonEvent::None) {
            sharedFpsLimiter.mark();

            inputGetInput();

            if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
                _display_body(FrmId::Empty(), INVENTORY_WINDOW_TYPE_NORMAL);
            }

            int x;
            int y;
            mouseGetPosition(&x, &y);
            if (y - previousMouseY > 10 || previousMouseY - y > 10) {
                if (y >= previousMouseY || menuItemIndex <= 0) {
                    if (previousMouseY < y && menuItemIndex < actionMenuItemsLength - 1) {
                        menuItemIndex++;
                    }
                } else {
                    menuItemIndex--;
                }
                gameMouseHighlightActionMenuItemAtIndex(menuItemIndex);
                windowRefreshRect(gInventoryWindow, &rect);
                previousMouseY = y;
            }

            renderPresent();
            sharedFpsLimiter.throttle();
        }

        buttonDestroy(btn);

        Buffer2D dst { windowGetBuffer(gInventoryWindow), windowWidth, windowHeight };
        FrmImage backgroundFrmImage;
        int sourceXOffset;
        ConstBuffer2D background = inventoryGetBackgroundBuffer(inventoryWindowType, windowDescription->frmId, backgroundFrmImage, sourceXOffset);
        if (background) {
            blitBuffer2D(background,
                rect.left + sourceXOffset,
                rect.top,
                cursorData->width,
                menuButtonHeight,
                dst,
                rect.left,
                rect.top);
        }
#endif
    }

    _mouse_set_position(x, y);

    _display_inventory(_stack_offset[_curr_stack], -1, inventoryWindowType);

    int actionMenuItem = actionMenuItems[menuItemIndex];

    gInventoryLastMenuAction = actionMenuItem;
    gInventoryLastMenuItem = item;
    gInventoryLastMenuVersion++;

    switch (actionMenuItem) {
    case GAME_MOUSE_ACTION_MENU_ITEM_DROP:
        inventoryDropToGround(keyCode, item, itemSlot, owner, quantity, true);
        break;
    case GAME_MOUSE_ACTION_MENU_ITEM_LOOK:
        if (inventoryWindowType != INVENTORY_WINDOW_TYPE_NORMAL) {
            objectExamineFunc(_stack[0], item, gInventoryPrintItemDescriptionHandler);
        } else {
            inventoryExamineItem(_stack[0], item);
        }
        break;
    case GAME_MOUSE_ACTION_MENU_ITEM_USE:
        switch (itemType) {
        case ITEM_TYPE_CONTAINER:
            _container_enter(keyCode, inventoryWindowType);
            break;
        case ITEM_TYPE_DRUG:
            inventoryUseDrug(item, itemSlot, owner);
            break;
        case ITEM_TYPE_WEAPON:
        case ITEM_TYPE_MISC:
            if (itemSlot == nullptr) {
                itemRemoveWithReason(owner, item, 1, RemoveInventoryObjectHookReason::UseObj);
            }

            UseItemResultCode useResult;
            if (_obj_action_can_use(item)) {
                useResult = objectUseItemInternal(_stack[0], item);
            } else {
                useResult = objectUseItemOnInternal(_stack[0], _stack[0], item);
            }

            if (useResult == USE_ITEM_RESULT_REMOVE) {
                if (itemSlot != nullptr) {
                    *itemSlot = nullptr;
                }

                _obj_connect(item, gDude->tile, gDude->elevation, nullptr);
                objectDestroy(item);
            } else {
                if (itemSlot == nullptr) {
                    itemAdd(owner, item, 1);
                }
            }
        default:
            break;
        }
        break;
    case GAME_MOUSE_ACTION_MENU_ITEM_UNLOAD:
        if (itemSlot == nullptr) {
            itemRemoveWithReason(owner, item, 1, RemoveInventoryObjectHookReason::UnloadWeapon);
        }

        inventoryUnloadWeaponToOwner(owner, item);

        if (itemSlot == nullptr) {
            itemAdd(owner, item, 1);
        }
        break;
    default:
        break;
    }

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL && actionMenuItem != GAME_MOUSE_ACTION_MENU_ITEM_LOOK) {
        inventoryRenderSummary();
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT
        || inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, inventoryWindowType);
    }

    _display_inventory(_stack_offset[_curr_stack], -1, inventoryWindowType);

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        barterDisplayTables(gInventoryBarterBackgroundWindow, gPlayerTableObj, gBartererTableObj, -1);
    }

    _adjust_fid();
}

// Drops [quantity] of the item at [keyCode] to the ground ([askQuantity] -
// lets the player choose how many).
static void inventoryDropToGround(int keyCode, Object* item, Object** itemSlot, Object* owner, int quantity, bool askQuantity)
{
    bool inventoryMoveAlreadyChecked = false;
    if (itemSlot != nullptr) {
        if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item, nullptr)) {
            return;
        }

        inventoryMoveAlreadyChecked = true;
        if (itemSlot == &gInventoryArmor) {
            adjustCritterStatsOnArmorChange(_stack[0], item, nullptr);
        }
        itemAdd(owner, item, 1);
        quantity = 1;
        *itemSlot = nullptr;
    }

    if (ProtoId(item) == ItemProtoTypeId::Money) {
        if (quantity > 1) {
            if (askQuantity) {
                quantity = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, quantity);
            }
        } else {
            quantity = 1;
        }

        if (quantity > 0) {
            if (quantity == 1) {
                if (inventoryMoveAlreadyChecked || scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item, nullptr)) {
                    itemSetMoney(item, 1);
                    objectDrop(owner, item);
                }
            } else {
                if (itemRemoveWithReason(owner, item, quantity - 1, RemoveInventoryObjectHookReason::InventoryDropCaps) == 0) {
                    Object* item2;
                    if (_inven_from_button(keyCode, &item2, &itemSlot, &owner) != 0) {
                        if (scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item2, nullptr)) {
                            itemSetMoney(item2, quantity);
                            objectDrop(owner, item2);
                        } else {
                            itemAdd(owner, item, quantity - 1);
                        }
                    } else {
                        itemAdd(owner, item, quantity - 1);
                    }
                }
            }
        }
    } else if (explosiveIsActiveExplosive(item)) {
        if (inventoryMoveAlreadyChecked || scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item, nullptr)) {
            _dropped_explosive = 1;
            objectDrop(owner, item);
        }
    } else {
        if (quantity > 1) {
            if (askQuantity) {
                quantity = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, quantity);
            }

            for (int index = 0; index < quantity; index++) {
                if (_inven_from_button(keyCode, &item, &itemSlot, &owner) != 0) {
                    if (scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item, nullptr)) {
                        objectDrop(owner, item);
                    }
                }
            }
        } else {
            if (inventoryMoveAlreadyChecked || scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_GROUND, item, nullptr)) {
                objectDrop(owner, item);
            }
        }
    }
}

// MARK: Mobile UI inventory screen

// CE: Weight of the inventory's owner with items in hands and armor. Equipped
// items are out of the inventory while the window is open and are counted
// for `_inven_dude` only, which is the container while one is open.
static int inventoryGetCritterWeight()
{
    Object* savedDude = _inven_dude;
    _inven_dude = _stack[0];
    int weight = objectGetInventoryWeight(_stack[0]);
    _inven_dude = savedDude;
    return weight;
}

bool inventoryGetView(InventoryView* view)
{
    if (!gInventoryViewActive) {
        return false;
    }

    view->critter = _stack[0];
    view->container = _curr_stack > 0 ? _stack[_curr_stack] : nullptr;
    view->listOwner = _stack[_curr_stack];
    view->leftHand = gInventoryLeftHandItem;
    view->rightHand = gInventoryRightHandItem;
    view->armor = gInventoryArmor;
    view->activeHand = _stack[0] == gDude ? interfaceGetCurrentHand() : HAND_RIGHT;
    view->weight = inventoryGetCritterWeight();
    view->carryWeight = critterGetStat(_stack[0], STAT_CARRY_WEIGHT);
    view->bodyFrmId = gInventoryCritterFrmId;
    view->bodyRotation = gInventoryWindowDudeRotation;
    view->examinedItem = gInventoryExaminedItem;
    view->examinedVersion = gInventoryExaminedVersion;
    view->lastMenuAction = gInventoryLastMenuAction;
    view->lastMenuItem = gInventoryLastMenuItem;
    view->lastMenuVersion = gInventoryLastMenuVersion;
    return true;
}

void inventorySetBodyRotation(int rotation)
{
    gInventoryWindowDudeRotation = static_cast<Rotation>(((rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT);
}

void inventoryQueueAction(const InventoryAction& action)
{
    gInventoryActions.push_back(action);
}

void inventoryRequestClose()
{
    gInventoryCloseRequested = true;
}

bool inventoryCanDropOnto(Object* item, Object* target)
{
    if (item == nullptr || target == nullptr || item == target) {
        return false;
    }

    if (itemGetType(target) == ITEM_TYPE_CONTAINER) {
        return true;
    }

    bool replaceAmmo;
    return inventoryCanLoadAmmo(target, item, &replaceAmmo);
}

bool inventoryCanUseOnSelf(Object* item)
{
    return item != nullptr && itemGetType(item) == ITEM_TYPE_DRUG;
}

bool inventoryGetSummary(InventorySummary* summary)
{
    if (!gInventoryViewActive) {
        return false;
    }

    inventoryBuildSummary(summary);
    return true;
}

// Collects lines of item examination.
static std::vector<std::string>* gInventoryItemInfoText = nullptr;

static void inventoryCollectItemInfoText(const char* string)
{
    if (gInventoryItemInfoText != nullptr) {
        gInventoryItemInfoText->push_back(string);
    }
}

void inventoryGetItemInfo(Object* item, InventoryItemInfo* info)
{
    Object* critter = gInventoryViewActive ? _stack[0] : gDude;

    info->name = objectGetName(item);
    info->text.clear();
    info->rows.clear();
    info->actionPoints = -1;

    gInventoryItemInfoText = &(info->text);
    inventoryExamineItemText(critter, item, inventoryCollectItemInfoText);
    gInventoryItemInfoText = nullptr;

    MessageListItem messageListItem;
    ItemType itemType = itemGetType(item);
    if (itemType == ITEM_TYPE_WEAPON) {
        // Numbers of the primary attack as if the critter held the weapon
        // (perks and traits apply), game functions take it from the hand.
        Object* savedDude = _inven_dude;
        Object* savedRightHand = gInventoryRightHandItem;
        _inven_dude = critter;
        gInventoryRightHandItem = item;

        char text[80];
        std::string damage;
        int range = inventoryFormatWeaponDamage(&gInventoryMessageList, critter, item, HIT_MODE_RIGHT_WEAPON_PRIMARY, text, sizeof(text), &damage);
        info->actionPoints = weaponGetActionPointCost(critter, HIT_MODE_RIGHT_WEAPON_PRIMARY, false);

        gInventoryRightHandItem = savedRightHand;
        _inven_dude = savedDude;

        messageListItem.num = 15; // Dmg:
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            info->rows.push_back({ messageListItem.text, damage });
        }

        messageListItem.num = 16; // Rng:
        if (range != -1 && messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            info->rows.push_back({ messageListItem.text, std::to_string(range) });
        }
    } else if (itemType == ITEM_TYPE_ARMOR) {
        messageListItem.num = 8; // Armor class
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            info->rows.push_back({ messageListItem.text, std::to_string(armorGetArmorClass(item)) });
        }

        // Normal, laser, fire, plasma, explosion as in the summary.
        const DamageType damageTypes[5] = {
            DAMAGE_TYPE_NORMAL,
            DAMAGE_TYPE_LASER,
            DAMAGE_TYPE_FIRE,
            DAMAGE_TYPE_PLASMA,
            DAMAGE_TYPE_EXPLOSION,
        };

        for (int index = 0; index < 5; index++) {
            messageListItem.num = 9 + index;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                char value[32];
                snprintf(value, sizeof(value), "%d/%d%%", armorGetDamageThreshold(item, damageTypes[index]), armorGetDamageResistance(item, damageTypes[index]));
                info->rows.push_back({ messageListItem.text, value });
            }
        }
    }
}

// Key code of the window button for [item] in [slot] (functions shared with
// the mouse take them). The screen doesn't scroll the game's list, list items
// are counted from its start. -1 if the item is not there anymore.
static int inventoryItemKeyCode(Object* item, InventorySlot slot)
{
    switch (slot) {
    case InventorySlot::LeftHand:
        return gInventoryLeftHandItem == item ? INVENTORY_HAND_LEFT_KEY : -1;
    case InventorySlot::RightHand:
        return gInventoryRightHandItem == item ? INVENTORY_HAND_RIGHT_KEY : -1;
    case InventorySlot::Armor:
        return gInventoryArmor == item ? INVENTORY_ARMOR_KEY : -1;
    case InventorySlot::None:
        break;
    }

    _stack_offset[_curr_stack] = 0;
    for (int index = 0; index < _pud->length && index < 1000; index++) {
        if (_pud->items[_pud->length - (index + 1)].item == item) {
            return 1000 + index;
        }
    }
    return -1;
}

static void inventoryLogAction(const InventoryAction& action)
{
    static const char* const kTypes[] = { "move", "use on self", "action menu", "drop", "drop all", "close container" };
    static const char* const kSlots[] = { "list", "left hand", "right hand", "armor" };
    static const char* const kTargets[] = { "list", "left hand", "right hand", "armor", "body" };
    actionLog("inventory%s: %s%s from %s%s%s%s",
        _inven_dude == gDude ? "" : actionLogObject(_inven_dude),
        kTypes[static_cast<int>(action.type)],
        actionLogObject(action.item),
        kSlots[static_cast<int>(action.slot)],
        action.type == InventoryActionType::Move ? " to " : "",
        action.type == InventoryActionType::Move ? kTargets[static_cast<int>(action.target)] : "",
        actionLogObject(action.targetItem));
}

static void inventoryPerformAction(const InventoryAction& action)
{
    inventoryLogAction(action);

    switch (action.type) {
    case InventoryActionType::Move: {
        int keyCode = inventoryItemKeyCode(action.item, action.slot);
        if (keyCode == -1) {
            return;
        }

        Object* item;
        Object** itemSlot = nullptr;
        int count = _inven_from_button(keyCode, &item, &itemSlot, nullptr);
        if (count == 0) {
            return;
        }

        // Light of the item leaving the active hand (flare).
        bool leftActive = interfaceGetCurrentHand() == HAND_LEFT;
        if (_inven_dude == gDude && ((keyCode == INVENTORY_HAND_LEFT_KEY && leftActive) || (keyCode == INVENTORY_HAND_RIGHT_KEY && !leftActive))) {
            _inven_update_lighting(nullptr);
        }

        Object* targetItem = action.targetItem;
        if (targetItem != nullptr && inventoryItemKeyCode(targetItem, InventorySlot::None) == -1) {
            targetItem = nullptr;
        }

        int itemIndex = action.slot == InventorySlot::None ? keyCode - 1000 : -1;
        inventoryPlaceItem(item, itemSlot, itemIndex, count, keyCode, false, &action.target, targetItem);
        inventoryAfterItemMove(0);
        break;
    }
    case InventoryActionType::UseOnSelf: {
        // The UI only offers this over the player's figure, but the action
        // is queued and must still be valid when the inventory loop runs it.
        if (_curr_stack != 0 || _stack[0] != gDude) {
            return;
        }

        int keyCode = inventoryItemKeyCode(action.item, action.slot);
        Object* item;
        Object** itemSlot;
        Object* owner;
        if (keyCode == -1 || _inven_from_button(keyCode, &item, &itemSlot, &owner) == 0 || !inventoryCanUseOnSelf(item)) {
            return;
        }

        inventoryUseDrug(item, itemSlot, owner);
        gInventoryLastMenuAction = GAME_MOUSE_ACTION_MENU_ITEM_USE;
        gInventoryLastMenuItem = nullptr;
        gInventoryLastMenuVersion++;
        inventoryAfterItemMove(0);
        break;
    }
    case InventoryActionType::OpenActionMenu: {
        int keyCode = inventoryItemKeyCode(action.item, action.slot);
        if (keyCode != -1) {
            inventoryRunItemActionMenu(keyCode, INVENTORY_WINDOW_TYPE_NORMAL, action.x, action.y);
        }
        break;
    }
    case InventoryActionType::Drop: {
        int keyCode = inventoryItemKeyCode(action.item, action.slot);
        Object* item;
        Object** itemSlot;
        Object* owner;
        int quantity = keyCode != -1 ? _inven_from_button(keyCode, &item, &itemSlot, &owner) : 0;
        if (quantity != 0) {
            inventoryDropToGround(keyCode, item, itemSlot, owner, quantity, true);
            inventoryAfterItemMove(0);
        }
        break;
    }
    case InventoryActionType::DropAll:
        for (Object* listed : action.items) {
            int keyCode = inventoryItemKeyCode(listed, InventorySlot::None);
            Object* item;
            Object** itemSlot;
            Object* owner;
            int quantity = keyCode != -1 ? _inven_from_button(keyCode, &item, &itemSlot, &owner) : 0;
            if (quantity != 0) {
                inventoryDropToGround(keyCode, item, itemSlot, owner, quantity, false);
            }
        }
        inventoryAfterItemMove(0);
        break;
    case InventoryActionType::CloseContainer:
        _container_exit(2500, INVENTORY_WINDOW_TYPE_NORMAL);
        break;
    }
}

static void inventoryPerformQueuedActions()
{
    std::vector<InventoryAction> actions;
    actions.swap(gInventoryActions);

    for (const InventoryAction& action : actions) {
        inventoryPerformAction(action);
    }
}

// 0x473904
int inventoryOpenLooting(Object* looter, Object* target)
{
    MessageListItem messageListItem;

    if (looter != _inven_dude) {
        return 0;
    }

    if (FrmId(target).objectType() == OBJ_TYPE_CRITTER && critterFlagCheck(target, CRITTER_NO_STEAL)) {
        inventoryDisplayMessage(50); // You can't find anything to take from that.
        return 0;
    }

    if (FrmId(target).objectType() == OBJ_TYPE_ITEM && itemGetType(target) == ITEM_TYPE_CONTAINER) {
        if (target->frame == 0) {
            CacheEntry* handle;
            Art* frm = artLock(FrmId(target), &handle);
            if (frm != nullptr) {
                int frameCount = artGetFrameCount(frm);
                artUnlock(handle);
                if (frameCount > 1) {
                    return 0;
                }
            }
        }
    }

    int sid = -1;
    if (!_gIsSteal && objectGetSid(target, &sid) != -1) {
        scriptSetObjects(sid, looter, nullptr);
        scriptExecProc(sid, SCRIPT_PROC_PICKUP);

        Script* script;
        if (scriptGetScript(sid, &script) != -1) {
            if (script->scriptOverrides) {
                return 0;
            }
        }
    }

    if (inventoryCommonInit() == -1) {
        return 0;
    }

    muiLootInit();

    Object* hiddenBox = nullptr;
    if (objectCreateWithFrmIdProtoId(&hiddenBox, FrmId::Empty(), ItemProtoTypeId::JesseContainer) == -1) {
        return 0;
    }
    CritterEquipped stealTargetEquipped {};

    bool isoWasEnabled = _setup_inventory(INVENTORY_WINDOW_TYPE_LOOT);
    partyTargetEquipped = _gIsSteal ? &stealTargetEquipped : nullptr;
    partyBaseTarget = nullptr;
    setLootTarget(target, hiddenBox);
    if (!inventoryScreensWindowless()) {
        createPartySlotButtons();
    }

    // The window's arrows (the mobile UI's screen has its own).
    auto makeButton = [&](int x, int y, int keyCode) {
        if (inventoryScreensWindowless()) {
            return;
        }

        int upFrmId = INVENTORY_ARROW_FRM_LEFT_ARROW_UP;
        int downFrmId = INVENTORY_ARROW_FRM_LEFT_ARROW_DOWN;
        if (keyCode == KEY_ARROW_RIGHT || keyCode == KEY_PAGE_DOWN) {
            upFrmId = INVENTORY_ARROW_FRM_RIGHT_ARROW_UP;
            downFrmId = INVENTORY_ARROW_FRM_RIGHT_ARROW_DOWN;
        }
        int btn = buttonCreateWithFrm(gInventoryWindow,
            x, y, -1, -1, -1, keyCode,
            kInventoryArrowFrmIds[upFrmId],
            kInventoryArrowFrmIds[downFrmId]);
        if (btn != -1) {
            buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
        }
    };

    Object** critters = nullptr;
    int critterCount = 0;
    int critterIndex = 0;
    if (!_gIsSteal) {
        if (FrmId(target).objectType() == OBJ_TYPE_CRITTER) {
            critterCount = objectListCreate(target->tile, target->elevation, OBJ_TYPE_CRITTER, &critters);
            int endIndex = critterCount - 1;
            for (int index = 0; index < critterCount; index++) {
                Object* critter = critters[index];
                if ((critter->data.critter.combat.results & (DAM_DEAD | DAM_KNOCKED_OUT)) == 0) {
                    critters[index] = critters[endIndex];
                    critters[endIndex] = critter;
                    critterCount--;
                    index--;
                    endIndex--;
                } else {
                    critterIndex++;
                }
            }

            if (critterCount == 1) {
                objectListFree(critters);
                critterCount = 0;
            }

            if (critterCount > 1) {
                makeButton(inventoryLootLayout.prevCritterButtonX, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_PAGE_UP);
                makeButton(inventoryLootLayout.nextCritterButtonX, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_PAGE_DOWN);

                for (int index = 0; index < critterCount; index++) {
                    if (target == critters[index]) {
                        critterIndex = index;
                    }
                }
            }
        }
    }

    // Party member navigation: in normal looting left-side arrows and Left/Right
    // switch whose inventory is shown on the left pane. In party quick-loot,
    // Left/Right instead cycles the active right-side party target.
    Object* const playerObj = _inven_dude;
    FrmId savedDudeFrmId = gInventoryWindowDudeFrmId;
    bool switchActivePartyTarget = settings.qol.party_loot_and_barter
        && _gIsSteal
        && objectIsPartyMember(target);
    std::vector<Object*> partyTargets;
    if (switchActivePartyTarget) {
        for (Object* pm : get_all_party_members_objects(false)) {
            if (pm != _inven_dude && (pm == target || partyMemberTradesItems(pm))) {
                partyTargets.push_back(pm);
            }
        }
    } else if (settings.qol.party_loot_and_barter && (!_gIsSteal || objectIsPartyMember(target))) {
        partyTargets.push_back(_inven_dude);
        for (Object* pm : get_all_party_members_objects(false)) {
            if (pm != gDude && pm != target && partyMemberTradesItems(pm)) {
                partyTargets.push_back(pm);
            }
        }
    }

    int partyTargetIndex = 0;
    for (int index = 0; index < partyTargets.size(); index++) {
        if ((switchActivePartyTarget ? target : _inven_dude) == partyTargets[index]) {
            partyTargetIndex = index;
            break;
        }
    }

    // Shows party member [index] on the left side, or loots it when
    // stealing from party members.
    auto selectPartyMember = [&](int index) {
        partyTargetIndex = index;
        if (switchActivePartyTarget) {
            target = partyTargets[partyTargetIndex];
            setLootTarget(target, hiddenBox);
            refreshAfterTargetChange();
        } else {
            inventorySetLeftPaneCritter(partyTargets[partyTargetIndex], target, INVENTORY_WINDOW_TYPE_LOOT);
        }
    };

    if (switchActivePartyTarget && partyTargets.size() > 1) {
        makeButton(inventoryLootLayout.prevCritterButtonX, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_ARROW_LEFT);
        makeButton(inventoryLootLayout.nextCritterButtonX, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_ARROW_RIGHT);
    } else if (partyTargets.size() > 1) {
        const int btnCenterX = inventoryLootLayout.leftBodyViewX + INVENTORY_BODY_VIEW_WIDTH / 2;

        makeButton(btnCenterX - 20, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_ARROW_LEFT);
        makeButton(btnCenterX, INVENTORY_LOOT_CRITTER_TOGGLE_Y, KEY_ARROW_RIGHT);
    }

    gInventoryWindowDudeRotationTimestamp = 0;
    if (!inventoryScreensWindowless()) {
        _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
        _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
        _display_body(FrmId(target), INVENTORY_WINDOW_TYPE_LOOT);
        inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
    }

    // Trigger game mode change _after_ window and loot_obj are set up
    ScopedGameMode gm(GameMode::kLoot);

    bool isCaughtStealing = false;
    int stealingXp = 0;
    int stealingXpBonus = 10;

    // Next or previous dead critter at the tile.
    auto switchTarget = [&](int direction) {
        if (critterCount != 0) {
            critterIndex = inventoryWrapIndex(critterIndex, critterCount, direction);
            target = critters[critterIndex];
            setLootTarget(target, hiddenBox);
            refreshAfterTargetChange();
        }
    };

    // Stealing results of a move, as for moves with the mouse.
    auto countSteal = [&](InventoryMoveResult rc, int stealXpOverride) {
        if (rc == INVENTORY_MOVE_RESULT_CAUGHT_STEALING) {
            isCaughtStealing = true;
        } else if (rc == INVENTORY_MOVE_RESULT_SUCCESS) {
            stealingXp += stealXpOverride >= 0 ? stealXpOverride : stealingXpBonus;
            stealingXpBonus += 10;
        }
    };

    // CE: Actions of the mobile UI's loot screen.
    auto performLootActions = [&]() {
        std::vector<LootAction> actions;
        actions.swap(gLootActions);

        for (const LootAction& action : actions) {
            if (isCaughtStealing) {
                break;
            }

            static const char* const kLootActions[] = { "move", "action menu", "take all", "give all", "give to party member", "select party member", "switch target", "close container" };
            actionLog("%s: %s%s%s, index %d, left%s, right%s",
                _gIsSteal ? "steal" : "loot",
                kLootActions[static_cast<int>(action.type)],
                actionLogObject(action.item),
                action.fromTarget ? " from the right" : "",
                action.index,
                actionLogObject(_stack[0]),
                actionLogObject(_target_stack[0]));

            Object* rightOwner = _target_stack[_target_curr_stack];
            switch (action.type) {
            case LootActionType::Move: {
                int quantity = inventoryListQuantity(action.fromTarget ? _target_pud : _pud, action.item);
                if (quantity == 0) {
                    break;
                }

                _gStealCount += 1;
                _gStealSize += itemGetSize(_stack[_curr_stack]);

                int stealXpOverride = -1;
                InventoryMoveResult rc = inventoryLootTransfer(action.item, quantity, rightOwner, !action.fromTarget, true, &stealXpOverride);
                countSteal(rc, stealXpOverride);
                break;
            }
            case LootActionType::OpenActionMenu: {
                int keyCode = inventoryListKeyCode(action.item, action.fromTarget);
                if (keyCode != -1) {
                    inventoryRunItemActionMenu(keyCode, INVENTORY_WINDOW_TYPE_LOOT, action.x, action.y);
                }
                break;
            }
            case LootActionType::TakeAll: {
                if (_gIsSteal) {
                    break;
                }

                // Like the game's "Take all" of the shown items: all of them
                // if they fit.
                int weight = 0;
                for (Object* item : action.items) {
                    weight += itemGetWeight(item) * inventoryListQuantity(_target_pud, item);
                }

                int maxCarryWeight = critterGetStat(_inven_dude, STAT_CARRY_WEIGHT);
                int currentWeight = objectGetInventoryWeight(_inven_dude);
                if (weight <= maxCarryWeight - currentWeight) {
                    soundPlayFile("ib1p1xx1");
                    for (Object* item : action.items) {
                        int quantity = inventoryListQuantity(_target_pud, item);
                        if (quantity != 0) {
                            itemMove(rightOwner, _inven_dude, item, quantity);
                        }
                    }
                } else {
                    // Sorry, you cannot carry that much.
                    MessageListItem carryMessageListItem;
                    carryMessageListItem.num = 31;
                    if (messageListGetItem(&gInventoryMessageList, &carryMessageListItem)) {
                        showDialogBox(carryMessageListItem.text, nullptr, 0, 169, 117, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
                    }
                }
                break;
            }
            case LootActionType::GiveAll:
                if (_gIsSteal) {
                    break;
                }

                for (Object* item : action.items) {
                    int quantity = inventoryListQuantity(_pud, item);
                    if (quantity != 0 && itemMove(_inven_dude, rightOwner, item, quantity) == -1) {
                        inventoryDisplayMessage(26); // There is no space left for that item.
                        break;
                    }
                }
                break;
            case LootActionType::GiveToPartyMember: {
                if (switchActivePartyTarget || action.index < 0 || action.index >= static_cast<int>(partyTargets.size())) {
                    break;
                }

                Object* member = partyTargets[action.index];
                int quantity = inventoryListQuantity(_pud, action.item);
                if (member == _inven_dude || quantity == 0 || !inventoryPartyMemberIsReachable(member)) {
                    break;
                }

                int quantityToMove = quantity > 1 ? inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, action.item, quantity) : 1;
                if (quantityToMove == -1) {
                    break;
                }

                // Weight is checked, like when the item is given in party
                // member's barter.
                if (itemMove(_inven_dude, member, action.item, quantityToMove) == -1) {
                    // Sorry, that's too much to carry.
                    inventoryDisplayMessage(32);
                } else {
                    soundPlayFile("iputdown");
                }
                break;
            }
            case LootActionType::SelectPartyMember:
                if (action.index >= 0 && action.index < static_cast<int>(partyTargets.size()) && action.index != partyTargetIndex
                    && inventoryPartyMemberIsReachable(partyTargets[action.index])) {
                    selectPartyMember(action.index);
                }
                break;
            case LootActionType::SwitchTarget:
                switchTarget(action.index);
                break;
            case LootActionType::CloseContainer:
                _container_exit(action.fromTarget ? 2501 : 2500, INVENTORY_WINDOW_TYPE_LOOT);
                break;
            }
        }
    };

    gLootViewActive = true;
    gLootPartyTargets = &partyTargets;
    gLootPartyTargetIndex = &partyTargetIndex;
    gLootPartySwitchesTarget = switchActivePartyTarget;
    gLootActions.clear();
    gLootCloseRequested = false;

    if (inventoryScreensWindowless()) {
        // CE: The mobile UI's loot screen shows the sides (see
        // `lootGetView`); the loop runs its actions (`lootQueueAction`) with
        // the functions the window loop runs.
        for (;;) {
            gLootTargetCount = critterCount;
            gLootTargetIndex = critterIndex;

            devAutotestTick();

            sharedFpsLimiter.mark();

            if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
                break;
            }

            if (isCaughtStealing) {
                break;
            }

            int keyCode = inputGetInput();

            if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X || keyCode == KEY_F10) {
                showQuitConfirmationDialog();
            }

            if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
                break;
            }

            if (!gLootActions.empty()) {
                performLootActions();
            }

            // A desktop keyboard: Esc closes, as in the game.
            if (keyCode == KEY_ESCAPE || gLootCloseRequested) {
                break;
            }

            renderPresent();
            sharedFpsLimiter.throttle();
        }
    } else {
        // The game's loot: its window, the mouse and the keyboard.
        for (;;) {
            gLootTargetCount = critterCount;
            gLootTargetIndex = critterIndex;

            devAutotestTick();

            sharedFpsLimiter.mark();

            if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
                break;
            }

            if (isCaughtStealing) {
                break;
            }

            int keyCode = inputGetInput();
            MouseButtonEvent mouseEvent = mouseGetEvent();
            InventoryScrollerDisplayContext inventoryScrollerContext { INVENTORY_WINDOW_TYPE_LOOT, nullptr };
            InventoryScrollerDisplayContext targetScrollerContext { INVENTORY_WINDOW_TYPE_LOOT, _target_pud };
            InventoryScroller lootInventoryScroller {
                { inventoryLootLayout.leftScrollerX, inventoryLootLayout.leftScrollerY, inventoryLootLayout.leftScrollerX + inventoryLootLayout.scrollerWidth, inventoryLootLayout.leftScrollerY + inventoryLootLayout.scrollerHeight },
                &(_stack_offset[_curr_stack]),
                inventoryLootLayout.columns,
                gInventorySlotsCount,
                _pud->length,
                KEY_ARROW_UP,
                KEY_ARROW_DOWN,
                -1,
                -1,
                -1,
                -1,
                inventoryScrollerRedrawInventory,
                &inventoryScrollerContext,
                false,
            };
            InventoryScroller lootTargetScroller {
                { inventoryLootLayout.rightScrollerX, inventoryLootLayout.rightScrollerY, inventoryLootLayout.rightScrollerX + inventoryLootLayout.scrollerWidth, inventoryLootLayout.rightScrollerY + inventoryLootLayout.scrollerHeight },
                &(_target_stack_offset[_target_curr_stack]),
                inventoryLootLayout.columns,
                gInventorySlotsCount,
                _target_pud->length,
                KEY_CTRL_ARROW_UP,
                KEY_CTRL_ARROW_DOWN,
                -1,
                -1,
                -1,
                -1,
                inventoryScrollerRedrawTargetInventory,
                &targetScrollerContext,
                true,
            };

            if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X || keyCode == KEY_F10) {
                showQuitConfirmationDialog();
            }

            if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
                break;
            }

            if (keyCode == 2502 || keyCode == KEY_LOWERCASE_A) {
                if (!_gIsSteal) {
                    if (keyCode == KEY_LOWERCASE_A) {
                        soundPlayFile("ib1p1xx1");
                    }
                    int maxCarryWeight = critterGetStat(_inven_dude, STAT_CARRY_WEIGHT);
                    int currentWeight = objectGetInventoryWeight(_inven_dude);
                    int newInventoryWeight = objectGetInventoryWeight(target);
                    if (newInventoryWeight <= maxCarryWeight - currentWeight) {
                        itemMoveAll(target, _inven_dude);
                        _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
                        _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
                    } else {
                        // Sorry, you cannot carry that much.
                        messageListItem.num = 31;
                        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                            showDialogBox(messageListItem.text, nullptr, 0, 169, 117, COLOR_AMBER, nullptr, COLOR_AMBER, 0);
                        }
                    }
                }

                // change selected party member
            } else if (keyCode == KEY_ARROW_LEFT || keyCode == KEY_ARROW_RIGHT) {
                int next = partyTargetStep(partyTargets, partyTargetIndex, keyCode == KEY_ARROW_LEFT ? -1 : 1);
                if (next != partyTargetIndex) {
                    selectPartyMember(next);
                }
            } else if (keyCode == KEY_PAGE_UP) {
                switchTarget(-1);
            } else if (keyCode == KEY_PAGE_DOWN) {
                switchTarget(1);
            } else if (keyCode >= 2500 && keyCode <= 2501) {
                _container_exit(keyCode, INVENTORY_WINDOW_TYPE_LOOT);
            } else {
                inventoryScrollerHandleInput(lootInventoryScroller, keyCode, mouseEvent);
                inventoryScrollerHandleInput(lootTargetScroller, keyCode, mouseEvent);

                if ((mouseEvent & MouseButtonEvent::RightButtonDown) != MouseButtonEvent::None) {
                    if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_HAND) {
                        inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
                    } else {
                        inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
                    }
                } else if ((mouseEvent & MouseButtonEvent::LeftButtonDown) != MouseButtonEvent::None) {
                    if (keyCode >= 1000 && keyCode < 1000 + gInventorySlotsCount) {
                        if (inventoryUsesContextMenu()) {
                            inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_LOOT);
                        } else {
                            int slotIndex = keyCode - 1000;
                            if (slotIndex + _stack_offset[_curr_stack] < _pud->length) {
                                _gStealCount += 1;
                                _gStealSize += itemGetSize(_stack[_curr_stack]);

                                InventoryItem* inventoryItem = &(_pud->items[_pud->length - (slotIndex + _stack_offset[_curr_stack] + 1)]);
                                int stealXpOverride = -1;
                                InventoryMoveResult rc = _move_inventory(inventoryItem->item, slotIndex, _target_stack[_target_curr_stack], true, &stealXpOverride);
                                if (rc == INVENTORY_MOVE_RESULT_CAUGHT_STEALING) {
                                    isCaughtStealing = true;
                                } else if (rc == INVENTORY_MOVE_RESULT_SUCCESS) {
                                    stealingXp += stealXpOverride >= 0 ? stealXpOverride : stealingXpBonus;
                                    stealingXpBonus += 10;
                                }

                                _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
                                _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
                                _display_body(getTargetDisplayFrmId(), INVENTORY_WINDOW_TYPE_LOOT);
                            }

                            keyCode = -1;
                        }
                    } else if (keyCode >= 2000 && keyCode < 2000 + gInventorySlotsCount) {
                        if (inventoryUsesContextMenu()) {
                            inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_LOOT);
                        } else {
                            int slotIndex = keyCode - 2000;
                            if (slotIndex + _target_stack_offset[_target_curr_stack] < _target_pud->length) {
                                _gStealCount += 1;
                                _gStealSize += itemGetSize(_stack[_curr_stack]);

                                InventoryItem* inventoryItem = &(_target_pud->items[_target_pud->length - (slotIndex + _target_stack_offset[_target_curr_stack] + 1)]);
                                int stealXpOverride = -1;
                                InventoryMoveResult rc = _move_inventory(inventoryItem->item, slotIndex, _target_stack[_target_curr_stack], false, &stealXpOverride);
                                if (rc == INVENTORY_MOVE_RESULT_CAUGHT_STEALING) {
                                    isCaughtStealing = true;
                                } else if (rc == INVENTORY_MOVE_RESULT_SUCCESS) {
                                    stealingXp += stealXpOverride >= 0 ? stealXpOverride : stealingXpBonus;
                                    stealingXpBonus += 10;
                                }

                                _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
                                _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_LOOT);
                                _display_body(getTargetDisplayFrmId(), INVENTORY_WINDOW_TYPE_LOOT);
                            }
                        }
                    } else if ((keyCode == PARTY_WEAPON_SLOT_KEY || keyCode == PARTY_ARMOR_SLOT_KEY)
                        && hasPartySlots()) {
                        if (inventoryUsesContextMenu()) {
                            inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_LOOT);
                        } else {
                            handlePartySlotPickup(keyCode == PARTY_WEAPON_SLOT_KEY ? InvenSlot::RightHand : InvenSlot::Armor);
                        }
                    }
                }
            }

            if (keyCode == KEY_ESCAPE) {
                break;
            }

            renderPresent();
            sharedFpsLimiter.throttle();
        }
    }

    gLootCloseRequested = false;
    gLootViewActive = false;
    gLootPartyTargets = nullptr;
    gLootPartyTargetIndex = nullptr;
    gLootActions.clear();

    if (critterCount != 0) {
        objectListFree(critters);
    }

    itemsResetTakenOut();

    if (_gIsSteal) {
        critterRestoreEquipped(target, stealTargetEquipped);
    }

    itemMoveAll(hiddenBox, target);
    objectDestroy(hiddenBox, nullptr);

    if (_gIsSteal && !isCaughtStealing && stealingXp > 0 && !objectIsPartyMember(target)) {
        stealingXp = std::min(300 - skillGetValue(looter, SKILL_STEAL), stealingXp);
        debugPrint("\n[[[%d]]]", 300 - skillGetValue(looter, SKILL_STEAL));

        // SFALL: Display actual xp received.
        int xpGained;
        pcAddExperience(stealingXp, &xpGained);

        // You gain %d experience points for successfully using your Steal skill.
        messageListItem.num = 29;
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            char formattedText[200];
            snprintf(formattedText, sizeof(formattedText), messageListItem.text, xpGained);
            displayMonitorAddMessage(formattedText);
        }
    }

    partyTargetEquipped = nullptr;
    partyBaseTarget = nullptr;
    lootHiddenBox = nullptr;
    gInventoryWindowDudeFrmId = savedDudeFrmId;

    _exit_inventory(isoWasEnabled);
    _inven_dude = playerObj;

    // NOTE: Uninline.
    inventoryCommonFree();

    if (_gIsSteal && isCaughtStealing && _gStealCount > 0 && objectGetSid(target, &sid) != -1) {
        scriptSetObjects(sid, looter, nullptr);
        scriptExecProc(sid, SCRIPT_PROC_PICKUP);

        // TODO: Looks like inlining, script is not used.
        Script* script;
        scriptGetScript(sid, &script);
    }

    return 0;
}

// 0x4746A0
int inventoryOpenStealing(Object* thief, Object* target)
{
    if (thief == target) {
        return -1;
    }

    _gIsSteal = ProtoId(thief).objectType() == OBJ_TYPE_CRITTER && critterIsActive(target);
    _gStealCount = 0;
    _gStealSize = 0;

    int rc = inventoryOpenLooting(thief, target);

    _gIsSteal = 0;
    _gStealCount = 0;
    _gStealSize = 0;

    return rc;
}

// 0x474708
// note: this is looting and stealing, not the inventory screen
static InventoryMoveResult _move_inventory(Object* item, int slotIndex, Object* targetObj, bool isPlanting, int* stealXpOverridePtr)
{
    assert(stealXpOverridePtr != nullptr);
    *stealXpOverridePtr = -1;

    bool needRefresh = true;

    Rect rect;

    int quantity;
    if (isPlanting) {
        rect.left = inventoryLootGetSlotX(false, slotIndex);
        rect.top = inventoryLootGetSlotY(slotIndex);

        InventoryItem* inventoryItem = &(_pud->items[_pud->length - (slotIndex + _stack_offset[_curr_stack] + 1)]);
        quantity = inventoryItem->quantity;
        if (quantity > 1) {
            _display_inventory(_stack_offset[_curr_stack], slotIndex, INVENTORY_WINDOW_TYPE_LOOT);
            needRefresh = false;
        }
    } else {
        rect.left = inventoryLootGetSlotX(true, slotIndex);
        rect.top = inventoryLootGetSlotY(slotIndex);

        InventoryItem* inventoryItem = &(_target_pud->items[_target_pud->length - (slotIndex + _target_stack_offset[_target_curr_stack] + 1)]);
        quantity = inventoryItem->quantity;
        if (quantity > 1) {
            _display_target_inventory(_target_stack_offset[_target_curr_stack], slotIndex, _target_pud, INVENTORY_WINDOW_TYPE_LOOT);
            windowRefresh(gInventoryWindow);
            needRefresh = false;
        }
    }

    if (needRefresh && inventoryHasWindow()) {
        unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);

        unsigned char* backgroundData = inventoryLootFrmImage.getData();
        int backgroundWidth = inventoryLootFrmImage.getWidth();
        if (backgroundData != nullptr) {
            blitBufferToBuffer(backgroundData + backgroundWidth * rect.top + rect.left,
                INVENTORY_SLOT_WIDTH,
                INVENTORY_SLOT_HEIGHT,
                backgroundWidth,
                windowBuffer + inventoryLootLayout.windowWidth * rect.top + rect.left,
                inventoryLootLayout.windowWidth);
        }

        rect.right = rect.left + INVENTORY_SLOT_WIDTH - 1;
        rect.bottom = rect.top + INVENTORY_SLOT_HEIGHT - 1;
        windowRefreshRect(gInventoryWindow, &rect);
    }

    bool immediate = _ctrl_pressed();
    _drag_item_loop(item, immediate);

    InventoryMoveResult result = INVENTORY_MOVE_RESULT_FAILED;

    if (isPlanting) {
        if (!immediate && tryEquipPartyItem(item, true)) {
            result = INVENTORY_MOVE_RESULT_SUCCESS;
        } else if (immediate || inventoryLootMouseHitTestScroller(true)) {
            result = inventoryLootTransfer(item, quantity, targetObj, true, !immediate, stealXpOverridePtr);
        }
    } else {
        if (!immediate && tryEquipPartyItem(item, false)) {
            result = INVENTORY_MOVE_RESULT_SUCCESS;
        } else if (immediate || inventoryLootMouseHitTestScroller(false)) {
            result = inventoryLootTransfer(item, quantity, targetObj, false, !immediate, stealXpOverridePtr);
        }
    }

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);

    return result;
}

// CE: Moves [item] ([quantity] of the stack, [askQuantity] - lets the player
// choose) from the left side of the loot window to [targetObj]
// ([isPlanting]) or back, with stealing checks. Mouse dragging and the mobile
// UI loot screen share it.
static InventoryMoveResult inventoryLootTransfer(Object* item, int quantity, Object* targetObj, bool isPlanting, bool askQuantity, int* stealXpOverridePtr)
{
    *stealXpOverridePtr = -1;

    InventoryMoveResult result = INVENTORY_MOVE_RESULT_FAILED;

    int quantityToMove = quantity;
    if (quantity > 1 && askQuantity) {
        quantityToMove = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, quantity);
    }

    if (quantityToMove == -1) {
        return result;
    }

    bool skipMove = false;
    if (_gIsSteal && _inven_dude == gDude) {
        SkillStealResult stealResult = skillsPerformStealing(_inven_dude, targetObj, item, quantityToMove, isPlanting, stealXpOverridePtr);
        if (stealResult == SkillStealResult::Caught) {
            result = INVENTORY_MOVE_RESULT_CAUGHT_STEALING;
        } else if (stealResult == SkillStealResult::Fail) {
            skipMove = true;
        }
    }

    if (skipMove || result == INVENTORY_MOVE_RESULT_CAUGHT_STEALING) {
        return result;
    }

    if (isPlanting) {
        if (itemMove(_inven_dude, targetObj, item, quantityToMove) != -1) {
            result = INVENTORY_MOVE_RESULT_SUCCESS;
        } else {
            inventoryDisplayMessage(26); // There is no space left for that item.
        }
    } else {
        if (itemMove(targetObj, _inven_dude, item, quantityToMove) == 0) {
            if ((item->flags & OBJECT_IN_RIGHT_HAND) != OBJECT_NONE) {
                targetObj->fid = FrmId(targetObj, WeaponAnimation::None, targetObj->rotation + 1).fid();
            }

            targetObj->flags &= ~OBJECT_EQUIPPED;

            result = INVENTORY_MOVE_RESULT_SUCCESS;
        } else {
            inventoryDisplayMessage(25); // You cannot pick that up. You are at your maximum weight capacity.
        }
    }

    return result;
}

// TODO: move barter related code to separate file

// Barter modifier (with Master Trader) and skills ratio of trader's prices.
static void barterGetPriceFactors(Object* dude, Object* npc, double* barterModMult, double* skillRatio)
{
    double perkBonus = 0.0;
    if (dude == gDude) {
        if (perkHasRank(gDude, PERK_MASTER_TRADER)) {
            perkBonus = perkGetMasterTraderBonus();
        }
    }

    const int partyBarter = partyGetBestSkillValue(SKILL_BARTER);
    const int npcBarter = skillGetValue(npc, SKILL_BARTER);

    // TODO: Check in debugger, complex math, probably uses floats, not doubles.
    *barterModMult = (gBarterFinalModifier + 100.0 - perkBonus) * 0.01;
    *skillRatio = (160.0 + npcBarter) / (160.0 + partyBarter);
    if (*barterModMult < 0) {
        // TODO: Probably 0.01 as float.
        *barterModMult = 0.0099999998;
    }
}

// Calculates value of NPC/barterer (request) and player (offer) tables.
//
// 0x474B2C barter_compute_value
static std::pair<int, int> barterComputeTablesValue(Object* dude, Object* npc, bool offerButton)
{
    assert(!gGameDialogSpeakerIsPartyMember);

    const int rawValue = objectGetCost(gBartererTableObj);
    const int caps = itemGetTotalCaps(gBartererTableObj);
    const int offerValue = objectGetCost(gPlayerTableObj);

    BarterPriceContext ctx { dude, npc, gBartererTableObj, gPlayerTableObj, 0, offerValue, rawValue, caps, offerButton, false };

    const int valueMinusCaps = rawValue - caps;
    double barterModMult;
    double skillRatio;
    barterGetPriceFactors(dude, npc, &barterModMult, &skillRatio);
    const double balancedCost = skillRatio * (valueMinusCaps * 2.0);

    ctx.value = static_cast<int>(barterModMult * balancedCost + caps);
    scriptHooks_BarterPrice(&ctx);

    return { ctx.value, ctx.offerValue };
}

// Calculates weight of NPC (request) and player (offer) tables. Used when bartering with party members.
static std::pair<int, int> barterComputeTablesWeight(Object* dude, Object* npc)
{
    assert(gGameDialogSpeakerIsPartyMember);
    const int rawValue = objectGetCost(gBartererTableObj);
    const int caps = itemGetTotalCaps(gBartererTableObj);
    const int offerValue = objectGetCost(gPlayerTableObj);

    BarterPriceContext ctx { dude, npc, gBartererTableObj, gPlayerTableObj, 0, offerValue, rawValue, caps, false, true };

    // Hook is invoked but return values are not used. This matches sfall behavior.
    scriptHooks_BarterPrice(&ctx);

    return { objectGetInventoryWeight(gBartererTableObj), objectGetInventoryWeight(gPlayerTableObj) };
}

// CE: State of the running barter loop for the mobile UI barter screen.
static Object* gBarterBarterer = nullptr;
static std::vector<Object*>* gBarterPartyTargets = nullptr;
static int* gBarterPartyTargetIndex = nullptr;
static std::vector<BarterAction> gBarterActions;
static BarterRequest gBarterRequest = BarterRequest::None;

// Quantity of [item] in [owner]'s inventory, 0 if it's not there.
static int barterItemQuantity(Object* owner, Object* item)
{
    Inventory* inventory = &(owner->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        if (inventory->items[index].item == item) {
            return inventory->items[index].quantity;
        }
    }
    return 0;
}

bool barterGetView(BarterView* view)
{
    if (gBarterBarterer == nullptr || gBarterPartyTargets == nullptr) {
        return false;
    }

    view->dude = _inven_dude;
    view->playerTable = gPlayerTableObj;
    view->barterer = gBarterBarterer;
    view->bartererTable = gBartererTableObj;
    view->partyMemberBarter = gGameDialogSpeakerIsPartyMember;
    if (gGameDialogSpeakerIsPartyMember) {
        auto [requestWeight, offerWeight] = barterComputeTablesWeight(_inven_dude, gBarterBarterer);
        view->requestValue = requestWeight;
        view->offerValue = offerWeight;
    } else {
        auto [requestValue, offerValue] = barterComputeTablesValue(_inven_dude, gBarterBarterer);
        view->requestValue = requestValue;
        view->offerValue = offerValue;
    }
    view->party = *gBarterPartyTargets;
    view->partyIndex = *gBarterPartyTargetIndex;
    return true;
}

void barterQueueAction(const BarterAction& action)
{
    gBarterActions.push_back(action);
}

void barterRequest(BarterRequest request)
{
    gBarterRequest = request;
}

// Performs actions queued by the barter screen with the same moves, quantity
// prompts and messages as dragging items in the game's barter window.
// Quantity of [item] in [inventory], 0 if it's not there.
static int inventoryListQuantity(Inventory* inventory, Object* item)
{
    for (int index = 0; index < inventory->length; index++) {
        if (inventory->items[index].item == item) {
            return inventory->items[index].quantity;
        }
    }
    return 0;
}

// Key code of the loot window button for [item] of the left side or the
// right one ([target]). The screen doesn't scroll the game's lists, items are
// counted from their start. -1 if the item is not there anymore.
static int inventoryListKeyCode(Object* item, bool target)
{
    Inventory* inventory;
    int base;
    int limit;
    if (target) {
        _target_stack_offset[_target_curr_stack] = 0;
        inventory = _target_pud;
        base = 2000;
        limit = 300;
    } else {
        _stack_offset[_curr_stack] = 0;
        inventory = _pud;
        base = 1000;
        limit = 1000;
    }

    for (int index = 0; index < inventory->length && index < limit; index++) {
        if (inventory->items[inventory->length - (index + 1)].item == item) {
            return base + index;
        }
    }
    return -1;
}

bool lootGetView(LootView* view)
{
    if (!gLootViewActive) {
        return false;
    }

    view->looter = _stack[0];
    view->leftOwner = _stack[_curr_stack];
    view->leftContainer = _curr_stack > 0 ? _stack[_curr_stack] : nullptr;
    view->target = _target_stack[0];
    view->rightOwner = _target_stack[_target_curr_stack];
    view->rightContainer = _target_curr_stack > 0 ? _target_stack[_target_curr_stack] : nullptr;
    view->steal = _gIsSteal;
    view->party = *gLootPartyTargets;
    view->partyIndex = *gLootPartyTargetIndex;
    view->partySwitchesTarget = gLootPartySwitchesTarget;
    view->targetCount = gLootTargetCount;
    view->targetIndex = gLootTargetIndex;
    view->weight = inventoryGetCritterWeight();
    view->carryWeight = ProtoId(_stack[0]).objectType() == OBJ_TYPE_CRITTER ? critterGetStat(_stack[0], STAT_CARRY_WEIGHT) : 0;
    return true;
}

void lootQueueAction(const LootAction& action)
{
    gLootActions.push_back(action);
}

void lootRequestClose()
{
    gLootCloseRequested = true;
}

// Key code of the trade window button for [item] (functions shared with the
// mouse take them). The screen doesn't scroll the game's lists, items are
// counted from their start. -1 if the item is not there anymore.
static int barterItemKeyCode(Object* item, bool playerSide, bool onTable)
{
    Inventory* inventory;
    int base;
    int limit;
    if (onTable) {
        if (playerSide) {
            gPlayerTableOffset = 0;
            inventory = gPlayerTableInventory;
            base = 2300;
        } else {
            gBartererTableOffset = 0;
            inventory = gBartererTableInventory;
            base = 2400;
        }
        limit = 100;
    } else if (playerSide) {
        _stack_offset[_curr_stack] = 0;
        inventory = _pud;
        base = 1000;
        limit = 1000;
    } else {
        _target_stack_offset[_target_curr_stack] = 0;
        inventory = _target_pud;
        base = 2000;
        limit = 300;
    }

    for (int index = 0; index < inventory->length && index < limit; index++) {
        if (inventory->items[inventory->length - (index + 1)].item == item) {
            return base + index;
        }
    }
    return -1;
}

int barterGetItemPrice(Object* item, int quantity, bool playerSide)
{
    // Of the item (`objectGetCost` is the cost of what is inside it).
    int cost = itemStackCost(item, quantity);
    if (gBarterBarterer == nullptr || playerSide || ProtoId(item) == ItemProtoTypeId::Money) {
        return cost;
    }

    double barterModMult;
    double skillRatio;
    barterGetPriceFactors(_inven_dude, gBarterBarterer, &barterModMult, &skillRatio);
    return static_cast<int>(barterModMult * (skillRatio * (cost * 2.0)));
}

// CE: Who put what on the player's barter table. With `[qol]
// party_loot_and_barter` party members' items go there too, and the game,
// knowing only the dude's, gave whatever was left there to him when the
// barter ended (overloading him with the others' items). They go back to
// whoever put them; what a party member can't carry anymore (the player gave
// him more meanwhile) goes to the dude, as everything did in the game, so
// overloading can't be shifted onto party members. Counted by pid: equal
// items stack into one object on the table.
struct BarterTableShare {
    Object* owner;
    int pid;
    int quantity;
};

static std::vector<BarterTableShare> gBarterTableShares;

static int barterTableShareOf(Object* owner, int pid)
{
    for (const BarterTableShare& share : gBarterTableShares) {
        if (share.owner == owner && share.pid == pid) {
            return share.quantity;
        }
    }
    return 0;
}

// [taker] took [quantity] of [pid] off the table: from its own share first.
static void barterTableSharesTake(Object* taker, int pid, int quantity)
{
    for (int pass = 0; pass < 2 && quantity > 0; pass++) {
        for (BarterTableShare& share : gBarterTableShares) {
            if (share.pid == pid && (share.owner == taker) == (pass == 0)) {
                int taken = std::min(quantity, share.quantity);
                share.quantity -= taken;
                quantity -= taken;
            }
        }
    }

    gBarterTableShares.erase(std::remove_if(gBarterTableShares.begin(), gBarterTableShares.end(), [](const BarterTableShare& share) {
        return share.quantity <= 0;
    }),
        gBarterTableShares.end());
}

// A player side item (of the critter on the left, or of its open bag) to the
// player's table.
static int barterMovePlayerItemToTable(Object* item, int quantity, Object* playerTable)
{
    int pid = item->pid;
    if (itemMoveForce(_inven_dude, playerTable, item, quantity) == -1) {
        return -1;
    }

    Object* owner = _stack[0];
    for (BarterTableShare& share : gBarterTableShares) {
        if (share.owner == owner && share.pid == pid) {
            share.quantity += quantity;
            return 0;
        }
    }
    gBarterTableShares.push_back({ owner, pid, quantity });
    return 0;
}

// Back from the player's table to the left side: its own items as they
// went (unchecked, as in the game), the others' as any item given (weight
// checked).
static int barterMovePlayerItemFromTable(Object* item, int quantity, Object* playerTable)
{
    int pid = item->pid;
    Object* taker = _stack[0];
    int rc = barterTableShareOf(taker, pid) >= quantity
        ? itemMoveForce(playerTable, _inven_dude, item, quantity)
        : itemMove(playerTable, _inven_dude, item, quantity);
    if (rc == 0) {
        barterTableSharesTake(taker, pid, quantity);
    }
    return rc;
}

static Object* barterTableFindPid(Object* table, int pid)
{
    Inventory* inventory = &(table->data.inventory);
    for (int index = 0; index < inventory->length; index++) {
        if (inventory->items[index].item->pid == pid) {
            return inventory->items[index].item;
        }
    }
    return nullptr;
}

// The barter is over: what is left on the player's table goes back (see
// above). Equipment is back on its critters by now (weights are whole).
static void barterReturnPlayerTable(Object* playerTable)
{
    for (const BarterTableShare& share : gBarterTableShares) {
        int remaining = share.quantity;
        while (remaining > 0) {
            Object* item = barterTableFindPid(playerTable, share.pid);
            if (item == nullptr) {
                break;
            }

            int quantity = std::min(remaining, itemGetQuantity(playerTable, item));
            int returned = 0;
            int unitWeight = itemGetWeight(item);
            int freeWeight = 0;
            if (share.owner != gDude) {
                freeWeight = critterGetStat(share.owner, STAT_CARRY_WEIGHT) - objectGetInventoryWeight(share.owner);
                int fits = unitWeight > 0 ? std::clamp(freeWeight / unitWeight, 0, quantity) : quantity;
                if (fits > 0 && itemMove(playerTable, share.owner, item, fits) == 0) {
                    returned = fits;
                }
            }

            if (returned < quantity) {
                // A stack moved in part leaves a new object on the table.
                Object* rest = barterTableFindPid(playerTable, share.pid);
                if (rest != nullptr) {
                    itemMoveForce(playerTable, gDude, rest, quantity - returned);
                }
            }

            if (share.owner != gDude) {
                actionLog("barter table: pid %d (weight %d) x%d back to%s (could carry %d more), x%d to the dude",
                    share.pid, unitWeight, returned, actionLogObject(share.owner), freeWeight, quantity - returned);
            }
            remaining -= quantity;
        }
    }
    gBarterTableShares.clear();

    // Anything else (the game's way).
    itemMoveAll(playerTable, gDude);
}

static void barterPerformQueuedActions(Object* barterer, Object* playerTable, Object* bartererTable)
{
    std::vector<BarterAction> actions;
    actions.swap(gBarterActions);

    for (const BarterAction& action : actions) {
        static const char* const kBarterActions[] = { "to table", "from table", "give to party member", "select party member", "action menu" };
        actionLog("barter: %s%s, %s side, party index %d, left%s, trader%s",
            kBarterActions[static_cast<int>(action.type)],
            actionLogObject(action.item),
            action.playerSide ? "player" : "trader",
            action.partyIndex,
            actionLogObject(_stack[0]),
            actionLogObject(barterer));

        switch (action.type) {
        case BarterActionType::MoveToTable:
        case BarterActionType::MoveFromTable: {
            bool toTable = action.type == BarterActionType::MoveToTable;
            Object* owner = action.playerSide ? _inven_dude : barterer;
            Object* table = action.playerSide ? playerTable : bartererTable;
            Object* from = toTable ? owner : table;
            Object* to = toTable ? table : owner;

            int quantity = barterItemQuantity(from, action.item);
            if (quantity == 0) {
                break;
            }

            int quantityToMove = barterGetMovedQuantity(action.item, quantity, action.playerSide, toTable, false);
            if (quantityToMove == -1) {
                break;
            }

            int rc;
            if (action.playerSide) {
                rc = toTable
                    ? barterMovePlayerItemToTable(action.item, quantityToMove, playerTable)
                    : barterMovePlayerItemFromTable(action.item, quantityToMove, playerTable);
            } else {
                rc = itemMoveForce(from, to, action.item, quantityToMove);
            }

            if (rc == -1) {
                // There is no space left for that item. / You cannot pick that
                // up. You are at your maximum weight capacity.
                inventoryDisplayMessage(action.playerSide && toTable ? 26 : 25);
            }
            soundPlayFile("iputdown");
            break;
        }
        case BarterActionType::GiveToPartyMember: {
            if (action.partyIndex < 0 || action.partyIndex >= static_cast<int>(gBarterPartyTargets->size())) {
                break;
            }

            Object* member = (*gBarterPartyTargets)[action.partyIndex];
            int quantity = barterItemQuantity(_inven_dude, action.item);
            if (member == _inven_dude || quantity == 0 || !inventoryPartyMemberIsReachable(member)) {
                break;
            }

            int quantityToMove = quantity > 1 ? inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, action.item, quantity) : 1;
            if (quantityToMove == -1) {
                break;
            }

            // Weight is checked, like when the item is given in party
            // member's barter.
            if (itemMove(_inven_dude, member, action.item, quantityToMove) == -1) {
                // Sorry, that's too much to carry.
                inventoryDisplayMessage(32);
            } else {
                soundPlayFile("iputdown");
            }
            break;
        }
        case BarterActionType::SelectPartyMember:
            if (action.partyIndex >= 0 && action.partyIndex < static_cast<int>(gBarterPartyTargets->size())
                && action.partyIndex != *gBarterPartyTargetIndex
                && inventoryPartyMemberIsReachable((*gBarterPartyTargets)[action.partyIndex])) {
                *gBarterPartyTargetIndex = action.partyIndex;
                inventorySetLeftPaneCritter((*gBarterPartyTargets)[action.partyIndex], barterer, INVENTORY_WINDOW_TYPE_TRADE);
            }
            break;
        case BarterActionType::OpenActionMenu: {
            int keyCode = barterItemKeyCode(action.item, action.playerSide, action.onTable);
            if (keyCode != -1) {
                inventoryRunItemActionMenu(keyCode, INVENTORY_WINDOW_TYPE_TRADE, action.x, action.y);
            }
            break;
        }
        }
    }
}

// 0x474C50 barter_attempt_transaction
static int barterAttemptTransaction(Object* dude, Object* offerTable, Object* npc, Object* barterTable)
{
    MessageListItem messageListItem;

    // SFALL: Fix for being unable to sell/give items in the barter screen when
    // the player/party member is overloaded: a table that weighs nothing (the
    // player or the party member takes nothing) always fits.
    int weightAvailable = critterGetStat(dude, STAT_CARRY_WEIGHT) - objectGetInventoryWeight(dude);
    int barterTableWeight = objectGetInventoryWeight(barterTable);
    if (barterTableWeight != 0 && barterTableWeight > weightAvailable) {
        // Sorry, you cannot carry that much.
        messageListItem.num = 31;
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            gameDialogRenderSupplementaryMessage(messageListItem.text);
        }
        return -1;
    }

    if (gGameDialogSpeakerIsPartyMember) {
        int npcWeightAvailable = critterGetStat(npc, STAT_CARRY_WEIGHT) - objectGetInventoryWeight(npc);
        int offerTableWeight = objectGetInventoryWeight(offerTable);
        if (offerTableWeight != 0 && offerTableWeight > npcWeightAvailable) {
            // Sorry, that's too much to carry.
            messageListItem.num = 32;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                gameDialogRenderSupplementaryMessage(messageListItem.text);
            }
            return -1;
        }
    } else {
        bool badOffer = false;
        if (offerTable->data.inventory.length == 0) {
            badOffer = true;
        } else {
            if (itemCanSellUsedGeiger()) {
                // SFALL: Fix for being unable to sell used geiger counters or
                // stealth boys: active ones in the offer are turned off. The
                // original checked the offer table's own pid, so nothing
                // active could be sold.
                Object* queuedItem;
                while ((queuedItem = itemGetQueued(offerTable)) != nullptr) {
                    ProtoId queuedItemProtoId = ProtoId(queuedItem);
                    if (queuedItemProtoId != ItemProtoTypeId::ActivatedGeigerCounter && queuedItemProtoId != ItemProtoTypeId::ActivatedStealthBoy) {
                        badOffer = true;
                        break;
                    }

                    miscItemTurnOff(queuedItem);
                }
            } else if (itemIsQueued(offerTable)) {
                if (ProtoId(offerTable) != ItemProtoTypeId::GeigerCounter || miscItemTurnOff(offerTable) == -1) {
                    badOffer = true;
                }
            }
        }

        if (!badOffer) {
            auto [requestValue, offerValue] = barterComputeTablesValue(dude, npc, true);
            if (requestValue > offerValue) {
                badOffer = true;
            }
        }

        if (badOffer) {
            // No, your offer is not good enough.
            messageListItem.num = 28;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                gameDialogRenderSupplementaryMessage(messageListItem.text);
            }
            return -1;
        }
    }

    itemMoveAll(barterTable, dude);
    itemMoveAll(offerTable, npc);
    gBarterTableShares.clear();
    return 0;
}

static int barterGetMovedQuantity(Object* item, int maxQuantity, bool fromPlayer, bool fromInventory, bool immediate)
{
    if (maxQuantity <= 1) {
        return maxQuantity;
    }

    int suggestedValue = 1;
    if (ProtoId(item) == ItemProtoTypeId::Money && !gGameDialogSpeakerIsPartyMember) {
        // Calculate change money automatically
        auto [totalCostNpc, totalCostPlayer] = barterComputeTablesValue(gDude, _target_stack[0]);
        // Actor's balance: negative - the actor must add money to balance the tables and vice versa
        int balance = fromPlayer ? totalCostPlayer - totalCostNpc : totalCostNpc - totalCostPlayer;

        if ((balance < 0 && fromInventory) || (balance > 0 && !fromInventory)) {
            suggestedValue = std::min(std::abs(balance), maxQuantity);
            if (immediate) {
                return suggestedValue;
            }
        }
    }

    if (immediate) {
        return maxQuantity;
    }

    // CE: What the chosen quantity is worth, shown while choosing (money is
    // its own price; party members don't sell).
    std::function<int(int)> price;
    if (!gGameDialogSpeakerIsPartyMember && ProtoId(item) != ItemProtoTypeId::Money) {
        price = [item, fromPlayer](int quantity) {
            return barterGetItemPrice(item, quantity, fromPlayer);
        };
    }

    return inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, maxQuantity, suggestedValue, price);
}

// clicked on an inventory in preparation of dragging or immediate transfer
static void _drag_item_loop(Object* item, bool immediate)
{
    if (immediate) {
        soundPlayFile("iputdown");
        // prevent item look from occuring after immediate move
        _im_value = -1;
        return;
    }

    FrmImage itemInventoryFrmImage;
    const FrmId itemInventoryFrmId = itemGetInventoryFrmId(item);
    if (itemInventoryFrmImage.lock(itemInventoryFrmId)) {
        int width = itemInventoryFrmImage.getWidth();
        int height = itemInventoryFrmImage.getHeight();
        unsigned char* data = itemInventoryFrmImage.getData();
        mouseSetFrame(data, width, height, width, width / 2, height / 2, 0);
        soundPlayFile("ipickup1");
    }

    // CE: Dragged item follows the finger.
    mouseSetTouchCursorVisible(true);

    do {
        sharedFpsLimiter.mark();

        inputGetInput();

        renderPresent();
        sharedFpsLimiter.throttle();
    } while ((mouseGetEvent() & MouseButtonEvent::LeftButtonRepeat) != MouseButtonEvent::None);

    mouseSetTouchCursorVisible(false);

    if (itemInventoryFrmImage.isLocked()) {
        itemInventoryFrmImage.unlock();
        soundPlayFile("iputdown");
    }
}

// 0x474DAC barter_move_inventory
static void barterMoveToTable(Object* item, int quantity, int slotIndex, int indexOffset, Object* npc, Object* sourceTable, bool fromDude)
{
    Rect rect;
    if (fromDude) {
        rect.left = 31;
        rect.top = INVENTORY_SLOT_HEIGHT * slotIndex + 31;
    } else {
        rect.left = 389;
        rect.top = INVENTORY_SLOT_HEIGHT * slotIndex + 31;
    }

    if (quantity > 1) {
        if (fromDude) {
            _display_inventory(indexOffset, slotIndex, INVENTORY_WINDOW_TYPE_TRADE);
        } else {
            _display_target_inventory(indexOffset, slotIndex, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
        }
    } else if (inventoryHasWindow()) {
        unsigned char* dest = windowGetBuffer(gInventoryWindow);
        unsigned char* src = windowGetBuffer(gInventoryBarterBackgroundWindow);

        int pitch = INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH;
        blitBufferToBuffer(src + pitch * rect.top + rect.left + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, pitch, dest + INVENTORY_TRADE_WINDOW_WIDTH * rect.top + rect.left, INVENTORY_TRADE_WINDOW_WIDTH);

        rect.right = rect.left + INVENTORY_SLOT_WIDTH - 1;
        rect.bottom = rect.top + INVENTORY_SLOT_HEIGHT - 1;
        windowRefreshRect(gInventoryWindow, &rect);
    }

    bool immediate = _ctrl_pressed();
    _drag_item_loop(item, immediate);

    if (fromDude) {
        if (immediate || mouseHitTestInWindow(gInventoryWindow, INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_X, INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_Y)) {
            int quantityToMove = barterGetMovedQuantity(item, quantity, true, true, immediate);
            if (quantityToMove != -1) {
                if (barterMovePlayerItemToTable(item, quantityToMove, sourceTable) == -1) {
                    inventoryDisplayMessage(26); // There is no space left for that item.
                }
            }
        }
    } else {
        if (immediate || mouseHitTestInWindow(gInventoryWindow, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_X, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_Y)) {
            int quantityToMove = barterGetMovedQuantity(item, quantity, false, true, immediate);
            if (quantityToMove != -1) {
                if (itemMoveForce(npc, sourceTable, item, quantityToMove) == -1) {
                    inventoryDisplayMessage(25); // You cannot pick that up. You are at your maximum weight capacity.
                }
            }
        }
    }

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
}

// 0x475070 barter_move_from_table_inventory
static void barterMoveFromTable(Object* item, int quantity, int slotIndex, Object* npc, Object* sourceTable, bool fromDude)
{
    Rect rect;
    if (fromDude) {
        rect.left = INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD;
        rect.top = INVENTORY_SLOT_HEIGHT * slotIndex + INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y_PAD;
    } else {
        rect.left = INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD;
        rect.top = INVENTORY_SLOT_HEIGHT * slotIndex + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y_PAD;
    }

    if (quantity > 1) {
        if (fromDude) {
            barterDisplayTables(gInventoryBarterBackgroundWindow, sourceTable, nullptr, slotIndex);
        } else {
            barterDisplayTables(gInventoryBarterBackgroundWindow, nullptr, sourceTable, slotIndex);
        }
    } else if (inventoryHasWindow()) {
        unsigned char* dest = windowGetBuffer(gInventoryWindow);
        unsigned char* src = windowGetBuffer(gInventoryBarterBackgroundWindow);

        int pitch = INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH;
        blitBufferToBuffer(src + pitch * rect.top + rect.left + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, INVENTORY_SLOT_HEIGHT, pitch, dest + INVENTORY_TRADE_WINDOW_WIDTH * rect.top + rect.left, INVENTORY_TRADE_WINDOW_WIDTH);

        rect.right = rect.left + INVENTORY_SLOT_WIDTH - 1;
        rect.bottom = rect.top + INVENTORY_SLOT_HEIGHT - 1;
        windowRefreshRect(gInventoryWindow, &rect);
    }

    bool immediate = _ctrl_pressed();
    _drag_item_loop(item, immediate);

    if (fromDude) {
        if (immediate || mouseHitTestInWindow(gInventoryWindow, INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_X, INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_Y)) {
            int quantityToMove = barterGetMovedQuantity(item, quantity, true, false, immediate);
            if (quantityToMove != -1) {
                if (barterMovePlayerItemFromTable(item, quantityToMove, sourceTable) == -1) {
                    inventoryDisplayMessage(25); // You cannot pick that up. You are at your maximum weight capacity.
                }
            }
        }
    } else {
        if (immediate || mouseHitTestInWindow(gInventoryWindow, INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_X, INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_Y)) {
            int quantityToMove = barterGetMovedQuantity(item, quantity, false, false, immediate);
            if (quantityToMove != -1) {
                if (itemMoveForce(sourceTable, npc, item, quantityToMove) == -1) {
                    inventoryDisplayMessage(25); // You cannot pick that up. You are at your maximum weight capacity.
                }
            }
        }
    }

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
}

// 0x475334 display_table_inventories
static void barterDisplayTables(int win, Object* leftTable, Object* rightTable, int draggedSlotIndex)
{
    barterClampTableOffsets();

    if (!inventoryHasWindow()) {
        return;
    }

    unsigned char* windowBuffer = windowGetBuffer(gInventoryWindow);

    int oldFont = fontGetCurrent();
    fontSetCurrent(101);

    char formattedText[80];
    int rectHeight = fontGetLineHeight() + INVENTORY_SLOT_HEIGHT * gInventorySlotsCount;

    auto [requestValue, offerValue] = gGameDialogSpeakerIsPartyMember
        ? barterComputeTablesWeight(gDude, _target_stack[0])
        : barterComputeTablesValue(gDude, _target_stack[0]);

    if (leftTable != nullptr) {
        inventoryUpdateScrollButtons(playerTableScrollUpButton, playerTableScrollDownButton,
            gPlayerTableOffset, leftTable->data.inventory.length, gInventorySlotsCount);

        unsigned char* src = windowGetBuffer(win);
        blitBufferToBuffer(src + INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH * INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y + INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, rectHeight + 1, INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH, windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y + INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD, INVENTORY_TRADE_WINDOW_WIDTH);

        unsigned char* dest = windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y_PAD + INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD;
        Inventory* inventory = &(leftTable->data.inventory);
        for (int index = 0; index < gInventorySlotsCount && index + gPlayerTableOffset < inventory->length; index++) {
            InventoryItem* inventoryItem = &(inventory->items[inventory->length - (index + gPlayerTableOffset + 1)]);
            const FrmId inventoryFrmId = itemGetInventoryFrmId(inventoryItem->item);
            artRender(inventoryFrmId, dest, INVENTORY_SLOT_WIDTH_PAD, INVENTORY_SLOT_HEIGHT_PAD, INVENTORY_TRADE_WINDOW_WIDTH);
            _display_inventory_info(inventoryItem->item, inventoryItem->quantity, dest, INVENTORY_TRADE_WINDOW_WIDTH, index == draggedSlotIndex);

            dest += INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_SLOT_HEIGHT;
        }

        if (gGameDialogSpeakerIsPartyMember) {
            MessageListItem messageListItem;
            messageListItem.num = 30;

            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                snprintf(formattedText, sizeof(formattedText), "%s %d", messageListItem.text, offerValue);
            }
        } else {
            snprintf(formattedText, sizeof(formattedText), "$%d", offerValue);
        }

        fontDrawText(windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * (INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y_PAD) + INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD, formattedText, 80, INVENTORY_TRADE_WINDOW_WIDTH, COLOR_WHITE);

        Rect rect;
        rect.left = INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD;
        rect.top = INVENTORY_TRADE_INNER_LEFT_SCROLLER_Y_PAD;
        // NOTE: Odd math, the only way to get 223 is to subtract 2.
        rect.right = INVENTORY_TRADE_INNER_LEFT_SCROLLER_X_PAD + INVENTORY_SLOT_WIDTH_PAD - 2;
        rect.bottom = rect.top + rectHeight;
        windowRefreshRect(gInventoryWindow, &rect);
    }

    if (rightTable != nullptr) {
        inventoryUpdateScrollButtons(bartererTableScrollUpButton, bartererTableScrollDownButton,
            gBartererTableOffset, rightTable->data.inventory.length, gInventorySlotsCount);

        unsigned char* src = windowGetBuffer(win);
        blitBufferToBuffer(src + INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH * INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD + INVENTORY_TRADE_WINDOW_OFFSET, INVENTORY_SLOT_WIDTH, rectHeight + 1, INVENTORY_TRADE_BACKGROUND_WINDOW_WIDTH, windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD, INVENTORY_TRADE_WINDOW_WIDTH);

        unsigned char* dest = windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y_PAD + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD;
        Inventory* inventory = &(rightTable->data.inventory);
        for (int index = 0; index < gInventorySlotsCount && index + gBartererTableOffset < inventory->length; index++) {
            InventoryItem* inventoryItem = &(inventory->items[inventory->length - (index + gBartererTableOffset + 1)]);
            const FrmId inventoryFrmId = itemGetInventoryFrmId(inventoryItem->item);
            artRender(inventoryFrmId, dest, INVENTORY_SLOT_WIDTH_PAD, INVENTORY_SLOT_HEIGHT_PAD, INVENTORY_TRADE_WINDOW_WIDTH);
            _display_inventory_info(inventoryItem->item, inventoryItem->quantity, dest, INVENTORY_TRADE_WINDOW_WIDTH, index == draggedSlotIndex);

            dest += INVENTORY_TRADE_WINDOW_WIDTH * INVENTORY_SLOT_HEIGHT;
        }

        if (gGameDialogSpeakerIsPartyMember) {
            MessageListItem messageListItem;
            messageListItem.num = 30;

            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                snprintf(formattedText, sizeof(formattedText), "%s %d", messageListItem.text, requestValue);
            }
        } else {
            snprintf(formattedText, sizeof(formattedText), "$%d", requestValue);
        }

        fontDrawText(windowBuffer + INVENTORY_TRADE_WINDOW_WIDTH * (INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y_PAD) + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD, formattedText, 80, INVENTORY_TRADE_WINDOW_WIDTH, COLOR_WHITE);

        Rect rect;
        rect.left = INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD;
        rect.top = INVENTORY_TRADE_INNER_RIGHT_SCROLLER_Y_PAD;
        // NOTE: Odd math, likely should be `INVENTORY_SLOT_WIDTH_PAD`.
        rect.right = INVENTORY_TRADE_INNER_RIGHT_SCROLLER_X_PAD + INVENTORY_SLOT_WIDTH;
        rect.bottom = rect.top + rectHeight;
        windowRefreshRect(gInventoryWindow, &rect);
    }

    fontSetCurrent(oldFont);
}

static bool _ctrl_pressed()
{
    const Uint8* keyboardState = SDL_GetKeyboardState(nullptr);
    return keyboardState[SDL_SCANCODE_LCTRL] || keyboardState[SDL_SCANCODE_RCTRL];
}

namespace {

    // A barter going on (`barterProcessUI`) and what it has set aside.
    struct BarterSession {
        int win = -1;
        Object* barterer = nullptr;
        Object* playerTable = nullptr;
        Object* bartererTable = nullptr;
        int barterMod = 0;
        Object* armor = nullptr;
        Object* item1 = nullptr;
        Object* item2 = nullptr;
        Object* hiddenBox = nullptr;
        Object* playerObj = nullptr;
        FrmId savedDudeFrmId;
        std::vector<Object*> partyTargets;
        int partyTargetIndex = 0;
        bool isoWasEnabled = false;
        int reactionModifier = 0;
    };

} // namespace

// Starts the barter: the trader's worn armor and weapon aren't for sale (set
// aside, as the items the trader hides), the sides and the tables are set up.
// `false` - it can't start (as in the game, what was set aside stays aside).
static bool barterSessionBegin(BarterSession& session)
{
    int win = session.win;
    Object* barterer = session.barterer;
    Object* playerTable = session.playerTable;
    Object* bartererTable = session.bartererTable;
    int barterMod = session.barterMod;

    gBarterFinalModifier = barterMod;

    if (inventoryCommonInit() == -1) {
        return false;
    }

    Object*& armor = session.armor;
    Object*& item1 = session.item1;
    Object*& item2 = session.item2;
    Object*& hiddenBox = session.hiddenBox;

    armor = critterGetArmor(barterer);
    if (armor != nullptr) {
        itemRemoveWithReason(barterer, armor, 1, RemoveInventoryObjectHookReason::BarterArmor);
    }

    item1 = nullptr;
    item2 = critterGetItem2(barterer);
    if (item2 != nullptr) {
        itemRemoveWithReason(barterer, item2, 1, RemoveInventoryObjectHookReason::BarterWeapon);
    } else {
        if (!gGameDialogSpeakerIsPartyMember) {
            item1 = inventoryFindByType(barterer, ITEM_TYPE_WEAPON, nullptr);
            if (item1 != nullptr) {
                itemRemoveWithReason(barterer, item1, 1, RemoveInventoryObjectHookReason::BarterWeapon);
            }
        }
    }

    hiddenBox = nullptr;
    if (objectCreateWithFrmIdProtoId(&hiddenBox, FrmId::Empty(), ItemProtoTypeId::JesseContainer) == -1) {
        return false;
    }

    // Sfall: remove hidden items of barterer (relevant to Goris)
    itemMoveAllHidden(barterer, hiddenBox);

    // SFALL: Fix for not counting in the weight/size of equipped items on NPC
    // when stealing or bartering: what was taken out above still counts in
    // the barterer's weight (a party member can't be given more than it can
    // carry with it).
    itemsSetTakenOut(barterer, { armor, item2, item1 }, hiddenBox);

    _pud = &(_inven_dude->data.inventory);
    gBartererTableObj = bartererTable;
    gPlayerTableObj = playerTable;

    gPlayerTableOffset = 0;
    gBartererTableOffset = 0;

    gPlayerTableInventory = &(playerTable->data.inventory);
    gBartererTableInventory = &(bartererTable->data.inventory);

    gInventoryBarterBackgroundWindow = win;
    _target_curr_stack = 0;
    _target_pud = &(barterer->data.inventory);

    _target_stack[0] = barterer;
    _target_stack_offset[0] = 0;

    session.playerObj = _inven_dude;
    session.savedDudeFrmId = gInventoryWindowDudeFrmId;

    std::vector<Object*>& partyTargets = session.partyTargets;
    partyTargets = { _inven_dude };
    if (settings.qol.party_loot_and_barter) {
        for (Object* pm : get_all_party_members_objects(false)) {
            if (pm != gDude && pm != barterer && partyMemberTradesItems(pm)) {
                partyTargets.push_back(pm);
            }
        }
    }
    session.partyTargetIndex = 0;

    gBarterBarterer = barterer;
    gBarterPartyTargets = &partyTargets;
    gBarterPartyTargetIndex = &(session.partyTargetIndex);
    gBarterActions.clear();
    gBarterRequest = BarterRequest::None;
    gBarterTableShares.clear();

    session.isoWasEnabled = _setup_inventory(INVENTORY_WINDOW_TYPE_TRADE);

    int& barterReactionModifier = session.reactionModifier;
    barterReactionModifier = 0;
    switch (reactionTranslateValue(reactionGetValue(barterer))) {
    case NPC_REACTION_BAD:
        barterReactionModifier = 25;
        break;
    case NPC_REACTION_NEUTRAL:
        break;
    case NPC_REACTION_GOOD:
        barterReactionModifier = -15;
        break;
    default:
        assert(false && "Should be unreachable");
    }

    return true;
}

// Back to talk (the game's T): the tables go back to their sides.
static void barterSessionReturnToTalk(BarterSession& session)
{
    itemMoveAll(session.bartererTable, session.barterer);
    // The player's table goes back once the barter ends
    // (`barterReturnPlayerTable`).
    gameDialogEndBarter();
}

// Offer (the game's M): the trader takes the deal or says why not.
static void barterSessionOffer(BarterSession& session)
{
    Object* barterer = session.barterer;
    Object* playerTable = session.playerTable;
    Object* bartererTable = session.bartererTable;

    if (playerTable->data.inventory.length != 0 || gBartererTableObj->data.inventory.length != 0) {
        // TODO: inven_dude can potentially be a container (bag) which was opened during trade, but code inside barterAttemptTransaction assumes it's a critter; maybe remove this arg and access gDude always?
        if (barterAttemptTransaction(_inven_dude, playerTable, barterer, bartererTable) == 0) {
            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
            barterDisplayTables(session.win, playerTable, bartererTable, -1);

            // Ok, that's a good trade.
            MessageListItem messageListItem;
            messageListItem.num = 27;
            if (!gGameDialogSpeakerIsPartyMember) {
                if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                    gameDialogRenderSupplementaryMessage(messageListItem.text);
                }
            }
        }
    }
}

// The game's barter: its window with the sides and the tables, the mouse
// and the keyboard.
static void barterRunWindowLoop(BarterSession& session)
{
    int win = session.win;
    Object* barterer = session.barterer;
    Object* playerTable = session.playerTable;
    Object* bartererTable = session.bartererTable;
    int barterMod = session.barterMod;
    int barterReactionModifier = session.reactionModifier;
    std::vector<Object*>& partyTargets = session.partyTargets;
    int& partyTargetIndex = session.partyTargetIndex;

    _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
    _display_inventory(_stack_offset[0], -1, INVENTORY_WINDOW_TYPE_TRADE);
    _display_body(FrmId(barterer), INVENTORY_WINDOW_TYPE_TRADE);
    windowRefresh(gInventoryBarterBackgroundWindow);
    barterDisplayTables(win, playerTable, bartererTable, -1);

    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);

    int keyCode = -1;
    for (;;) {
        sharedFpsLimiter.mark();

        if (keyCode == KEY_ESCAPE || _game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        keyCode = inputGetInput();
        MouseButtonEvent mouseEvent = mouseGetEvent();
        barterClampTableOffsets();

        devAutotestTick();
        InventoryScrollerDisplayContext inventoryScrollerContext { INVENTORY_WINDOW_TYPE_TRADE, nullptr };
        InventoryScrollerDisplayContext targetScrollerContext { INVENTORY_WINDOW_TYPE_TRADE, _target_pud };
        InventoryScrollerBarterContext barterScrollerContext { win, playerTable, bartererTable };
        InventoryScroller tradeInventoryScroller {
            { INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_X, INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_LEFT_SCROLLER_TRACKING_Y },
            &(_stack_offset[_curr_stack]),
            1,
            gInventorySlotsCount,
            _pud->length,
            KEY_ARROW_UP,
            KEY_ARROW_DOWN,
            -1,
            -1,
            -1,
            -1,
            inventoryScrollerRedrawInventory,
            &inventoryScrollerContext,
            false,
        };
        InventoryScroller tradePlayerTableScroller {
            { INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_X, INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_LEFT_SCROLLER_TRACKING_Y },
            &gPlayerTableOffset,
            1,
            gInventorySlotsCount,
            gPlayerTableInventory->length,
            KEY_PAGE_UP,
            KEY_PAGE_DOWN,
            -1,
            -1,
            -1,
            -1,
            inventoryScrollerRedrawBarterTables,
            &barterScrollerContext,
            false,
        };
        InventoryScroller tradeTargetScroller {
            { INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_X, INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_RIGHT_SCROLLER_TRACKING_Y },
            &(_target_stack_offset[_target_curr_stack]),
            1,
            gInventorySlotsCount,
            _target_pud->length,
            KEY_CTRL_ARROW_UP,
            KEY_CTRL_ARROW_DOWN,
            -1,
            -1,
            -1,
            -1,
            inventoryScrollerRedrawTargetInventory,
            &targetScrollerContext,
            true,
        };
        InventoryScroller tradeBartererTableScroller {
            { INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_X, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_Y, INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_MAX_X, INVENTORY_SLOT_HEIGHT * gInventorySlotsCount + INVENTORY_TRADE_INNER_RIGHT_SCROLLER_TRACKING_Y },
            &gBartererTableOffset,
            1,
            gInventorySlotsCount,
            gBartererTableInventory->length,
            KEY_CTRL_PAGE_UP,
            KEY_CTRL_PAGE_DOWN,
            -1,
            -1,
            -1,
            -1,
            inventoryScrollerRedrawBarterTables,
            &barterScrollerContext,
            false,
        };
        if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X || keyCode == KEY_F10) {
            showQuitConfirmationDialog();
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        gBarterFinalModifier = barterMod + barterReactionModifier;

        if (keyCode == KEY_LOWERCASE_T || barterReactionModifier <= -30) {
            // T == return to talk
            barterSessionReturnToTalk(session);
            break;
        } else if (keyCode == KEY_LOWERCASE_M) {
            // M == attempt offer
            barterSessionOffer(session);
        } else if (keyCode == KEY_ARROW_LEFT || keyCode == KEY_ARROW_RIGHT) {
            int next = partyTargetStep(partyTargets, partyTargetIndex, keyCode == KEY_ARROW_LEFT ? -1 : 1);
            if (next != partyTargetIndex) {
                partyTargetIndex = next;
                inventorySetLeftPaneCritter(partyTargets[partyTargetIndex], barterer, INVENTORY_WINDOW_TYPE_TRADE);
            }
        } else if (keyCode >= 2500 && keyCode <= 2501) {
            _container_exit(keyCode, INVENTORY_WINDOW_TYPE_TRADE);
        } else {
            inventoryScrollerHandleInput(tradeInventoryScroller, keyCode, mouseEvent);
            inventoryScrollerHandleInput(tradePlayerTableScroller, keyCode, mouseEvent);
            inventoryScrollerHandleInput(tradeTargetScroller, keyCode, mouseEvent);
            inventoryScrollerHandleInput(tradeBartererTableScroller, keyCode, mouseEvent);

            if ((mouseEvent & MouseButtonEvent::RightButtonDown) != MouseButtonEvent::None) {
                if (gInventoryCursor == INVENTORY_WINDOW_CURSOR_HAND) {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
                } else {
                    inventorySetCursor(INVENTORY_WINDOW_CURSOR_HAND);
                }
            } else if ((mouseEvent & MouseButtonEvent::LeftButtonDown) != MouseButtonEvent::None) {
                if (keyCode >= 1000 && keyCode <= 1000 + gInventorySlotsCount) {
                    if (inventoryUsesContextMenu()) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_TRADE);
                        barterDisplayTables(win, playerTable, nullptr, -1);
                    } else {
                        // player inventory
                        int slotIndex = keyCode - 1000;
                        if (slotIndex + _stack_offset[_curr_stack] < _pud->length) {
                            int offset = _stack_offset[_curr_stack];
                            InventoryItem* inventoryItem = &(_pud->items[_pud->length - (slotIndex + offset + 1)]);
                            barterMoveToTable(inventoryItem->item, inventoryItem->quantity, slotIndex, offset, barterer, playerTable, true);
                            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                            barterDisplayTables(win, playerTable, nullptr, -1);
                        }
                    }

                    keyCode = -1;
                } else if (keyCode >= 2000 && keyCode <= 2000 + gInventorySlotsCount) {
                    // merchant inventory
                    if (inventoryUsesContextMenu()) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_TRADE);
                        barterDisplayTables(win, nullptr, bartererTable, -1);
                    } else {
                        int slotIndex = keyCode - 2000;
                        if (slotIndex + _target_stack_offset[_target_curr_stack] < _target_pud->length) {
                            int stackOffset = _target_stack_offset[_target_curr_stack];
                            InventoryItem* inventoryItem = &(_target_pud->items[_target_pud->length - (slotIndex + stackOffset + 1)]);
                            barterMoveToTable(inventoryItem->item, inventoryItem->quantity, slotIndex, stackOffset, barterer, bartererTable, false);
                            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                            barterDisplayTables(win, nullptr, bartererTable, -1);
                        }
                    }

                    keyCode = -1;
                } else if (keyCode >= 2300 && keyCode <= 2300 + gInventorySlotsCount) {
                    // player table (offer)
                    if (inventoryUsesContextMenu()) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_TRADE);
                        barterDisplayTables(win, playerTable, nullptr, -1);
                    } else {
                        int slotIndex = keyCode - 2300;
                        int tableIndex = slotIndex + gPlayerTableOffset;
                        InventoryItem* inventoryItem = inventoryGetReversedSlot(gPlayerTableInventory, tableIndex);
                        if (inventoryItem != nullptr) {
                            barterMoveFromTable(inventoryItem->item, inventoryItem->quantity, slotIndex, barterer, playerTable, true);
                            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                            barterDisplayTables(win, playerTable, nullptr, -1);
                        }
                    }

                    keyCode = -1;
                } else if (keyCode >= 2400 && keyCode <= 2400 + gInventorySlotsCount) {
                    // merchant table (offer)
                    if (inventoryUsesContextMenu()) {
                        inventoryWindowOpenContextMenu(keyCode, INVENTORY_WINDOW_TYPE_TRADE);
                        barterDisplayTables(win, nullptr, bartererTable, -1);
                    } else {
                        int slotIndex = keyCode - 2400;
                        int tableIndex = slotIndex + gBartererTableOffset;
                        InventoryItem* inventoryItem = inventoryGetReversedSlot(gBartererTableInventory, tableIndex);
                        if (inventoryItem != nullptr) {
                            barterMoveFromTable(inventoryItem->item, inventoryItem->quantity, slotIndex, barterer, bartererTable, false);
                            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, INVENTORY_WINDOW_TYPE_TRADE);
                            _display_inventory(_stack_offset[_curr_stack], -1, INVENTORY_WINDOW_TYPE_TRADE);
                            barterDisplayTables(win, nullptr, bartererTable, -1);
                        }
                    }

                    keyCode = -1;
                }
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }
}

// CE: The mobile UI's barter screen shows the sides and the tables (see
// `barterGetView`); the loop runs the screen's actions and buttons with the
// functions the window loop runs.
static void barterRunTouchLoop(BarterSession& session)
{
    for (;;) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();

        devAutotestTick();

        if (keyCode == KEY_CTRL_Q || keyCode == KEY_CTRL_X || keyCode == KEY_F10) {
            showQuitConfirmationDialog();
        }

        // A desktop keyboard: Esc leaves, as in the game.
        if (keyCode == KEY_ESCAPE || _game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            break;
        }

        gBarterFinalModifier = session.barterMod + session.reactionModifier;

        BarterRequest request = gBarterRequest;
        gBarterRequest = BarterRequest::None;

        if (request == BarterRequest::Talk || session.reactionModifier <= -30) {
            barterSessionReturnToTalk(session);
            break;
        } else if (request == BarterRequest::Offer) {
            barterSessionOffer(session);
        } else if (!gBarterActions.empty()) {
            barterPerformQueuedActions(session.barterer, session.playerTable, session.bartererTable);
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }
}

// Ends the barter: what was set aside goes back.
static void barterSessionEnd(BarterSession& session)
{
    gBarterBarterer = nullptr;
    gBarterPartyTargets = nullptr;
    gBarterPartyTargetIndex = nullptr;
    gBarterActions.clear();
    gBarterRequest = BarterRequest::None;

    Object* barterer = session.barterer;
    itemsResetTakenOut();
    itemMoveAll(session.hiddenBox, barterer);
    objectDestroy(session.hiddenBox, nullptr);

    if (session.armor != nullptr) {
        session.armor->flags |= OBJECT_WORN;
        itemAdd(barterer, session.armor, 1);
    }

    if (session.item2 != nullptr) {
        session.item2->flags |= OBJECT_IN_RIGHT_HAND;
        itemAdd(barterer, session.item2, 1);
    }

    if (session.item1 != nullptr) {
        itemAdd(barterer, session.item1, 1);
    }

    gInventoryWindowDudeFrmId = session.savedDudeFrmId;
    _exit_inventory(session.isoWasEnabled);
    _inven_dude = session.playerObj;

    barterReturnPlayerTable(session.playerTable);

    // NOTE: Uninline.
    inventoryCommonFree();
}

// 0x4757F0 barter_inventory
void barterProcessUI(int win, Object* barterer, Object* playerTable, Object* bartererTable, int barterMod)
{
    ScopedGameMode gm(GameMode::kBarter);

    BarterSession session;
    session.win = win;
    session.barterer = barterer;
    session.playerTable = playerTable;
    session.bartererTable = bartererTable;
    session.barterMod = barterMod;

    if (!barterSessionBegin(session)) {
        return;
    }

    if (inventoryScreensWindowless()) {
        barterRunTouchLoop(session);
    } else {
        barterRunWindowLoop(session);
    }

    barterSessionEnd(session);
}

// 0x47620C
static void _container_enter(int keyCode, int inventoryWindowType)
{
    if (keyCode >= 2000) {
        int index = _target_pud->length - (_target_stack_offset[_target_curr_stack] + keyCode - 2000 + 1);
        if (index < _target_pud->length && _target_curr_stack < 9) {
            InventoryItem* inventoryItem = &(_target_pud->items[index]);
            Object* item = inventoryItem->item;
            if (itemGetType(item) == ITEM_TYPE_CONTAINER) {
                _target_curr_stack += 1;
                _target_stack[_target_curr_stack] = item;
                _target_stack_offset[_target_curr_stack] = 0;

                _target_pud = &(item->data.inventory);

                _display_body(FrmId(item), inventoryWindowType);
                _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, inventoryWindowType);
                windowRefresh(gInventoryWindow);
            }
        }
    } else {
        int index = _pud->length - (_stack_offset[_curr_stack] + keyCode - 1000 + 1);
        if (index < _pud->length && _curr_stack < 9) {
            InventoryItem* inventoryItem = &(_pud->items[index]);
            Object* item = inventoryItem->item;
            if (itemGetType(item) == ITEM_TYPE_CONTAINER) {
                _curr_stack += 1;

                _stack[_curr_stack] = item;
                _stack_offset[_curr_stack] = 0;

                _inven_dude = _stack[_curr_stack];
                _pud = &(item->data.inventory);

                _adjust_fid();
                _display_body(FrmId::Empty(), inventoryWindowType);
                _display_inventory(_stack_offset[_curr_stack], -1, inventoryWindowType);
            }
        }
    }
}

// 0x476394
static void _container_exit(int keyCode, int inventoryWindowType)
{
    if (keyCode == 2500) {
        if (_curr_stack > 0) {
            _curr_stack -= 1;
            _inven_dude = _stack[_curr_stack];
            _pud = &_inven_dude->data.inventory;
            _adjust_fid();
            _display_body(FrmId::Empty(), inventoryWindowType);
            _display_inventory(_stack_offset[_curr_stack], -1, inventoryWindowType);
        }
    } else if (keyCode == 2501) {
        if (_target_curr_stack > 0) {
            _target_curr_stack -= 1;
            Object* target = _target_stack[_target_curr_stack];
            _target_pud = &(target->data.inventory);
            _display_body(FrmId(target), inventoryWindowType);
            _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, inventoryWindowType);
            windowRefresh(gInventoryWindow);
        }
    }
}

// Drop item inside a container item (bag, backpack, etc.).
// 0x476464
static int _drop_into_container(Object* container, Object* item, int sourceIndex, Object** itemSlot, int quantity)
{
    if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_CONTAINER, item, container)) {
        return -1;
    }

    int quantityToMove;
    if (quantity > 1) {
        quantityToMove = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, item, quantity);
    } else {
        quantityToMove = 1;
    }

    if (quantityToMove == -1) {
        return -1;
    }

    if (sourceIndex != -1) {
        if (itemRemoveWithReason(_inven_dude, item, quantityToMove, RemoveInventoryObjectHookReason::DropIntoContainer) == -1) {
            return -1;
        }
    }

    int rc = itemAttemptAdd(container, item, quantityToMove);
    if (rc != 0) {
        if (sourceIndex != -1) {
            // SFALL: Fix for items disappearing from inventory when you try to
            // drag them to bag/backpack in the inventory list and are
            // overloaded.
            itemAdd(_inven_dude, item, quantityToMove);
        }
    } else {
        if (itemSlot != nullptr) {
            if (itemSlot == &gInventoryArmor) {
                adjustCritterStatsOnArmorChange(_stack[0], gInventoryArmor, nullptr);
            }
            *itemSlot = nullptr;
        }
    }

    return rc;
}

static void inventoryUnloadWeaponToOwner(Object* owner, Object* weapon)
{
    Object* ammo;
    while ((ammo = weaponUnload(weapon)) != nullptr) {
        itemAdd(owner, ammo, 1);
    }
}

// 0x47650C drop ammo into weapon
// Dropping [ammo] on [weapon] loads it ([replaceAmmo] - unloads other ammo
// first).
static bool inventoryCanLoadAmmo(Object* weapon, Object* ammo, bool* replaceAmmo)
{
    *replaceAmmo = false;

    if (itemGetType(weapon) != ITEM_TYPE_WEAPON) {
        return false;
    }

    if (itemGetType(ammo) != ITEM_TYPE_AMMO) {
        return false;
    }

    if (ProtoId(weapon) == ItemProtoTypeId::SolarScorcher) {
        return false;
    }

    if (!weaponCanBeReloadedWith(weapon, ammo)) {
        if (!settings.qol.fast_ammo_load
            || !weaponCanBeUnloaded(weapon)
            || !weaponCanBeReloadedWithReplacingAmmo(weapon, ammo)) {
            return false;
        }
        *replaceAmmo = true;
    }

    if (!*replaceAmmo && ammoGetQuantity(weapon) >= ammoGetCapacity(weapon)) {
        return false;
    }

    return true;
}

static InventoryAmmoMoveResult _drop_ammo_into_weapon(Object* weapon, Object* ammo, Object** ammoItemSlot, int quantity, int keyCode)
{
    bool replaceAmmo;
    if (!inventoryCanLoadAmmo(weapon, ammo, &replaceAmmo)) {
        return INVENTORY_AMMO_MOVE_RESULT_FAILED;
    }

    if (!scriptHooks_InventoryMove(HOOK_INVENTORYMOVE_WEAPON_RELOAD, ammo, weapon)) {
        return INVENTORY_AMMO_MOVE_RESULT_BLOCKED;
    }

    int quantityToMove;
    if (settings.qol.fast_ammo_load) {
        quantityToMove = quantity;
    } else if (quantity > 1) {
        quantityToMove = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_MOVE_ITEMS, ammo, quantity);
    } else {
        quantityToMove = 1;
    }

    if (quantityToMove == -1) {
        return INVENTORY_AMMO_MOVE_RESULT_FAILED;
    }

    Object* sourceItem = ammo;
    bool isReloaded = false;
    int rc = itemRemoveQuietly(_inven_dude, weapon, 1);
    std::vector<Object*> unloadedAmmo;
    if (replaceAmmo) {
        Object* oldAmmo;
        while ((oldAmmo = weaponUnload(weapon)) != nullptr) {
            unloadedAmmo.push_back(oldAmmo);
        }
    }

    for (int index = 0; index < quantityToMove; index++) {
        int rcReload = weaponReload(weapon, sourceItem);
        if (rcReload == 0) {
            if (ammoItemSlot != nullptr) {
                *ammoItemSlot = nullptr;
            } else {
                itemRemoveQuietly(_inven_dude, sourceItem, 1);
            }

            objectDestroy(sourceItem);

            isReloaded = true;
            if (_inven_from_button(keyCode, &sourceItem, nullptr, nullptr) == 0) {
                break;
            }
        }
        if (rcReload != -1) {
            isReloaded = true;
        }
        if (rcReload != 0) {
            break;
        }
    }

    if (rc != -1) {
        itemAdd(_inven_dude, weapon, 1);
    }

    for (Object* oldAmmo : unloadedAmmo) {
        itemAdd(_inven_dude, oldAmmo, 1);
    }

    if (!isReloaded) {
        return INVENTORY_AMMO_MOVE_RESULT_FAILED;
    }

    const char* sfx = sfxBuildWeaponName(WeaponSoundEffect::Ready, weapon, HIT_MODE_RIGHT_WEAPON_PRIMARY, nullptr);
    soundPlayFile(sfx);

    return INVENTORY_AMMO_MOVE_RESULT_SUCCESS;
}

// 0x47664C draw_amount
static void _draw_amount(int value, int inventoryWindowType)
{
    FrmImage numbersFrmImage;
    if (!numbersFrmImage.lock(InterfaceFrameId::BigNum)) {
        return;
    }

    Rect rect;

    int windowWidth = windowGetWidth(_mt_wid);
    unsigned char* windowBuffer = windowGetBuffer(_mt_wid);

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
        rect.left = 125;
        rect.top = 45;
        rect.right = 195;
        rect.bottom = 69;

        int ranks[5];
        ranks[4] = value % 10;
        ranks[3] = value / 10 % 10;
        ranks[2] = value / 100 % 10;
        ranks[1] = value / 1000 % 10;
        ranks[0] = value / 10000 % 10;

        windowBuffer += rect.top * windowWidth + rect.left;

        for (int index = 0; index < 5; index++) {
            unsigned char* src = numbersFrmImage.getData() + 14 * ranks[index];
            blitBufferToBuffer(src, 14, 24, 336, windowBuffer, windowWidth);
            windowBuffer += 14;
        }
    } else {
        rect.left = 133;
        rect.top = 64;
        rect.right = 189;
        rect.bottom = 88;

        windowBuffer += windowWidth * rect.top + rect.left;
        blitBufferToBuffer(numbersFrmImage.getData() + 14 * (value / 60), 14, 24, 336, windowBuffer, windowWidth);
        blitBufferToBuffer(numbersFrmImage.getData() + 14 * (value % 60 / 10), 14, 24, 336, windowBuffer + 14 * 2, windowWidth);
        blitBufferToBuffer(numbersFrmImage.getData() + 14 * (value % 10), 14, 24, 336, windowBuffer + 14 * 3, windowWidth);
    }

    windowRefreshRect(_mt_wid, &rect);
}

// 0x47688C do_move_timer
// CE: [price] - the price of a quantity in this deal, shown by the mobile
// UI's picker.
static int inventoryQuantitySelect(int inventoryWindowType, Object* item, int max, int defaultValue, const std::function<int(int)>& price)
{
    ScopedGameMode gm(GameMode::kCounter);

    // CE: Mobile UI picker, same ranges (timer: 10..180 s by 10, 60 first).
    if (muiIsEnabled()) {
        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
            return muiQuantitySelect(item, 1, std::min(max, 99999), 1, defaultValue, false, price);
        }
        return muiQuantitySelect(item, 10, max, 10, 60, true);
    }

    inventoryQuantityWindowInit(inventoryWindowType, item);

    int value;
    int min;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
        if (max > 99999) {
            max = 99999;
        }
        min = 1;
        value = std::clamp(defaultValue, min, max);
    } else {
        value = 60;
        min = 10;
    }

    _draw_amount(value, inventoryWindowType);

    bool isTyping = false;
    for (;;) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            inventoryQuantityWindowFree(inventoryWindowType);
            return -1;
        }

        if (keyCode == KEY_RETURN || keyCode == 500) {
            if (value >= min && value <= max) {
                if (inventoryWindowType != INVENTORY_WINDOW_TYPE_SET_TIMER || value % 10 == 0) {
                    if (keyCode != 500) {
                        soundPlayFile("ib1p1xx1");
                    }
                    break;
                }
            }

            soundPlayFile("iisxxxx1");

        } else if (keyCode == 5000 || keyCode == KEY_LOWERCASE_A) {
            if (keyCode == KEY_LOWERCASE_A) {
                soundPlayFile("ib1p1xx1");
            }
            isTyping = false;
            value = max;
            _draw_amount(value, inventoryWindowType);
        } else if (keyCode == 6000) {
            isTyping = false;
            if (value < max) {
                if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
                    if ((mouseGetEvent() & MouseButtonEvent::LeftButtonRepeat) != MouseButtonEvent::None) {
                        getTicks();

                        unsigned int delay = 100;
                        while ((mouseGetEvent() & MouseButtonEvent::LeftButtonRepeat) != MouseButtonEvent::None) {
                            sharedFpsLimiter.mark();

                            if (value < max) {
                                value++;
                            }

                            _draw_amount(value, inventoryWindowType);
                            inputGetInput();

                            if (delay > 1) {
                                delay--;
                                inputPauseForTocks(delay);
                            }

                            renderPresent();
                            sharedFpsLimiter.throttle();
                        }
                    } else {
                        if (value < max) {
                            value++;
                        }
                    }
                } else {
                    value += 10;
                }

                _draw_amount(value, inventoryWindowType);
                continue;
            }
        } else if (keyCode == 7000) {
            isTyping = false;
            if (value > min) {
                if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
                    if ((mouseGetEvent() & MouseButtonEvent::LeftButtonRepeat) != MouseButtonEvent::None) {
                        getTicks();

                        unsigned int delay = 100;
                        while ((mouseGetEvent() & MouseButtonEvent::LeftButtonRepeat) != MouseButtonEvent::None) {
                            sharedFpsLimiter.mark();

                            if (value > min) {
                                value--;
                            }

                            _draw_amount(value, inventoryWindowType);
                            inputGetInput();

                            if (delay > 1) {
                                delay--;
                                inputPauseForTocks(delay);
                            }

                            renderPresent();
                            sharedFpsLimiter.throttle();
                        }
                    } else {
                        if (value > min) {
                            value--;
                        }
                    }
                } else {
                    value -= 10;
                }

                _draw_amount(value, inventoryWindowType);
                continue;
            }
        }

        if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
            if (keyCode >= KEY_0 && keyCode <= KEY_9) {
                int number = keyCode - KEY_0;
                if (!isTyping) {
                    value = 0;
                }

                value = 10 * value % 100000 + number;
                isTyping = true;

                _draw_amount(value, inventoryWindowType);
                continue;
            } else if (keyCode == KEY_BACKSPACE) {
                if (!isTyping) {
                    value = 0;
                }

                value /= 10;
                isTyping = true;

                _draw_amount(value, inventoryWindowType);
                continue;
            }
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    inventoryQuantityWindowFree(inventoryWindowType);

    return value;
}

// Creates move items/set timer interface.
//
// 0x476AB8 setup_move_timer_win
static int inventoryQuantityWindowInit(int inventoryWindowType, Object* item)
{
    const int oldFont = fontGetCurrent();
    fontSetCurrent(103);

    const InventoryWindowDescription* windowDescription = &(gInventoryWindowDescriptions[inventoryWindowType]);

    // Maintain original position in original resolution, otherwise center it.
    int quantityWindowX = screenGetWidth() != 640
        ? (screenGetWidth() - windowDescription->width) / 2
        : windowDescription->x;
    int quantityWindowY = screenGetHeight() != 480
        ? inventoryGetCenteredWindowY(windowDescription->height)
        : windowDescription->y;
    _mt_wid = windowCreate(quantityWindowX, quantityWindowY, windowDescription->width, windowDescription->height, static_cast<ColorWithFlags>(257), WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    unsigned char* windowBuffer = windowGetBuffer(_mt_wid);

    FrmImage backgroundFrmImage;
    const FrmId backgroundFrmId = FrmId(windowDescription->frmId);
    if (backgroundFrmImage.lock(backgroundFrmId)) {
        blitBufferToBuffer(backgroundFrmImage.getData(),
            windowDescription->width,
            windowDescription->height,
            windowDescription->width,
            windowBuffer,
            windowDescription->width);
    }

    MessageListItem messageListItem;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
        // MOVE ITEMS
        messageListItem.num = 21;
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            int length = fontGetStringWidth(messageListItem.text);
            fontDrawText(windowBuffer + windowDescription->width * 9 + (windowDescription->width - length) / 2, messageListItem.text, 200, windowDescription->width, COLOR_YELLOW_2);
        }
    } else if (inventoryWindowType == INVENTORY_WINDOW_TYPE_SET_TIMER) {
        // SET TIMER
        messageListItem.num = 23;
        if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
            int length = fontGetStringWidth(messageListItem.text);
            fontDrawText(windowBuffer + windowDescription->width * 9 + (windowDescription->width - length) / 2, messageListItem.text, 200, windowDescription->width, COLOR_YELLOW_2);
        }

        // Timer overlay
        FrmImage overlayFrmImage;
        if (overlayFrmImage.lock(InterfaceFrameId::TimerOverlay)) {
            blitBufferToBuffer(overlayFrmImage.getData(),
                105, 81, 105,
                windowBuffer + 34 * windowDescription->width + 113, windowDescription->width);
        }
    }

    const FrmId inventoryFrmId = itemGetInventoryFrmId(item);
    artRender(inventoryFrmId, windowBuffer + windowDescription->width * 46 + 16, INVENTORY_LARGE_SLOT_WIDTH, INVENTORY_LARGE_SLOT_HEIGHT, windowDescription->width);

    int x = 194;
    int y = 64;
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
        x = 200;
        y = 46;
    }

    // Plus button
    int btn = buttonCreateActionWithFrm(_mt_wid,
        x, y, 6000, -1, InterfaceFrameId::CharacterEditorSpecialPositiveOff, InterfaceFrameId::CharacterEditorSpecialPositiveOn);

    // Minus button
    btn = buttonCreateActionWithFrm(_mt_wid,
        x, y + 12, 7000, -1, InterfaceFrameId::CharacterEditorSpecialNegativeOff, InterfaceFrameId::CharacterEditorSpecialNegativeOn);

    // Done
    btn = buttonCreateActionWithFrm(_mt_wid,
        98, 128, -1, 500, InterfaceFrameId::LittleRedButtonUp, InterfaceFrameId::LittleRedButtonDown);

    // Cancel
    btn = buttonCreateActionWithFrm(_mt_wid,
        148, 128, -1, KEY_ESCAPE, InterfaceFrameId::LittleRedButtonUp, InterfaceFrameId::LittleRedButtonDown);

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS) {
        _moveFrmImages[6].lock(InterfaceFrameId::AllButtonPressed);
        _moveFrmImages[7].lock(InterfaceFrameId::AllButtonUnpressed);

        if (_moveFrmImages[6].isLocked() && _moveFrmImages[7].isLocked()) {
            // ALL
            messageListItem.num = 22;
            if (messageListGetItem(&gInventoryMessageList, &messageListItem)) {
                int length = fontGetStringWidth(messageListItem.text);

                // TODO: Where is y? Is it hardcoded in to 376?
                fontDrawText(_moveFrmImages[6].getData() + (94 - length) / 2 + 376, messageListItem.text, 200, 94, COLOR_YELLOW_2);
                fontDrawText(_moveFrmImages[7].getData() + (94 - length) / 2 + 470, messageListItem.text, 200, 94, COLOR_DARK_YELLOW_4);

                btn = buttonCreate(_mt_wid,
                    120, 80, 94, 33, -1, -1, -1, 5000,
                    _moveFrmImages[6].getData(),
                    _moveFrmImages[7].getData(),
                    nullptr,
                    BUTTON_FLAG_TRANSPARENT);
                if (btn != -1) {
                    buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
                }
            }
        }
    }

    windowRefresh(_mt_wid);
    inventorySetCursor(INVENTORY_WINDOW_CURSOR_ARROW);
    fontSetCurrent(oldFont);

    return 0;
}

// 0x477030 exit_move_timer_win
static int inventoryQuantityWindowFree(int inventoryWindowType)
{
    int count = inventoryWindowType == INVENTORY_WINDOW_TYPE_MOVE_ITEMS ? 8 : 6;

    for (int index = 0; index < count; index++) {
        _moveFrmImages[index].unlock();
    }

    windowDestroy(_mt_wid);

    return 0;
}

// 0x477074 inven_set_timer
int inventorySetTimer(Object* item)
{
    bool isInitialized = _inven_is_initialized;

    if (!isInitialized) {
        if (inventoryCommonInit() == -1) {
            return -1;
        }
    }

    int seconds = inventoryQuantitySelect(INVENTORY_WINDOW_TYPE_SET_TIMER, item, 180);

    if (!isInitialized) {
        // NOTE: Uninline.
        inventoryCommonFree();
    }

    return seconds;
}

int inventoryGetWindow()
{
    return windowGetWindow(gInventoryWindow) != nullptr ? gInventoryWindow : -1;
}

void inventoryDisplayStats()
{
    if (windowGetWindow(gInventoryWindow) == nullptr) {
        return;
    }

    inventoryRenderSummary();
    windowRefresh(gInventoryWindow);
}

void inventoryRedraw(int redrawSide)
{
    if (windowGetWindow(gInventoryWindow) == nullptr) {
        return;
    }

    InventoryWindowType inventoryWindowType;
    if (!inventoryGetCurrentWindowType(&inventoryWindowType)) {
        return;
    }

    if (redrawSide <= 0) {
        _stack_offset[_curr_stack] = 0;
        _display_inventory(0, -1, inventoryWindowType);
    }

    if (redrawSide != 0
        && (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT
            || inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE)) {
        _target_stack_offset[_target_curr_stack] = 0;
        _display_target_inventory(0, -1, _target_pud, inventoryWindowType);
        windowRefresh(gInventoryWindow);
    }
}

Object* inventoryGetTargetObject()
{
    return _target_stack[_target_curr_stack];
}

int inventoryUnwieldSlot(Object* critter, InvenSlot slot)
{
    if (critter == nullptr) {
        return -1;
    }

    bool isDude = critter == gDude;
    InventoryWindowType inventoryWindowType;
    bool uiModeActive = inventoryGetCurrentWindowType(&inventoryWindowType);
    bool update = false;

    if (slot != InvenSlot::Armor && !uiModeActive) {
        int rc = inventoryUnequipFunc(critter, slot == InvenSlot::LeftHand ? HAND_LEFT : HAND_RIGHT, false);
        if (rc == 0) {
            update = isDude;
        }

        if (update) {
            interfaceUpdateItems(false, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT);
        }
        return rc;
    }

    bool forceAdd = false;
    Object* item = nullptr;
    Object** itemSlot = nullptr;
    if (slot != InvenSlot::Armor) {
        if (!isDude || critter != _inven_dude) {
            return 0;
        }

        if (slot == InvenSlot::RightHand) {
            item = gInventoryRightHandItem;
            itemSlot = &gInventoryRightHandItem;
        } else {
            item = gInventoryLeftHandItem;
            itemSlot = &gInventoryLeftHandItem;
        }

        if (item != nullptr) {
            if (!scriptHooks_InvenWield(critter, item, slot, 0, 0, false)) {
                return -1;
            }

            int oldFlags = item->flags;
            bool clearBothHandSlots = gInventoryLeftHandItem == item && gInventoryRightHandItem == item;
            if (clearBothHandSlots) {
                gInventoryLeftHandItem = nullptr;
                gInventoryRightHandItem = nullptr;
            } else {
                *itemSlot = nullptr;
            }
            item->flags &= ~OBJECT_IN_ANY_HAND;

            if (itemAdd(critter, item, 1) != 0) {
                // item will be in an invalid state, but this can only happen on malloc failure
                return -1;
            }

            // CE: This branch bypasses inventoryUnequipFunc(), so restore light here too.
            if (ProtoId(item) == ItemProtoTypeId::LitFlare) {
                critterRestoreLightWithoutFlare(critter);
            }

            update = true;
        }
    } else {
        if (isDude && critter == _inven_dude) {
            item = gInventoryArmor;
        }

        if (item == nullptr) {
            item = critterGetArmor(critter);
        } else {
            gInventoryArmor = nullptr;
            forceAdd = true;
        }

        if (item != nullptr) {
            item->flags &= ~OBJECT_WORN;
            if (correctFidForRemovedItem(critter, item, OBJECT_WORN) != 0) {
                if (forceAdd) {
                    gInventoryArmor = item;
                    item->flags |= OBJECT_WORN;
                }
                return -1;
            }

            if (forceAdd && itemAdd(critter, item, 1) != 0) {
                // improper cleanup, but this can only happen on malloc failure
                return -1;
            }

            if (isDude) {
                interfaceRenderArmorClass(false);
            }
        }
    }

    if (!uiModeActive) {
        return 0;
    }

    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_NORMAL) {
        inventoryRenderSummary();
    }

    FrmId targetFrmId = FrmId::Empty();
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_LOOT
        || inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        targetFrmId = FrmId(_target_stack[_target_curr_stack]->fid);
        _display_target_inventory(_target_stack_offset[_target_curr_stack], -1, _target_pud, inventoryWindowType);
    }

    _adjust_fid();
    _display_inventory(_stack_offset[_curr_stack], -1, inventoryWindowType);
    _display_body(targetFrmId, inventoryWindowType);
    if (inventoryWindowType == INVENTORY_WINDOW_TYPE_TRADE) {
        barterDisplayTables(gInventoryBarterBackgroundWindow, gPlayerTableObj, gBartererTableObj, -1);
    }

    if (update) {
        interfaceUpdateItems(false, INTERFACE_ITEM_ACTION_DEFAULT, INTERFACE_ITEM_ACTION_DEFAULT);
    }

    return 0;
}

} // namespace fallout
