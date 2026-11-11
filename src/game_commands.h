#ifndef FALLOUT_GAME_COMMANDS_H_
#define FALLOUT_GAME_COMMANDS_H_

namespace fallout {

// CE: Commands of the game's interface: what the interface bar's buttons and
// the game's hotkeys do. The touch HUD and the mobile screens post them
// instead of pressing keys; the game's hotkeys (`gameHandleKey`) execute the
// same commands, so both run one code path.
//
// Posted commands wait for the game loop (the main loop, or the player's
// turn in combat), which executes them one per frame like keys: screens
// post them while drawing, the game's screens they open run their own
// loops. Pending commands are dropped with pending input
// (`inputEventQueueReset`, e.g. when the player's turn starts).
enum class GameCommandType {
    None,
    // Game menu (Esc).
    Menu,
    Inventory,
    Character,
    Pipboy,
    Automap,
    Skilldex,
    Sneak,
    SwapHands,
    // Active item's action (the bar's item button).
    UseItem,
    // The active weapon reloaded, its attack mode kept.
    Reload,
    // Combat begun without an attack (the game's A key).
    StartCombat,
    QuickSave,
    QuickLoad,
    // Taken by the combat loop (Space, Enter). EndCombat's `value`
    // kGameCommandEndCombatOrTurn: refused, it ends the turn.
    EndTurn,
    EndCombat,
    // Touch HUD's own action, `value` - its event (touch_hud.cc).
    Hud,
};

constexpr int kGameCommandEndCombatOrTurn = 1;

struct GameCommand {
    GameCommandType type = GameCommandType::None;
    int value = 0;
};

// Adds a command for the game loop. The same command already waiting isn't
// added again (two taps before the loop took the first).
void gameCommandPost(GameCommandType type, int value = 0);

// The oldest pending command, `false` when none.
bool gameCommandTake(GameCommand* command);

void gameCommandsClear();

// Runs [command] as the game's hotkey does. `isInCombatMode` - from the
// combat loop.
void gameCommandExecute(const GameCommand& command, bool isInCombatMode);

} // namespace fallout

#endif /* FALLOUT_GAME_COMMANDS_H_ */
