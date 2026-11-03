#ifndef LOAD_SAVE_GAME_H
#define LOAD_SAVE_GAME_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fallout {

enum class LoadSaveMode : int {
    FromMainMenu, // Special case - loading game from main menu.
    Normal, // Normal (full-screen) save/load screen.
    Quick, // Quick load/save.
};

void _InitLoadSave();
#ifdef __EMSCRIPTEN__
// Commit the complete user operation, or restore the last durable user state.
bool lsgWebCommitUserChanges(bool prepared, int previousSession = -1);
void lsgWebRestoreQuickRange(int previousCount, int previousSession);
#endif
void _ResetLoadSave();
int lsgSaveGame(LoadSaveMode mode);
// CE: Saves to [slot] (0-based) with [description] without the save window,
// like a quick save (automated tests).
int lsgSaveGameToSlot(int slot, const char* description);
// CE: Loads [slot] (0-based) without the load window (automated tests).
int lsgLoadGameFromSlot(int slot);
int lsgLoadGame(LoadSaveMode mode);

// CE: Saves for the mobile save/load screen (mui_loadsave.cc), opened by
// `lsgSaveGame` / `lsgLoadGame` under the mobile UI. Slots are the game's
// zero-based slots; placement, order and quick load are the save catalog's
// (save_catalog.h).

enum class MobileSaveSlotState {
    Empty,
    Occupied,
    Corrupt,
    OldVersion,
};

struct MobileSaveSlotInfo {
    MobileSaveSlotState state = MobileSaveSlotState::Empty;
    // Game text (cp1251).
    char description[30] = {};
    char characterName[32] = {};
    int gameDay = 0;
    int gameMonth = 0;
    int gameYear = 0;
    unsigned int gameTime = 0;
    int map = -1;
    int elevation = 0;
    // When it was made (Unix seconds, 0 - unknown) and the creation order.
    std::int64_t created = 0;
    std::int64_t order = 0;
};

// Reads the save folder again (and the port's records of its saves).
void lsgMobileRefreshSlots();
// The game is back in front: the app's import of saves may have changed the
// saves and their records meanwhile - read again (once loadsave is set up).
void lsgMobileSavesMayHaveChanged();
bool lsgMobileGetSlotInfo(int slot, MobileSaveSlotInfo* info);
// SAVE.DAT of [slot] (save_compatibility.h).
std::string lsgMobileSaveDatPath(int slot);
// SAVE.DATs of all slots with something in them.
std::vector<std::string> lsgAllSaveDatPaths();
// Slot in the automatic quick save range.
bool lsgMobileIsQuickSlot(int slot);
// Slot of a new manual save, -1 - none left.
int lsgFindFreeManualSlot();
// Saves the game to [slot] with [description] (game text). On failure shows
// the game's error. 0 - saved.
int lsgMobileSaveGame(int slot, const char* description);
// Loads [slot]. On failure shows the game's error and returns to the main
// menu, as the game's window does. 0 - loaded.
int lsgMobileLoadGame(int slot);
// Removes the slot with all its files (maps, mod files, previews).
bool lsgMobileDeleteSlot(int slot);
// Makes quick save [slot] a manual one (moved to a free manual slot with
// all its files, its place in the list stays). Returns its slot or -1.
int lsgMakeQuickSavePermanent(int slot);

// Quick saves (`[ui] auto_quick_save` pages of 10), 0 - none: the quick save
// button opens the save screen.
int lsgQuickSaveCount();
// How many quick saves would become manual with [count] of them.
int lsgQuickSavesOverCount(int count);
// Sets the number of quick saves ([count] rounded down to tens, up to 100)
// with the saves moved so none is lost (`SaveCatalog::setQuickRange`), and
// the setting (written by the caller with the settings file). False - a
// move failed, the number stays.
bool lsgSetQuickSaveCount(int count, bool persist = true);
// Slot quick load loads (the last save loaded or made in this session), -1.
int lsgSessionLoadSlot();

// The save made last (the main menu's Continue), -1 - none.
int lsgMobileNewestSlot();

// Main menu's Continue: loads the save made last as the load screen from the
// main menu does. 1 - loaded, 0 - no save, -1 - loading failed (the game's
// error shown).
int lsgContinueGame();
// Text [messageId] of LSGAME.MSG while the mobile screen is open.
const char* lsgGetMessage(int messageId);

// The game's thumbnail in SAVE.DAT: palette indices.
constexpr int kMobileSavePreviewWidth = 224;
constexpr int kMobileSavePreviewHeight = 133;
bool lsgMobileReadPreview(int slot, unsigned char* pixels, size_t size);
bool lsgMobileCapturePreview(unsigned char* pixels, size_t size);

// Wide RGBA preview (PREVIEW.PNG next to SAVE.DAT, saves made with the
// mobile UI; older ones have only the thumbnail), at most this big.
constexpr int kMobileWidePreviewMaxSize = 800;
bool lsgMobileReadWidePreview(int slot, std::vector<unsigned char>* rgba, int* width, int* height);
bool lsgMobileCaptureWidePreview(std::vector<unsigned char>* rgba, int* width, int* height);

void lsgDevSetLoadGameSlot(int slot);
int lsgGetTotalSlotCount();
bool _isLoadingGame();
int mapIdBeingLoaded();
void lsgInit();
int MapDirErase(const char* path, const char* extension);
int _MapDirEraseFile_(const char* relativePath, const char* fileName);

} // namespace fallout

#endif /* LOAD_SAVE_GAME_H */
