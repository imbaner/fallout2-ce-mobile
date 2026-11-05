#include "error_report.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <SDL.h>

#ifdef __ANDROID__
#include <jni.h>
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include "app_version.h"
#include "debug.h"
#include "dev_autotest.h"
#include "loadsave.h"
#include "map.h"
#include "mui.h"
#include "mui_draw.h"
#include "mui_screens.h"
#include "platform_compat.h"
#include "settings.h"
#include "worldmap.h"
#include "xfile.h"

namespace fallout {

namespace {

    // The Google Form's prefill link up to its report field's value (the
    // form's first question; the other two are the player's own, optional).
    // Empty hides its button.
    constexpr const char* kFormPrefillUrl = "https://docs.google.com/forms/d/e/1FAIpQLSfxwxftP2naSS9T5-maVG-jC9vti_2dnRACLccl2whZbW4IJw/viewform?usp=pp_url&entry.878318686=";
    // The channel's direct messages (kPortTelegramUrl), the report as the
    // draft. Telegram may leave the draft out: the report is copied first.
    constexpr const char* kTelegramDirectUrl = "https://t.me/imbaner?direct&text=";

    // In the game's folder: the run going on (its last events, for the next
    // start when it ends unexpectedly), and a problem the app's import
    // screen left for the game (one "reason detail" line).
    constexpr const char* kRunningFile = "running.txt";
    constexpr const char* kTestRunningFile = "running-test.txt";
    // What the system tells of the crash in a test.
    constexpr const char* kTestStackFile = "exit-stack-test.txt";
    constexpr const char* kProblemFile = "problem.txt";
    constexpr Uint32 kRunningWriteMs = 5000;

    constexpr size_t kEventsKept = 20;
    constexpr size_t kEventsShown = 8;
    constexpr size_t kScriptErrorsKept = 20;
    constexpr size_t kScriptErrorsShown = 5;
    constexpr size_t kLineLength = 120;
    constexpr size_t kStackLinesShown = 8;

    // ce.msg.
    constexpr int kTextTitle = 470;
    constexpr int kTextNotYours = 471;
    constexpr int kTextSavesSafe = 472;
    constexpr int kTextForm = 473;
    constexpr int kTextNeedsAccount = 474;
    constexpr int kTextNoAccount = 475;
    constexpr int kTextCopy = 476;
    constexpr int kTextCopied = 477;
    constexpr int kTextExportSaves = 342;
    constexpr int kTextReportTitle = 442;
    constexpr int kTextManual = 478;
    constexpr int kTextCrashTitle = 479;
    constexpr int kTextCrash = 480;
    constexpr int kTextCrashNow = 481;
    constexpr int kTextSystemClosed = 482;
    constexpr int kTextLowMemory = 483;
    constexpr int kTextBrowserMemory = 484;
    constexpr int kTextSave = 485;
    constexpr int kTextMap = 486;
    constexpr int kTextUpdate = 487;
    constexpr int kTextReplace = 488;
    constexpr int kTextHangTitle = 489;
    constexpr int kTextHang = 490;
    constexpr int kTextWait = 491;
    constexpr int kTextClose = 492;
    constexpr int kTextSaveMap = 493;

    struct Reason {
        ErrorReason reason;
        // In the report's code line.
        const char* code;
        // What failed, in the window ("%s" of its operation's text).
        int textId;
        const char* fallback;
    };

    const Reason kReasons[] = {
        { ErrorReason::None, "none", 0, "" },
        { ErrorReason::SaveFile, "save-file", 500, "its file can't be opened" },
        { ErrorReason::SaveHeader, "save-header", 501, "its header can't be read" },
        { ErrorReason::SavePart, "save-part", 502, "its data can't be read" },
        { ErrorReason::SaveSfall, "save-sfall", 503, "its sfall data can't be read" },
        { ErrorReason::MapFile, "map-file", 504, "its file wasn't found" },
        { ErrorReason::MapHeader, "map-header", 505, "its file is damaged" },
        { ErrorReason::MapVersion, "map-version", 506, "its file is of another version" },
        { ErrorReason::MapVariables, "map-variables", 507, "its variables can't be read" },
        { ErrorReason::MapScripts, "map-scripts", 508, "its scripts can't be read" },
        { ErrorReason::MapObjects, "map-objects", 509, "its objects can't be read" },
        { ErrorReason::MapElevation, "map-elevation", 510, "the entrance's level doesn't exist" },
        { ErrorReason::MapEntrance, "map-entrance", 511, "the entrance has no tile" },
        { ErrorReason::MapScript, "map-script", 512, "its script didn't start" },
        { ErrorReason::MapEncounter, "map-encounter", 513, "the random encounter wasn't set up" },
        { ErrorReason::UpdateCheck, "update-check", 514, "its file failed the checks" },
        { ErrorReason::UpdateInstall, "update-install", 515, "the system didn't install it" },
        { ErrorReason::ReplaceGame, "replace-game", 0, "" },
        { ErrorReason::Crash, "crash", 0, "" },
        { ErrorReason::LowMemory, "low-memory", 0, "" },
        { ErrorReason::Manual, "manual", 0, "" },
    };

    // `mapLoadByName`'s stages as it names them.
    const struct {
        const char* error;
        ErrorReason reason;
    } kMapStages[] = {
        { "Invalid file handle", ErrorReason::MapFile },
        { "Error reading header", ErrorReason::MapHeader },
        { "Invalid map version", ErrorReason::MapVersion },
        { "Error allocating global vars", ErrorReason::MapVariables },
        { "Error loading global vars", ErrorReason::MapVariables },
        { "Error allocating local vars", ErrorReason::MapVariables },
        { "Error loading local vars", ErrorReason::MapVariables },
        { "Error reading scripts", ErrorReason::MapScripts },
        { "Error reading objects", ErrorReason::MapObjects },
        { "Error setting map elevation", ErrorReason::MapElevation },
        { "Error setting tile center", ErrorReason::MapEntrance },
        { "Error creating new map script", ErrorReason::MapScript },
        { "Error Setting up random encounter", ErrorReason::MapEncounter },
    };

    // A save's parts in `_master_load_list`'s order (loadsave.cc).
    const char* const kSaveParts[] = {
        "prepare",
        "dude-id",
        "global-vars",
        "maps",
        "global-vars-skip",
        "dude",
        "critters",
        "kills",
        "skills",
        "random",
        "perks",
        "combat",
        "ai",
        "stats",
        "items",
        "traits",
        "automap",
        "preferences",
        "character",
        "worldmap",
        "pipboy",
        "movies",
        "skill-usage",
        "party",
        "queue",
        "interface",
        "end",
    };

    struct Problem {
        ErrorReason reason = ErrorReason::None;
        // The game's file (a map, a save's slot folder).
        std::string file;
        // What the reason's code doesn't tell (a save's part, the system's
        // reason).
        std::string detail;
        // A map that didn't load inside a save's load.
        ErrorReason cause = ErrorReason::None;
        std::string causeFile;
        std::u32string causePlace;
        // The map's name as the game shows it.
        std::u32string place;
        // The last events and scripts' errors of a run that ended
        // unexpectedly (`kRunningFile`).
        std::vector<std::string> previous;
        // Where a native crash happened, as the system tells (`exitStack`).
        std::vector<std::string> stack;
    };

    struct ScriptError {
        std::string text;
        int count = 1;
    };

    Problem gPending;
    // The report of the window showing (automated tests).
    std::string gShownText;

    // Written by the game, read by the platform's watchdog (another thread
    // on Android): the events, the scripts' errors and the texts below.
    std::mutex gMutex;
    std::vector<std::string> gEvents;
    std::vector<ScriptError> gScriptErrors;
    int gScriptErrorsDropped = 0;
    bool gContextChanged = false;

    // What the platform shows when the game can't (a hang, the browser's
    // engine stopped): JSON, rebuilt when the context changes.
    std::string gPlatformTexts;
    bool gPlatformTextsDirty = true;
    Uint32 gPlatformTextsTime = 0;
    constexpr Uint32 kPlatformTextsMs = 1000;

    std::atomic<unsigned int> gFrames { 0 };
    std::atomic<int> gLoading { 0 };

    bool gStarted = false;
    bool gRunningMarked = false;
    Uint32 gLastRunningWrite = 0;

    // ---- texts

    void appendUtf8(std::string& out, char32_t ch)
    {
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else if (ch < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (ch >> 18)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }

    std::string utf8(const std::u32string& text)
    {
        std::string result;
        for (char32_t ch : text) {
            appendUtf8(result, ch);
        }
        return result;
    }

    std::u32string text(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }

    // [pattern]'s "%s" / "%d" by [args] in turn.
    std::u32string format(const std::u32string& pattern, const std::vector<std::u32string>& args)
    {
        std::u32string result;
        size_t next = 0;
        for (size_t index = 0; index < pattern.size(); index++) {
            if (pattern[index] == U'%' && index + 1 < pattern.size() && (pattern[index + 1] == U's' || pattern[index + 1] == U'd')) {
                if (next < args.size()) {
                    result += args[next++];
                }
                index++;
            } else {
                result.push_back(pattern[index]);
            }
        }
        return result;
    }

    const Reason& reasonInfo(ErrorReason reason)
    {
        for (const Reason& info : kReasons) {
            if (info.reason == reason) {
                return info;
            }
        }
        return kReasons[0];
    }

    bool isEnvironment(ErrorReason reason)
    {
        return reason == ErrorReason::LowMemory;
    }

    // A line of a report: printable, no hex numbers (code addresses, ids
    // the player can't tell; [withHex]: the lines of where a crash happened
    // are made of them), not too long.
    std::string cleanLine(const std::string& value, bool withHex = false)
    {
        std::string result;
        for (size_t index = 0; index < value.size() && result.size() < kLineLength; index++) {
            unsigned char ch = static_cast<unsigned char>(value[index]);
            if (!withHex && ch == '0' && index + 2 < value.size() && (value[index + 1] == 'x' || value[index + 1] == 'X') && isxdigit(static_cast<unsigned char>(value[index + 2]))) {
                index += 2;
                while (index + 1 < value.size() && isxdigit(static_cast<unsigned char>(value[index + 1]))) {
                    index++;
                }
                result += "…";
                continue;
            }
            if (ch < 0x20) {
                if (!result.empty() && result.back() != ' ') {
                    result.push_back(' ');
                }
                continue;
            }
            result.push_back(static_cast<char>(ch));
        }
        while (!result.empty() && result.back() == ' ') {
            result.pop_back();
        }
        // Not cut inside a character.
        while (!result.empty() && (static_cast<unsigned char>(result.back()) & 0xC0) == 0x80) {
            result.pop_back();
        }
        if (!result.empty() && (static_cast<unsigned char>(result.back()) & 0x80) != 0) {
            result.pop_back();
        }
        return result;
    }

    // "maps\NEWRGO.SAV" -> "NEWRGO.SAV".
    std::string baseName(const char* path)
    {
        if (path == nullptr) {
            return std::string();
        }
        const char* name = path;
        for (const char* at = path; *at != '\0'; at++) {
            if (*at == '\\' || *at == '/') {
                name = at + 1;
            }
        }
        return name;
    }

    // ---- what the game is

#ifdef __EMSCRIPTEN__
    // clang-format off
    // "Web, iOS 17.1, Safari" (ASCII) into [buffer]. (No backslashes: the
    // code is a C string.)
    EM_JS(void, webPlatformJs, (char* buffer, int capacity), {
        const agent = navigator.userAgent;
        let os = 'other';
        let match;
        if ((match = agent.match(/(?:iPhone|CPU) OS ([0-9]+)_([0-9]+)/))) os = 'iOS ' + match[1] + '.' + match[2];
        else if ((match = agent.match(/Android ([0-9]+(?:[.][0-9]+)?)/))) os = 'Android ' + match[1];
        else if (/Macintosh/.test(agent)) os = navigator.maxTouchPoints > 1 ? 'iPadOS' : 'macOS';
        else if (/Windows/.test(agent)) os = 'Windows';
        else if (/Linux/.test(agent)) os = 'Linux';
        const browser = /Edg[/]/.test(agent) ? 'Edge' : /(CriOS|Chrome)[/]/.test(agent) ? 'Chrome'
            : /(FxiOS|Firefox)[/]/.test(agent) ? 'Firefox' : /Safari[/]/.test(agent) ? 'Safari' : 'other';
        const value = 'Web, ' + os + ', ' + browser;
        for (let index = 0; index < value.length && index < capacity; index++) HEAPU8[buffer + index] = value.charCodeAt(index) & 0x7f;
    });

    // The page reads the watchdog's state and the texts from [state]
    // (`gWebState`) without calling the engine: it may be waiting or
    // stopped.
    EM_JS(void, webPublishErrorReportJs, (int* state), {
        const at = state >> 2;
        Module['webErrorReport'] = {
            state: () => ({frames: HEAP32[at], loading: HEAP32[at + 1] !== 0}),
            texts: () => UTF8ToString(HEAP32[at + 2]),
        };
    });
    // clang-format on
#endif

    std::string platformName()
    {
#if defined(__ANDROID__)
        std::string result = "Android";
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        if (env != nullptr) {
            jclass version = env->FindClass("android/os/Build$VERSION");
            if (version != nullptr) {
                jfieldID field = env->GetStaticFieldID(version, "RELEASE", "Ljava/lang/String;");
                jstring release = field != nullptr ? static_cast<jstring>(env->GetStaticObjectField(version, field)) : nullptr;
                if (release != nullptr) {
                    const char* chars = env->GetStringUTFChars(release, nullptr);
                    if (chars != nullptr) {
                        result += " ";
                        result += chars;
                        env->ReleaseStringUTFChars(release, chars);
                    }
                    env->DeleteLocalRef(release);
                }
                env->DeleteLocalRef(version);
            }
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }
        return result;
#elif defined(__EMSCRIPTEN__)
        char value[64] = { 0 };
        webPlatformJs(value, sizeof(value) - 1);
        return value;
#elif defined(__APPLE__)
        char version[32] = { 0 };
        size_t size = sizeof(version);
        std::string result = SDL_GetPlatform();
        if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) == 0) {
            result += " ";
            result += version;
        }
        return result;
#else
        return SDL_GetPlatform();
#endif
    }

    // The game's build: its mods (archives and folders in mods\, as the
    // engine opened them) and language.
    std::string gameBuild()
    {
        std::vector<std::string> mods;
        for (const XBase* base = xbaseGetFirst(); base != nullptr; base = base->next) {
            if (compat_strnicmp(base->path, "mods", 4) == 0 && (base->path[4] == '\\' || base->path[4] == '/')) {
                mods.push_back(baseName(base->path));
            }
        }
        std::string result = "Fallout 2, ";
        if (mods.empty()) {
            result += "no mods";
        } else {
            result += "mods: ";
            for (size_t index = 0; index < mods.size() && index < 8; index++) {
                result += (index > 0 ? ", " : "") + mods[index];
            }
            if (mods.size() > 8) {
                result += " +" + std::to_string(mods.size() - 8);
            }
        }
        return result + ", " + settings.system.language;
    }

    const std::string& buildLine()
    {
        static std::string line;
        if (line.empty()) {
            line = std::string("F2 Touch ") + appVersion() + " (" + appBuild() + ") · " + platformName() + " · " + gameBuild();
        }
        return line;
    }

    // The last events and the scripts' errors (under `gMutex`).
    std::vector<std::string> contextLines()
    {
        std::vector<std::string> lines;
        size_t shown = std::min(gScriptErrors.size(), kScriptErrorsShown);
        for (size_t index = gScriptErrors.size() - shown; index < gScriptErrors.size(); index++) {
            std::string line = "Script: " + gScriptErrors[index].text;
            if (gScriptErrors[index].count > 1) {
                line += " (x" + std::to_string(gScriptErrors[index].count) + ")";
            }
            lines.push_back(line);
        }
        size_t more = gScriptErrors.size() - shown + gScriptErrorsDropped;
        if (more > 0) {
            lines.push_back("Script: " + std::to_string(more) + " more");
        }
        if (!gEvents.empty()) {
            lines.push_back("Last events:");
            size_t first = gEvents.size() > kEventsShown ? gEvents.size() - kEventsShown : 0;
            for (size_t index = first; index < gEvents.size(); index++) {
                lines.push_back("- " + gEvents[index]);
            }
        }
        return lines;
    }

    // ---- the report

    std::u32string mapPlace(const char* fileName)
    {
        char name[16] = { 0 };
        std::string base = baseName(fileName);
        snprintf(name, sizeof(name), "%s", base.c_str());
        char* dot = strrchr(name, '.');
        if (dot != nullptr) {
            *dot = '\0';
        }
        int map = name[0] != '\0' ? static_cast<int>(wmMapMatchNameToIdx(name)) : -1;
        std::u32string place = map >= 0 ? muiPlaceText(map, 0) : std::u32string();
        return place.empty() ? muiDecodeUtf8(name) : place;
    }

    // The window's lines saying what happened (game errors), or the action
    // fixing it (environment).
    std::u32string whatHappened(const Problem& problem)
    {
        const Reason& info = reasonInfo(problem.reason);
        const Reason& cause = reasonInfo(problem.cause);
        switch (problem.reason) {
        case ErrorReason::SaveFile:
        case ErrorReason::SaveHeader:
        case ErrorReason::SavePart:
        case ErrorReason::SaveSfall: {
            std::u32string why = problem.cause != ErrorReason::None
                ? format(text(kTextSaveMap, "the map %s in it didn't load: %s"), { problem.causePlace, text(cause.textId, cause.fallback) })
                : text(info.textId, info.fallback);
            // Its slot is in the code line: the save list shows no numbers.
            return format(text(kTextSave, "The save didn't load: %s."), { why });
        }
        case ErrorReason::UpdateCheck:
        case ErrorReason::UpdateInstall:
            return format(text(kTextUpdate, "The update didn't install: %s."), { text(info.textId, info.fallback) });
        case ErrorReason::ReplaceGame:
            return text(kTextReplace, "Replacing the game didn't finish, the game stayed as it was.");
        case ErrorReason::Crash:
            return text(kTextCrash, "The game closed unexpectedly the last time.");
        case ErrorReason::LowMemory:
            return text(kTextLowMemory, "The system ran out of memory for the game. Close other apps before playing.");
        case ErrorReason::Manual:
            return text(kTextManual, "Tell us what went wrong: the text below goes along and helps us find the cause.");
        case ErrorReason::None:
            return std::u32string();
        default:
            // Maps.
            return format(text(kTextMap, "The map %s didn't load: %s."), { problem.place, text(info.textId, info.fallback) });
        }
    }

    // Loading never writes saves, nor do an update or replacing the game:
    // they are certainly as they were.
    bool savesSafe(ErrorReason reason)
    {
        return reason != ErrorReason::Crash && reason != ErrorReason::LowMemory && reason != ErrorReason::Manual && reason != ErrorReason::None;
    }

    // "Code: map-entrance · NEWRGO.SAV".
    std::string codeLine(const Problem& problem)
    {
        std::string line = std::string("Code: ") + reasonInfo(problem.reason).code;
        if (!problem.detail.empty() && problem.detail != reasonInfo(problem.reason).code) {
            line += " (" + problem.detail + ")";
        }
        if (!problem.file.empty()) {
            line += " · " + problem.file;
        }
        if (problem.cause != ErrorReason::None) {
            line += std::string("; ") + reasonInfo(problem.cause).code;
            if (!problem.causeFile.empty()) {
                line += " · " + problem.causeFile;
            }
        }
        return cleanLine(line);
    }

    struct Report {
        std::u32string title;
        // What happened, the saves, whose error it is.
        std::vector<std::u32string> lines;
        // What goes (UTF-8); empty for environment problems.
        std::string text;
        // The issue's title.
        std::string summary;
    };

    Report buildReport(const Problem& problem)
    {
        Report report;
        bool environment = isEnvironment(problem.reason);
        if (problem.reason == ErrorReason::Manual) {
            report.title = text(kTextReportTitle, "Report a problem");
        } else if (problem.reason == ErrorReason::Crash) {
            report.title = text(kTextCrashTitle, "The game closed unexpectedly");
        } else if (environment) {
            report.title = text(kTextSystemClosed, "The system closed the game");
        } else {
            report.title = text(kTextTitle, "An error in the game");
        }

        std::u32string happened = whatHappened(problem);
        report.lines.push_back(happened);
        if (environment) {
            return report;
        }
        if (problem.reason != ErrorReason::Manual) {
            std::u32string verdict = text(kTextNotYours, "It's the game's error, not yours: send us a report.");
            report.lines.push_back(savesSafe(problem.reason) ? text(kTextSavesSafe, "Your saves are not affected.") + U" " + verdict : verdict);
        }

        std::vector<std::string> lines;
        if (problem.reason != ErrorReason::Manual) {
            lines.push_back(cleanLine(utf8(happened)));
        }
        lines.push_back(codeLine(problem));
        lines.push_back(buildLine());
        lines.insert(lines.end(), problem.stack.begin(), problem.stack.end());
        if (!problem.previous.empty()) {
            lines.insert(lines.end(), problem.previous.begin(), problem.previous.end());
        } else {
            std::lock_guard<std::mutex> lock(gMutex);
            std::vector<std::string> context = contextLines();
            lines.insert(lines.end(), context.begin(), context.end());
        }
        for (const std::string& line : lines) {
            report.text += line + "\n";
        }
        report.summary = problem.reason == ErrorReason::Manual ? utf8(report.title) : cleanLine(utf8(happened));
        return report;
    }

    // ---- channels

    std::string urlEncode(const std::string& value)
    {
        static const char kHex[] = "0123456789ABCDEF";
        std::string result;
        for (unsigned char ch : value) {
            if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                result.push_back(static_cast<char>(ch));
            } else {
                result.push_back('%');
                result.push_back(kHex[ch >> 4]);
                result.push_back(kHex[ch & 15]);
            }
        }
        return result;
    }

    // Google Forms and GitHub refuse links longer than about 8 KB (a letter
    // of another alphabet takes 6 characters in a link): what doesn't fit
    // goes from the oldest events, then from the end.
    constexpr size_t kUrlLimit = 8000;

    std::string fittedText(std::string text, size_t room)
    {
        while (urlEncode(text).size() > room) {
            // The oldest event's line, else the last line.
            size_t start = text.find("\n- ");
            if (start != std::string::npos) {
                start += 1;
            } else {
                start = text.rfind('\n', text.size() - 2);
                start = start != std::string::npos ? start + 1 : 0;
            }
            size_t end = text.find('\n', start);
            text.erase(start, end != std::string::npos ? end + 1 - start : std::string::npos);
        }
        return text;
    }

    std::string formUrl(const Report& report)
    {
        if (kFormPrefillUrl[0] == '\0') {
            return std::string();
        }
        std::string prefix = kFormPrefillUrl;
        return prefix + urlEncode(fittedText(report.text, kUrlLimit - prefix.size()));
    }

    std::string githubUrl(const Report& report)
    {
        std::string prefix = std::string(kPortIssuesUrl) + "/new?title=" + urlEncode(report.summary) + "&body=" + urlEncode("```\n");
        std::string suffix = urlEncode("```\n");
        return prefix + urlEncode(fittedText(report.text, kUrlLimit - prefix.size() - suffix.size())) + suffix;
    }

    std::string telegramUrl(const Report& report)
    {
        std::string prefix = kTelegramDirectUrl;
        return prefix + urlEncode(fittedText(report.text, kUrlLimit - prefix.size()));
    }

    void openUrl(const std::string& url)
    {
        if (devAutotestIsEnabled()) {
            devAutotestNote("report: open %s\n", url.c_str());
            return;
        }
        SDL_OpenURL(url.c_str());
    }

#ifdef __EMSCRIPTEN__
    // clang-format off
    EM_JS(void, webCopyTextJs, (const char* text), {
        navigator.clipboard?.writeText(UTF8ToString(text)).catch(error => console.warn('Copy failed', error));
    });
    // clang-format on
#endif

    void copyText(const std::string& text)
    {
        if (devAutotestIsEnabled()) {
            devAutotestNote("report: copy %zu bytes\n", text.size());
            return;
        }
#ifdef __EMSCRIPTEN__
        webCopyTextJs(text.c_str());
#else
        SDL_SetClipboardText(text.c_str());
#endif
    }

    // ---- the window

    class ReportScreen : public MuiScreen {
    public:
        explicit ReportScreen(const Problem& problem)
            : problem(problem)
            , report(buildReport(problem))
        {
        }

        void build(MuiContext& ui) override;
        void back() override { finished = true; }

        bool exportSaves = false;

    private:
        Problem problem;
        Report report;
        Uint32 copiedTime = 0;
        float wrapWidth = -1.0f;
        std::vector<std::u32string> wrapped;
        std::vector<std::u32string> bodyLines;
    };

    void ReportScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        bool environment = report.text.empty();
        ui.dim();
        MuiScreenFrame frame = muiScreenFrameLayout(ui);
        const MuiRect& safe = frame.content;
        float width = std::min(safe.w, ui.dp(560.0f));
        float height = std::min(safe.h, ui.dp(environment ? 200.0f : 380.0f));
        MuiRect card = { safe.centerX() - width / 2.0f, safe.centerY() - height / 2.0f, width, height };
        MuiRect rail = frame.windowed ? muiTabsBeside(ui, card) : frame.tabs;
        if (muiGameScreenTabs(ui, rail, "report", MuiGameScreenTab::None, false) == MuiGameScreenTab::Back || ui.tappedOutside(card)) {
            back();
        }
        ui.panel(card);
        MuiRect inner = card.inset(ui.dp(16.0f));

        MuiRect heading = { inner.x, inner.y, inner.w, ui.dp(30.0f) };
        muiDrawTextAligned(report.title, heading, muiFitTextSize(report.title, heading.w, ui.dp(18.0f), ui.dp(12.0f)), theme.accent, MuiAlign::Start, MuiAlign::Center);
        ui.mark("report.title", heading);

        float size = ui.dp(13.0f);
        float line = muiLineHeight(size);
        if (wrapWidth != inner.w) {
            wrapWidth = inner.w;
            wrapped.clear();
            for (const std::u32string& value : report.lines) {
                auto lines = muiWrapText(value, inner.w, size);
                wrapped.insert(wrapped.end(), lines.begin(), lines.end());
            }
            bodyLines = muiWrapText(muiDecodeUtf8(report.text.c_str()), inner.w - ui.dp(16.0f), ui.dp(11.0f));
        }
        float y = heading.bottom() + ui.dp(4.0f);
        for (const std::u32string& value : wrapped) {
            muiDrawTextAligned(value, { inner.x, y, inner.w, line }, size, theme.text, MuiAlign::Start, MuiAlign::Center);
            y += line;
        }
        ui.mark("report.message", { inner.x, heading.bottom(), inner.w, y - heading.bottom() });
        if (environment) {
            return;
        }

        // The channels, equal, each with what it needs; then the text's
        // copy and the saves' export.
        float buttonHeight = ui.dp(40.0f);
        float captionHeight = muiLineHeight(ui.dp(10.0f));
        float gap = ui.dp(8.0f);
        MuiRect bottomRow = { inner.x, inner.bottom() - buttonHeight, inner.w, buttonHeight };
        MuiRect channelRow = { inner.x, bottomRow.y - gap - captionHeight - buttonHeight, inner.w, buttonHeight };

        std::string form = formUrl(report);
        struct Channel {
            const char* id;
            std::u32string label;
            std::u32string caption;
        };
        std::vector<Channel> channels;
        if (!form.empty()) {
            channels.push_back({ "report.form", text(kTextForm, "Google Form"), text(kTextNoAccount, "no account needed") });
        }
        channels.push_back({ "report.github", U"GitHub", text(kTextNeedsAccount, "needs an account") });
        channels.push_back({ "report.telegram", U"Telegram", text(kTextNeedsAccount, "needs an account") });
        float cellWidth = (channelRow.w - gap * (channels.size() - 1)) / channels.size();
        for (size_t index = 0; index < channels.size(); index++) {
            MuiRect button = { channelRow.x + index * (cellWidth + gap), channelRow.y, cellWidth, buttonHeight };
            muiDrawTextAligned(channels[index].caption, { button.x, button.bottom(), button.w, captionHeight }, ui.dp(10.0f), theme.textDim, MuiAlign::Center, MuiAlign::Center);
            if (ui.button(channels[index].id, button, channels[index].label, MuiButtonStyle::Primary)) {
                if (strcmp(channels[index].id, "report.form") == 0) {
                    openUrl(form);
                } else if (strcmp(channels[index].id, "report.github") == 0) {
                    openUrl(githubUrl(report));
                } else {
                    copyText(report.text);
                    copiedTime = ui.now != 0 ? ui.now : 1;
                    openUrl(telegramUrl(report));
                }
            }
        }

        float half = (bottomRow.w - gap) / 2.0f;
        bool copied = copiedTime != 0 && ui.now - copiedTime < 2000;
        if (ui.button("report.copy", { bottomRow.x, bottomRow.y, half, buttonHeight }, copied ? text(kTextCopied, "Copied") : text(kTextCopy, "Copy the text"))) {
            copyText(report.text);
            copiedTime = ui.now != 0 ? ui.now : 1;
        }
        if (ui.button("report.saves", { bottomRow.x + half + gap, bottomRow.y, half, buttonHeight }, text(kTextExportSaves, "Export saves"))) {
            // Over the window's own loop (the browser's exchange waits in
            // it): the window closes for it and comes back after.
            exportSaves = true;
            finished = true;
        }

        // The report as it goes, scrolling between.
        MuiRect box = { inner.x, y + ui.dp(8.0f), inner.w, std::max(0.0f, channelRow.y - y - ui.dp(16.0f)) };
        muiFillRoundRect(box, ui.dp(4.0f), theme.button);
        float row = muiLineHeight(ui.dp(11.0f));
        MuiRect content = box.inset(ui.dp(8.0f));
        float offset = ui.scroll("report.text", content, bodyLines.size() * row);
        muiPushClip(content);
        float rowY = content.y - offset;
        for (const std::u32string& value : bodyLines) {
            if (rowY + row >= content.y && rowY <= content.bottom()) {
                muiDrawTextAligned(value, { content.x, rowY, content.w, row }, ui.dp(11.0f), theme.textDim, MuiAlign::Start, MuiAlign::Center);
            }
            rowY += row;
        }
        muiPopClip();
    }

    void show(const Problem& problem)
    {
        // Without the mobile UI (the game's own windows): the log only.
        if (!settings.touch.mobile_ui) {
            debugPrint("\nProblem: %s\n", codeLine(problem).c_str());
            return;
        }

        // What a test checks: the window and every line that goes.
        if (devAutotestIsEnabled()) {
            Report report = buildReport(problem);
            devAutotestNote("report: window \"%s\"\n", utf8(report.title).c_str());
            size_t start = 0;
            while (start < report.text.size()) {
                size_t end = report.text.find('\n', start);
                devAutotestNote("report| %s\n", report.text.substr(start, end - start).c_str());
                start = end + 1;
            }
        }

        ReportScreen screen(problem);
        gShownText = buildReport(problem).text;
        while (true) {
            muiRunModal(&screen);
            if (!screen.exportSaves) {
                break;
            }
            // The saves' export of the settings (Android: the app's screen
            // over the game; the browser: its save exchange), then back
            // here.
            screen.exportSaves = false;
            screen.finished = false;
            muiExchangeSaves(false);
        }
        gShownText.clear();
    }

    // ---- the platform's texts (a hang, the browser's engine stopped)

    void appendJson(std::string& out, const char* key, const std::string& value, bool last = false)
    {
        out += "\"";
        out += key;
        out += "\":\"";
        for (unsigned char ch : value) {
            if (ch == '"' || ch == '\\') {
                out.push_back('\\');
                out.push_back(static_cast<char>(ch));
            } else if (ch == '\n') {
                out += "\\n";
            } else if (ch < 0x20) {
                char escaped[8];
                snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
                out += escaped;
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
        out += last ? "\"" : "\",";
    }

    // Under `gMutex`.
    void rebuildPlatformTexts()
    {
        std::string context;
        for (const std::string& line : contextLines()) {
            context += line + "\n";
        }
        std::string verdict = utf8(text(kTextNotYours, "It's the game's error, not yours: send us a report."));
        std::string hang = utf8(text(kTextHang, "The game has stopped responding."));
        std::string crash = utf8(text(kTextCrashNow, "The game stopped because of an error."));
        std::string json = "{";
        appendJson(json, "hangTitle", utf8(text(kTextHangTitle, "The game isn't responding")));
        appendJson(json, "hang", hang + " " + verdict);
        appendJson(json, "hangReport", hang + "\nCode: hang ({state})\n" + buildLine() + "\n" + context);
        appendJson(json, "crashTitle", utf8(text(kTextCrashTitle, "The game closed unexpectedly")));
        appendJson(json, "crash", crash + " " + verdict);
        appendJson(json, "crashReport", crash + "\nCode: crash ({reason})\n" + buildLine() + "\n" + context);
        // The browser's page ended while the game was in front (its next
        // start tells).
        std::string before = utf8(text(kTextCrash, "The game closed unexpectedly the last time."));
        appendJson(json, "crashBefore", before + " " + verdict);
        appendJson(json, "crashBeforeReport", before + "\nCode: crash (page)\n" + buildLine() + "\n" + context);
        appendJson(json, "memoryTitle", utf8(text(kTextSystemClosed, "The system closed the game")));
        appendJson(json, "memory", utf8(text(kTextBrowserMemory, "The browser ran out of memory for the game. Close other tabs and open the game again.")));
        appendJson(json, "form", kFormPrefillUrl);
        appendJson(json, "github", std::string(kPortIssuesUrl) + "/new");
        appendJson(json, "telegram", kTelegramDirectUrl);
        appendJson(json, "formLabel", utf8(text(kTextForm, "Google Form")));
        appendJson(json, "noAccount", utf8(text(kTextNoAccount, "no account needed")));
        appendJson(json, "account", utf8(text(kTextNeedsAccount, "needs an account")));
        appendJson(json, "copy", utf8(text(kTextCopy, "Copy the text")));
        appendJson(json, "copied", utf8(text(kTextCopied, "Copied")));
        appendJson(json, "wait", utf8(text(kTextWait, "Wait")));
        appendJson(json, "close", utf8(text(kTextClose, "Close the game")), true);
        json += "}";
        gPlatformTexts = json;
        gPlatformTextsDirty = false;
        gPlatformTextsTime = SDL_GetTicks();
    }

#ifdef __EMSCRIPTEN__
    // The page reads these without calling the engine (it may be waiting or
    // stopped): frames, loading, the texts' address.
    int gWebState[3];

    void publishWebState()
    {
        gWebState[0] = static_cast<int>(gFrames.load());
        gWebState[1] = gLoading.load();
        gWebState[2] = static_cast<int>(reinterpret_cast<intptr_t>(gPlatformTexts.c_str()));
    }
#endif

    // ---- the run's marker

    const char* runningFile()
    {
        return devAutotestIsEnabled() ? kTestRunningFile : kRunningFile;
    }

    // The browser's page keeps its own (the game's files are its storage):
    // automated tests have none, a test of it its own file.
    bool usesRunningFile()
    {
#ifdef __EMSCRIPTEN__
        return false;
#else
        return !devAutotestIsEnabled();
#endif
    }

    int runningProcess()
    {
#ifdef __ANDROID__
        return static_cast<int>(getpid());
#else
        return 0;
#endif
    }

    void writeRunning()
    {
        if (!usesRunningFile()) {
            return;
        }
        FILE* stream = fopen(runningFile(), "wt");
        if (stream == nullptr) {
            return;
        }
        std::vector<std::string> lines;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            lines = contextLines();
            gContextChanged = false;
        }
        // The process, for the system's record of how it ended.
        fprintf(stream, "# F2 Touch is running (error_report.h), process %d\n", runningProcess());
        for (const std::string& line : lines) {
            fprintf(stream, "%s\n", line.c_str());
        }
        fclose(stream);
        gRunningMarked = true;
        gLastRunningWrite = SDL_GetTicks();
    }

    void removeRunning()
    {
        if (gRunningMarked) {
            remove(runningFile());
            gRunningMarked = false;
        }
    }

    // The last run's lines, or false when it ended cleanly.
    bool readRunning(std::vector<std::string>* lines, int* process)
    {
        FILE* stream = fopen(runningFile(), "rt");
        if (stream == nullptr) {
            return false;
        }
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), stream) != nullptr) {
            const char* processText = buffer[0] == '#' ? strstr(buffer, ", process ") : nullptr;
            if (processText != nullptr) {
                *process = atoi(processText + strlen(", process "));
            }
            std::string line = cleanLine(buffer);
            if (!line.empty() && line[0] != '#' && lines->size() < kEventsShown + kScriptErrorsShown + 2) {
                lines->push_back(line);
            }
        }
        fclose(stream);
        remove(runningFile());
        return true;
    }

    // What the activity's [method] (MainActivity.java) tells of the run of
    // [process]; empty off Android.
    std::string activityText(const char* method, int process)
    {
        std::string result;
#ifdef __ANDROID__
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (env == nullptr || activity == nullptr) {
            return result;
        }
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID methodId = env->GetMethodID(activityClass, method, "(I)Ljava/lang/String;");
        jstring value = methodId != nullptr ? static_cast<jstring>(env->CallObjectMethod(activity, methodId, static_cast<jint>(process))) : nullptr;
        if (value != nullptr) {
            const char* chars = env->GetStringUTFChars(value, nullptr);
            if (chars != nullptr) {
                result = chars;
                env->ReleaseStringUTFChars(value, chars);
            }
            env->DeleteLocalRef(value);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
#endif
        return result;
    }

    // Why the run of [process] ended, as the system tells (Android 11+):
    // "crash", "native-crash", "anr", "low-memory", ...; "none" when it
    // wasn't the game's failure (the phone restarted, the app was stopped or
    // updated); empty when the system doesn't tell.
    std::string exitReason(int process)
    {
        return cleanLine(activityText("exitReason", process));
    }

    // Where the crashed run of [process] fell, as the system tells (Android
    // 12+): the report's lines, none when it kept no record.
    std::vector<std::string> exitStack(int process)
    {
        std::string text;
        if (devAutotestIsEnabled()) {
            FILE* stream = fopen(kTestStackFile, "rt");
            if (stream != nullptr) {
                char buffer[256];
                while (fgets(buffer, sizeof(buffer), stream) != nullptr) {
                    text += buffer;
                }
                fclose(stream);
                remove(kTestStackFile);
            }
        } else {
            text = activityText("exitStack", process);
        }

        std::vector<std::string> lines;
        size_t start = 0;
        while (start < text.size() && lines.size() < kStackLinesShown) {
            size_t end = text.find('\n', start);
            std::string raw = text.substr(start, end != std::string::npos ? end - start : std::string::npos);
            std::string line = cleanLine(raw, raw.compare(0, 7, "Signal:") == 0 || raw.compare(0, 5, "Stack") == 0);
            if (!line.empty()) {
                lines.push_back(line);
            }
            start = end != std::string::npos ? end + 1 : text.size();
        }
        return lines;
    }

    // A problem the app's import screen left: "replace-game <detail>".
    bool readPlatformProblem(Problem* problem)
    {
        FILE* stream = fopen(kProblemFile, "rt");
        if (stream == nullptr) {
            return false;
        }
        char buffer[256] = { 0 };
        bool read = fgets(buffer, sizeof(buffer), stream) != nullptr;
        fclose(stream);
        remove(kProblemFile);
        std::string line = read ? cleanLine(buffer) : std::string();
        std::string code = line.substr(0, line.find(' '));
        if (code != reasonInfo(ErrorReason::ReplaceGame).code) {
            return false;
        }
        problem->reason = ErrorReason::ReplaceGame;
        problem->detail = line.size() > code.size() ? line.substr(code.size() + 1) : std::string();
        return true;
    }

    void raise(const Problem& problem)
    {
        // The first failure is the cause, the next ones its consequences.
        if (gPending.reason == ErrorReason::None) {
            gPending = problem;
        }
    }

} // namespace

void errorReportMapLoad(const char* fileName, const char* error)
{
    Problem problem;
    problem.reason = ErrorReason::MapFile;
    for (const auto& stage : kMapStages) {
        if (error != nullptr && strcmp(stage.error, error) == 0) {
            problem.reason = stage.reason;
        }
    }
    // Before its header is read the map's name is the previous one's.
    if (problem.reason != ErrorReason::MapFile && problem.reason != ErrorReason::MapHeader) {
        problem.file = baseName(fileName);
        problem.place = mapPlace(fileName);
    }

    // Inside a save's load: why that fails.
    if (_isLoadingGame()) {
        if (gPending.reason == ErrorReason::None && gPending.cause == ErrorReason::None) {
            gPending.cause = problem.reason;
            gPending.causeFile = problem.file;
            gPending.causePlace = problem.place;
        }
        return;
    }
    raise(problem);
}

void errorReportSaveLoad(ErrorReason reason, int slot, int part)
{
    Problem problem;
    problem.reason = reason;
    char folder[16];
    snprintf(folder, sizeof(folder), "SLOT%.2d", slot + 1);
    problem.file = folder;
    if (part >= 0 && part < static_cast<int>(sizeof(kSaveParts) / sizeof(kSaveParts[0]))) {
        problem.detail = kSaveParts[part];
    }
    // The map's failure noted on the way.
    problem.cause = gPending.cause;
    problem.causeFile = gPending.causeFile;
    problem.causePlace = gPending.causePlace;
    gPending = Problem();
    raise(problem);
}

void errorReportUpdate(ErrorReason reason, const char* detail)
{
    Problem problem;
    problem.reason = reason;
    problem.detail = cleanLine(detail != nullptr ? detail : "");
    show(problem);
}

bool errorReportShowPending()
{
    if (gPending.reason == ErrorReason::None) {
        // A map's failure seen in a save's load that went on.
        gPending.cause = ErrorReason::None;
        return false;
    }
    Problem problem = gPending;
    gPending = Problem();
    show(problem);
    return true;
}

void errorReportShowManual()
{
    Problem problem;
    problem.reason = ErrorReason::Manual;
    show(problem);
}

void errorReportScriptError(const char* script, const char* format, va_list args)
{
    char buffer[260];
    if (vsnprintf(buffer, sizeof(buffer), format, args) < 0) {
        return;
    }
    std::string line = cleanLine(buffer);
    if (line.empty()) {
        return;
    }
    // Which script, unless the error says it.
    std::string name = baseName(script);
    if (!name.empty() && line.find(name) == std::string::npos) {
        line = cleanLine(name + ": " + line);
    }

    std::lock_guard<std::mutex> lock(gMutex);
    for (auto it = gScriptErrors.begin(); it != gScriptErrors.end(); ++it) {
        if (it->text == line) {
            // The latest last.
            ScriptError error = *it;
            error.count++;
            gScriptErrors.erase(it);
            gScriptErrors.push_back(error);
            gContextChanged = gPlatformTextsDirty = true;
            return;
        }
    }
    if (gScriptErrors.size() >= kScriptErrorsKept) {
        gScriptErrors.erase(gScriptErrors.begin());
        gScriptErrorsDropped++;
    }
    gScriptErrors.push_back({ line });
    gContextChanged = gPlatformTextsDirty = true;
}

void errorReportEvent(const char* text)
{
    std::string line = cleanLine(text != nullptr ? text : "");
    if (line.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(gMutex);
    // A run of one kind ("map tap: ...", "inventory: ...") is its latest
    // event: the taps don't push the rest out.
    size_t kind = line.find(": ");
    if (kind != std::string::npos && !gEvents.empty() && gEvents.back().compare(0, kind + 2, line, 0, kind + 2) == 0) {
        gEvents.pop_back();
    }
    if (gEvents.size() >= kEventsKept) {
        gEvents.erase(gEvents.begin());
    }
    gEvents.push_back(line);
    gContextChanged = gPlatformTextsDirty = true;
}

void errorReportFlush()
{
    if (gStarted && gRunningMarked) {
        writeRunning();
    }
}

void errorReportStart()
{
    gStarted = true;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        rebuildPlatformTexts();
    }
#ifdef __EMSCRIPTEN__
    publishWebState();
    webPublishErrorReportJs(gWebState);
#endif

    Problem problem;
    std::vector<std::string> previous;
    int process = 0;
    std::string reason;
    if ((usesRunningFile() || devAutotestIsEnabled()) && readRunning(&previous, &process) && (reason = exitReason(process)) != "none") {
        problem.reason = reason == "low-memory" ? ErrorReason::LowMemory : ErrorReason::Crash;
        problem.detail = reason;
        problem.previous = previous;
        if (problem.reason == ErrorReason::Crash) {
            problem.stack = exitStack(process);
        }
        raise(problem);
    } else if (readPlatformProblem(&problem)) {
        raise(problem);
    }
    writeRunning();
}

void errorReportFocus(bool active)
{
    if (!gStarted) {
        return;
    }
    if (active) {
        writeRunning();
    } else {
        removeRunning();
    }
}

void errorReportExit()
{
    removeRunning();
}

void errorReportFrame()
{
    gFrames.fetch_add(1);
    gLoading.store(mapIsLoading() || _isLoadingGame() ? 1 : 0);

    if (!gStarted) {
        return;
    }

    Uint32 now = SDL_GetTicks();
    bool changed;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (gPlatformTextsDirty && now - gPlatformTextsTime >= kPlatformTextsMs) {
            rebuildPlatformTexts();
        }
        changed = gContextChanged;
    }
#ifdef __EMSCRIPTEN__
    publishWebState();
#endif
    if (changed && gRunningMarked && now - gLastRunningWrite >= kRunningWriteMs) {
        writeRunning();
    }
}

std::string errorReportTestShown()
{
    return gShownText;
}

std::string errorReportTestShownFitted(int less)
{
    return fittedText(gShownText, urlEncode(gShownText).size() - static_cast<size_t>(less));
}

void errorReportTestRaise(ErrorReason reason, const char* detail)
{
    Problem problem;
    problem.reason = reason;
    problem.detail = cleanLine(detail != nullptr ? detail : "");
    raise(problem);
}

std::string errorReportTestText(ErrorReason reason)
{
    Problem problem;
    problem.reason = reason;
    if (reason == ErrorReason::SavePart) {
        problem.file = "SLOT01";
        problem.detail = kSaveParts[3];
    }
    return buildReport(problem).text;
}

#ifdef __ANDROID__
// The activity's hang watchdog (MainActivity.java), on its own thread.
extern "C" JNIEXPORT jlong JNICALL Java_com_alexbatalov_fallout2ce_MainActivity_nativeWatchState(JNIEnv* env, jclass clazz)
{
    return (static_cast<jlong>(gFrames.load()) << 1) | gLoading.load();
}

extern "C" JNIEXPORT jstring JNICALL Java_com_alexbatalov_fallout2ce_MainActivity_nativeReportTexts(JNIEnv* env, jclass clazz)
{
    std::lock_guard<std::mutex> lock(gMutex);
    return env->NewStringUTF(gPlatformTexts.c_str());
}

// The watchdog closes the game: not a crash for the next start.
extern "C" JNIEXPORT void JNICALL Java_com_alexbatalov_fallout2ce_MainActivity_nativeClosing(JNIEnv* env, jclass clazz)
{
    remove(kRunningFile);
}
#endif

} // namespace fallout
