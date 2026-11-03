#include "game_commands.h"

#include <deque>
#include <string>

#include "action_log.h"
#include "actions.h"
#include "automap.h"
#include "character_editor.h"
#include "combat.h"
#include "debug.h"
#include "display_monitor.h"
#include "game.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "interface.h"
#include "inventory.h"
#include "loadsave.h"
#include "map.h"
#include "message.h"
#include "options.h"
#include "pipboy.h"
#include "skill.h"
#include "skilldex.h"
#include "touch_hud.h"

namespace fallout {

static std::deque<GameCommand> gGameCommands;

void gameCommandPost(GameCommandType type, int value)
{
    if (type == GameCommandType::None) {
        return;
    }

    for (const GameCommand& command : gGameCommands) {
        if (command.type == type && command.value == value) {
            return;
        }
    }

    gGameCommands.push_back({ type, value });
}

bool gameCommandTake(GameCommand* command)
{
    if (gGameCommands.empty()) {
        return false;
    }

    *command = gGameCommands.front();
    gGameCommands.pop_front();
    return true;
}

void gameCommandsClear()
{
    gGameCommands.clear();
}

static const char* gameCommandName(GameCommandType type)
{
    switch (type) {
    case GameCommandType::None:
        return "none";
    case GameCommandType::Menu:
        return "menu";
    case GameCommandType::Inventory:
        return "inventory";
    case GameCommandType::Character:
        return "character";
    case GameCommandType::Pipboy:
        return "pipboy";
    case GameCommandType::Automap:
        return "automap";
    case GameCommandType::Skilldex:
        return "skilldex";
    case GameCommandType::Sneak:
        return "sneak";
    case GameCommandType::SwapHands:
        return "swap hands";
    case GameCommandType::UseItem:
        return "use item";
    case GameCommandType::Reload:
        return "reload";
    case GameCommandType::StartCombat:
        return "start combat";
    case GameCommandType::QuickSave:
        return "quick save";
    case GameCommandType::QuickLoad:
        return "quick load";
    case GameCommandType::EndTurn:
        return "end turn";
    case GameCommandType::EndCombat:
        return "end combat";
    case GameCommandType::Hud:
        return "hud";
    }
    return "?";
}

void gameCommandExecute(const GameCommand& command, bool isInCombatMode)
{
    actionLog("command %s%s%s", gameCommandName(command.type),
        command.type == GameCommandType::Hud ? (" " + std::to_string(command.value)).c_str() : "",
        isInCombatMode ? " (combat)" : "");

    switch (command.type) {
    case GameCommandType::None:
        break;
    case GameCommandType::Menu:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            showOptions();
        }
        break;
    case GameCommandType::Inventory:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            inventoryOpen();
        }
        break;
    case GameCommandType::Character:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            bool isoWasEnabled = isoDisable();
            characterEditorShow(false);
            if (isoWasEnabled) {
                isoEnable();
            }
        }
        break;
    case GameCommandType::Pipboy:
        if (interfaceBarEnabled()) {
            if (isInCombatMode) {
                pipboyShowUnavailable(PipboyUnavailable::InCombat);
            } else {
                soundPlayFile("ib1p1xx1");
                pipboyOpen(PIPBOY_OPEN_INTENT_UNSPECIFIED);
            }
        }
        break;
    case GameCommandType::Automap:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            automapShow(true, false);
        }
        break;
    case GameCommandType::Skilldex:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            gameHandleSkilldexResult(skilldexOpen());
        }
        break;
    case GameCommandType::Sneak:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            gameMouseSetCursor(MOUSE_CURSOR_USE_CROSSHAIR);
            _action_skill_use(SKILL_SNEAK);
        }
        break;
    case GameCommandType::SwapHands:
        if (interfaceBarEnabled()) {
            soundPlayFile("ib1p1xx1");
            interfaceBarSwapHands(true);
        }
        break;
    case GameCommandType::UseItem:
        if (interfaceBarEnabled()) {
            _intface_use_item();
        }
        break;
    case GameCommandType::Reload:
        if (interfaceBarEnabled()) {
            interfaceReloadCurrentWeapon();
        }
        break;
    case GameCommandType::StartCombat:
        // As the A key: the bar's weapon button starts combat so too, with
        // the crosshair the touch controls set on their own.
        if (interfaceBarEnabled() && !isInCombatMode) {
            _combat(nullptr);
        }
        break;
    case GameCommandType::QuickSave: {
        soundPlayFile("ib1p1xx1");

        int rc = lsgSaveGame(LoadSaveMode::Quick);
        if (rc == -1) {
            debugPrint("\n ** Error calling SaveGame()! **\n");
        } else if (rc == 1) {
            MessageListItem messageListItem;
            // Quick save game successfully saved.
            char* msg = getmsg(&gMiscMessageList, &messageListItem, 5);
            displayMonitorAddMessage(msg);
        }
        break;
    }
    case GameCommandType::QuickLoad: {
        soundPlayFile("ib1p1xx1");

        int rc = lsgLoadGame(LoadSaveMode::Quick);
        if (rc == -1) {
            debugPrint("\n ** Error calling LoadGame()! **\n");
        } else if (rc == 1) {
            MessageListItem messageListItem;
            // Quick load game successfully loaded.
            char* msg = getmsg(&gMiscMessageList, &messageListItem, 4);
            displayMonitorAddMessage(msg);
        }
        break;
    }
    case GameCommandType::EndTurn:
    case GameCommandType::EndCombat:
        // Only the combat loop ends turns (see `_combat_input`).
        break;
    case GameCommandType::Hud:
        touchHudHandleEvent(command.value);
        break;
    }
}

} // namespace fallout
