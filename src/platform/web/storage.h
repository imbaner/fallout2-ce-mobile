#ifndef FALLOUT_PLATFORM_WEB_STORAGE_H_
#define FALLOUT_PLATFORM_WEB_STORAGE_H_

#include <string>

namespace fallout {

// Stable IDBFS mount/database identity, shared with os/web/working-files.js.
constexpr char kWebUserRoot[] = "/fallout2-ce-mobile-userdata";
// The game's own part of it (the page's Module.webGameUserRoot: the first
// game's is kWebUserRoot itself, another's kWebUserRoot/games/<id>): its
// savegame, settings and port folders.
const std::string& webGameUserRoot();
std::string webPortDataPath(const char* name);
// Both finish outside the normal engine stack through Asyncify. Errors are
// returned to the caller; no rejected JS promise crosses back into the engine.
bool webCommitUserFiles();
bool webRollbackUserFiles();
void webUserWriteFailed();

} // namespace fallout

#endif
