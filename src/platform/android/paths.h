#ifndef FALLOUT_PLATFORM_ANDROID_PATHS_H_
#define FALLOUT_PLATFORM_ANDROID_PATHS_H_

#include <string>

namespace fallout {

// The app's folders, as it chose them (MainActivity, Games.java): every game
// lives in a folder of its own.

// The current game's folder, as on a computer (the engine works in it).
const std::string& androidGameFolder();

// The current game's private folder: the port's records of its saves
// (save_records.h, save_compatibility.h).
const std::string& androidGameDataFolder();

// [name] in the app's own folder, shared by all its games: the engine's data
// (ce.dat) and the marks of a run (error_report.h).
std::string androidAppFile(const char* name);

} // namespace fallout

#endif /* FALLOUT_PLATFORM_ANDROID_PATHS_H_ */
