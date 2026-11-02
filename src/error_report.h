#ifndef FALLOUT_ERROR_REPORT_H_
#define FALLOUT_ERROR_REPORT_H_

#include <stdarg.h>

#include <string>

namespace fallout {

// CE: Problem reports (port). When something breaks that the player can't
// fix, a window says what happened and what it means for the saves, and the
// player sends us a short report through a channel of their choice. Nothing
// is sent by itself: what the window shows is exactly what goes.
//
// Two classes of problems:
// - the player's environment (no space, not enough memory): the window says
//   what fixes it, no report;
// - the game's errors: what happened, a fact about the saves when it is
//   certain, "it's the game's error, not yours - send us a report"; no
//   advice (a known error is fixed, not worked around).
//
// A report has a fixed set of fields only: the operation, the reason, a game
// file's name, the app's version, the platform and its version, the game's
// build (its mods and language); then the last understandable events (from
// the game journal's lines, `actionLog`) and the scripts' errors of the
// session. Never: the hero's name, saves' descriptions, paths outside the
// game's folder, files' contents, times.
//
// Call sites are one line where the engine already knows the reason; the
// window shows where the game can wait (`errorReportShowPending`).

// Why something failed. Codes in the report are their names (`kReasons` in
// error_report.cc), texts are in ce.msg.
enum class ErrorReason {
    None,
    // A save (slot) didn't load: its SAVE.DAT can't be opened, its header
    // is wrong, one of its parts (`detail`: the load handler's index) can't
    // be read, sfall's data of it (sfallgv.sav) can't be read.
    SaveFile,
    SaveHeader,
    SavePart,
    SaveSfall,
    // A map (`file`) didn't load, at the stage the engine names
    // (`errorReportMapLoad`).
    MapFile,
    MapHeader,
    MapVersion,
    MapVariables,
    MapScripts,
    MapObjects,
    MapElevation,
    MapEntrance,
    MapScript,
    MapEncounter,
    // The update's file failed its checks (`detail`: which one), or the
    // system didn't install it.
    UpdateCheck,
    UpdateInstall,
    // Replacing the game (the app's import screen) failed (`detail`: the
    // import screen's reason).
    ReplaceGame,
    // The game closed unexpectedly the previous time (`detail`: the
    // system's reason, when it tells).
    Crash,
    // The system closed the game for lack of memory (environment).
    LowMemory,
    // "Report a problem" in the settings' About section.
    Manual,
};

// map.cc: the map [fileName] didn't load; [error] is the engine's text of
// the stage (`mapLoadByName`'s "... while loading map.").
void errorReportMapLoad(const char* fileName, const char* error);

// loadsave.cc: the save in [slot] (0-based) didn't load for [reason];
// [part] - the load handler's index (SavePart).
void errorReportSaveLoad(ErrorReason reason, int slot, int part = -1);

// The update failed for [reason] ([detail]: the platform's check that
// failed). Shown at once (the update window waits under it).
void errorReportUpdate(ErrorReason reason, const char* detail);

// Shows the problem kept since it happened, if any (a modal window): where
// the game can wait - its main loop, the main menu, a failed load. False
// when there is none.
bool errorReportShowPending();

// The settings' "Report a problem": the window with no failure (the last
// events and the scripts' errors only).
void errorReportShowManual();

// interpreter.cc `programFatalError`: the script [script] (its file, null
// when none runs) stopped with an error. Kept for the report (the same one
// once, at most 20), never a window.
void errorReportScriptError(const char* script, const char* format, va_list args);

// action_log.cc: an understandable line of the game journal for the last
// events (20 kept in memory; the report shows the latest).
void errorReportEvent(const char* text);

// The game starts (after the command line): a previous run that didn't end
// cleanly is a problem shown in the main menu, this one is marked running.
void errorReportStart();
// The game goes to the background ([active] false) or comes back: closing it
// in the background is the system's right, not a crash.
void errorReportFocus(bool active);
// The game closes cleanly.
void errorReportExit();
// Every presented frame (svga.cc): the hang watchdog of the platform counts
// them.
void errorReportFrame();

// For automated tests: the text the window shows for [reason] now (UTF-8),
// what its channels would send; the text of the window showing now (empty
// without one or for an environment problem); a problem raised as the
// engine would, with [detail].
std::string errorReportTestText(ErrorReason reason);
std::string errorReportTestShown();
void errorReportTestRaise(ErrorReason reason, const char* detail);

} // namespace fallout

#endif /* FALLOUT_ERROR_REPORT_H_ */
