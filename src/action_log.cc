#include "action_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

// Where the dude's look changed from (`actionLogDudeLook`): the stack, where
// the unwinder and dladdr are (not Windows, not the browser).
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define ACTION_LOG_BACKTRACE 1
#include <dlfcn.h>
#include <unwind.h>
#endif

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "art.h"
#include "error_report.h"
#include "game.h"
#include "interface.h"
#include "inventory.h"
#include "loadsave.h"
#include "map.h"
#include "mui_draw.h"
#include "object.h"
#include "party_member.h"
#include "perf_monitor.h"
#include "settings.h"
#include "tile.h"
#include "world_view.h"

namespace fallout {

namespace {

    constexpr const char* kFileName = "actions.log";
    constexpr const char* kOldFileName = "actions.log.old";
    constexpr long kMaxFileSize = 8 * 1024 * 1024;

    // Waiting lines are written this often (ms), or once there are this many
    // bytes.
    constexpr Uint32 kFlushIntervalMs = 1000;
    constexpr size_t kFlushSize = 64 * 1024;

    // The party is looked at this often (ms); the dude every frame.
    constexpr Uint32 kPartyIntervalMs = 250;
    // The camera too.
    constexpr Uint32 kViewIntervalMs = 300;

    // Callers noted with a change of the dude's look.
    constexpr int kBacktraceFrames = 12;

    // Screens holding the dude's equipment aside: his look follows it when
    // they close.
    constexpr int kEquipmentScreens = GameMode::kInventory | GameMode::kLoot | GameMode::kBarter | GameMode::kUseOn;

    FILE* gStream = nullptr;
    bool gStarted = false;
    std::string gPending;
    Uint32 gLastFlushTime = 0;

    // What a critter wears and holds, and how it looks.
    struct Look {
        Object* critter = nullptr;
        int pid = -1;
        int armor = -1;
        int right = -1;
        int left = -1;
        int art = -1;
        int weapon = -1;
    };

    Look gDudeLook;
    int gDudeHand = -1;
    bool gDudeMismatch = false;
    std::vector<Look> gPartyLooks;
    Uint32 gLastPartyTime = 0;

    int gLastModes = -1;
    std::string gLastMap;
    int gLastElevation = -1;

    Uint32 gLastViewTime = 0;
    int gLastViewTile = -1;
    float gLastViewZoom = 0.0f;

    // The inventory's equipment globals set outside its screens.
    bool gStaleEquipmentNoted = false;

    bool enabled()
    {
        return settings.debug.action_log;
    }

    void appendUtf8(std::string& out, char32_t ch)
    {
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }

    // Game texts are in the game's encoding (cp1251 with Russian).
    std::string gameTextToUtf8(const char* text)
    {
        std::string result;
        for (char32_t ch : muiDecodeGameText(text)) {
            appendUtf8(result, ch);
        }
        return result;
    }

    void writePending()
    {
        if (gPending.empty()) {
            return;
        }

        if (gStream != nullptr && ftell(gStream) > kMaxFileSize) {
            fclose(gStream);
            gStream = nullptr;
            remove(kOldFileName);
            rename(kFileName, kOldFileName);
        }

        if (gStream == nullptr) {
            gStream = fopen(kFileName, "a");
        }

        if (gStream != nullptr) {
            fwrite(gPending.data(), 1, gPending.size(), gStream);
            fflush(gStream);
        }
        gPending.clear();
    }

    void start()
    {
        gStarted = true;

        // The previous run stays as the old file.
        remove(kOldFileName);
        rename(kFileName, kOldFileName);

        char date[32];
        time_t wallTime = time(nullptr);
        strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&wallTime));

        gPending += "# Game journal (see action_log.h).\n=== ";
        gPending += date;
        gPending += "  the game started\n";
    }

    void line(const std::string& text)
    {
        if (!gStarted) {
            start();
        }

        auto now = std::chrono::system_clock::now();
        time_t wallTime = std::chrono::system_clock::to_time_t(now);
        int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
        char stamp[32];
        size_t length = strftime(stamp, sizeof(stamp), "%H:%M:%S", localtime(&wallTime));
        snprintf(stamp + length, sizeof(stamp) - length, ".%03d ", ms);

        gPending += stamp;
        gPending += text;
        gPending += '\n';

        if (gPending.size() >= kFlushSize) {
            writePending();
        }
    }

    int pidOf(Object* object)
    {
        return object != nullptr ? object->pid : -1;
    }

    Look lookOf(Object* critter)
    {
        Look look;
        look.critter = critter;
        look.pid = critter->pid;
        look.armor = pidOf(critterGetArmor(critter));
        look.right = pidOf(critterGetItem2(critter));
        look.left = pidOf(critterGetItem1(critter));
        const FrmId frmId(critter);
        look.art = static_cast<int>(frmId.frameId<CritterFrameId>());
        look.weapon = static_cast<int>(frmId.weaponAnimation());
        return look;
    }

    std::string artName(Object* critter)
    {
        char name[16] = { 0 };
        artCopyFileName(FrmId(critter), name);
        return name;
    }

    // "none", "pid 74" or "pid 74 (Leather Jacket)" with the [item] at hand.
    std::string itemName(int pid, Object* item)
    {
        if (pid == -1) {
            return "none";
        }
        if (item == nullptr) {
            return "pid " + std::to_string(pid);
        }
        // Without `actionLogObject`'s leading space.
        return actionLogObject(item) + 1;
    }

    // Lines for what changed between [before] and [after] of [who].
    void logChanges(const char* who, const Look& before, const Look& after)
    {
        char text[512];
        if (before.armor != after.armor) {
            snprintf(text, sizeof(text), "%s armor: %s -> %s", who,
                itemName(before.armor, nullptr).c_str(), itemName(after.armor, critterGetArmor(after.critter)).c_str());
            line(text);
        }
        if (before.right != after.right || before.left != after.left) {
            snprintf(text, sizeof(text), "%s hands: right %d left %d -> right %s, left %s", who,
                before.right, before.left,
                itemName(after.right, critterGetItem2(after.critter)).c_str(),
                itemName(after.left, critterGetItem1(after.critter)).c_str());
            line(text);
        }
        if ((before.art != after.art || before.weapon != after.weapon) && after.critter != gDude) {
            snprintf(text, sizeof(text), "%s look: art %d weapon %d -> art %d (%s) weapon %d", who,
                before.art, before.weapon, after.art, artName(after.critter).c_str(), after.weapon);
            line(text);
        }
    }

    // The look's art isn't what the dude wears (outside screens holding his
    // equipment).
    void checkDudeMismatch(int modes)
    {
        bool mismatch = false;
        int expectedArt = gDudeLook.art;
        if ((modes & kEquipmentScreens) == 0) {
            const FrmId frmId(gDude);
            const FrmId expected = inventoryComputeCritterFrmId(gDude,
                gDude,
                critterGetItem2(gDude),
                critterGetItem1(gDude),
                critterGetArmor(gDude),
                interfaceGetCurrentHand(),
                frmId.animationType(),
                frmId.rotation());
            expectedArt = static_cast<int>(expected.frameId<CritterFrameId>());
            mismatch = expectedArt != gDudeLook.art;
        }

        if (mismatch != gDudeMismatch) {
            gDudeMismatch = mismatch;
            char text[160];
            if (mismatch) {
                snprintf(text, sizeof(text), "MISMATCH: the dude looks art %d, wears art %d", gDudeLook.art, expectedArt);
            } else {
                snprintf(text, sizeof(text), "the dude's look is what he wears again");
            }
            line(text);
        }
    }

    void watchDude(int modes, bool modesChanged)
    {
        // These screens hold the equipment aside (an open bag hides it from
        // `critterGetArmor` too): what changed shows once they close.
        if (gDude == nullptr || (modes & kEquipmentScreens) != 0) {
            return;
        }

        Look look = lookOf(gDude);
        int hand = interfaceGetCurrentHand();
        bool changed = look.armor != gDudeLook.armor
            || look.right != gDudeLook.right
            || look.left != gDudeLook.left
            || look.art != gDudeLook.art
            || look.weapon != gDudeLook.weapon
            || hand != gDudeHand;

        if (changed && gDudeLook.critter != nullptr) {
            logChanges("dude", gDudeLook, look);
            if (hand != gDudeHand) {
                char text[64];
                snprintf(text, sizeof(text), "dude active hand: %s", hand == HAND_LEFT ? "left" : "right");
                line(text);
            }
        }

        gDudeLook = look;
        gDudeHand = hand;

        if (changed || modesChanged) {
            checkDudeMismatch(modes);
        }
    }

    void watchParty(Uint32 now)
    {
        if (gDude == nullptr || now - gLastPartyTime < kPartyIntervalMs) {
            return;
        }
        gLastPartyTime = now;

        std::vector<Look> looks;
        for (Object* member : get_all_party_members_objects(false)) {
            if (member != gDude && FrmId(member).objectType() == OBJ_TYPE_CRITTER) {
                looks.push_back(lookOf(member));
            }
        }

        for (const Look& look : looks) {
            const Look* before = nullptr;
            for (const Look& previous : gPartyLooks) {
                if (previous.critter == look.critter && previous.pid == look.pid) {
                    before = &previous;
                }
            }

            std::string who = "party" + std::string(actionLogObject(look.critter));
            if (before == nullptr) {
                char text[256];
                snprintf(text, sizeof(text), "%s: armor %d, right %d, left %d, look art %d (%s)", who.c_str(),
                    look.armor, look.right, look.left, look.art, artName(look.critter).c_str());
                line(text);
            } else {
                logChanges(who.c_str(), *before, look);
            }
        }

        gPartyLooks = std::move(looks);
    }

#if ACTION_LOG_BACKTRACE
    struct Backtrace {
        void* frames[kBacktraceFrames];
        int count;
    };

    _Unwind_Reason_Code backtraceFrame(struct _Unwind_Context* context, void* data)
    {
        Backtrace* backtrace = static_cast<Backtrace*>(data);
        uintptr_t pc = _Unwind_GetIP(context);
        if (pc != 0) {
            backtrace->frames[backtrace->count++] = reinterpret_cast<void*>(pc);
        }
        return backtrace->count < kBacktraceFrames ? _URC_NO_REASON : _URC_END_OF_STACK;
    }

    // Return addresses as offsets in their libraries ("so+0x1a2b3c"; the
    // game's own library is "so", symbolized with the unstripped build).
    std::string backtraceText()
    {
        Backtrace backtrace;
        backtrace.count = 0;
        _Unwind_Backtrace(backtraceFrame, &backtrace);

        Dl_info self;
        bool selfKnown = dladdr(reinterpret_cast<void*>(&actionLogFrame), &self) != 0;

        std::string text;
        // The first two are this function and the hook.
        for (int index = 2; index < backtrace.count; index++) {
            Dl_info info;
            char entry[96];
            if (dladdr(backtrace.frames[index], &info) != 0 && info.dli_fbase != nullptr) {
                const char* name = info.dli_fname != nullptr ? info.dli_fname : "?";
                const char* slash = strrchr(name, '/');
                if (selfKnown && info.dli_fbase == self.dli_fbase) {
                    name = "so";
                } else if (slash != nullptr) {
                    name = slash + 1;
                }
                snprintf(entry, sizeof(entry), " %s+0x%lx", name,
                    static_cast<unsigned long>(reinterpret_cast<uintptr_t>(backtrace.frames[index]) - reinterpret_cast<uintptr_t>(info.dli_fbase)));
            } else {
                snprintf(entry, sizeof(entry), " 0x%lx", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(backtrace.frames[index])));
            }
            text += entry;
        }
        return text;
    }
#else
    std::string backtraceText()
    {
        return " (no stack here)";
    }
#endif

    void watchView(Uint32 now)
    {
        if (now - gLastViewTime < kViewIntervalMs) {
            return;
        }
        gLastViewTime = now;

        int x;
        int y;
        worldViewGetViewCenter(&x, &y);
        int tile = tileFromScreenXY(x, y);
        float zoom = worldViewGetZoom();
        if (tile != gLastViewTile || zoom != gLastViewZoom) {
            gLastViewTile = tile;
            gLastViewZoom = zoom;
            char text[96];
            snprintf(text, sizeof(text), "view: center tile %d, zoom %.2f", tile, zoom);
            line(text);
        }
    }

    // The inventory screens hold the equipment in globals that
    // `critterGetArmor` and others answer with: set outside them, every
    // look computed from them is wrong.
    void watchStaleEquipment(int modes)
    {
        if ((modes & kEquipmentScreens) != 0) {
            return;
        }

        InventoryHeldEquipment held;
        inventoryGetHeldEquipment(&held);
        bool stale = held.armor != nullptr || held.rightHand != nullptr || held.leftHand != nullptr
            || (held.critter != nullptr && held.critter != gDude);
        if (stale != gStaleEquipmentNoted) {
            gStaleEquipmentNoted = stale;
            if (stale) {
                char text[512];
                snprintf(text, sizeof(text), "STALE inventory globals outside its screens: critter%s, armor%s, right%s, left%s",
                    actionLogObject(held.critter),
                    actionLogObject(held.armor),
                    actionLogObject(held.rightHand),
                    actionLogObject(held.leftHand));
                line(text);
            } else {
                line("inventory globals clear again");
            }
        }
    }

    // The map is one of the last events of a problem report too, with the
    // journal off (error_report.h).
    void watchMap()
    {
        std::string map = gMapHeader.name;
        if (map != gLastMap || gElevation != gLastElevation) {
            gLastMap = map;
            gLastElevation = gElevation;
            if (!map.empty()) {
                char text[96];
                snprintf(text, sizeof(text), "map %s, elevation %d", map.c_str(), gElevation);
                errorReportEvent(text);
                if (enabled()) {
                    line(text);
                }
            }
        }
    }

} // namespace

// Also one of the last events of a problem report, with the journal off
// (error_report.h): the lines of fingers, the camera and code addresses
// aren't these.
void actionLog(const char* format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    errorReportEvent(text);
    if (enabled()) {
        line(text);
    }
}

const char* actionLogObject(Object* object)
{
    // Several in one line.
    static std::string buffers[4];
    static int next = 0;

    std::string& buffer = buffers[next];
    next = (next + 1) % 4;

    buffer.clear();
    if (object == gDude && object != nullptr) {
        buffer = " dude";
    } else if (object != nullptr) {
        char pid[32];
        snprintf(pid, sizeof(pid), " pid %d", object->pid);
        buffer = pid;
        char* name = objectGetName(object);
        if (name != nullptr && name[0] != '\0') {
            buffer += " (" + gameTextToUtf8(name) + ")";
        }
    }
    return buffer.c_str();
}

void actionLogState(const char* title)
{
    // The title only for a problem report.
    errorReportEvent(title);
    if (!enabled() || gDude == nullptr) {
        return;
    }

    char text[512];
    Look look = lookOf(gDude);
    snprintf(text, sizeof(text), "%s: dude armor %s, right %s, left %s, active hand %s, look art %d (%s) weapon %d, fid %08x, tile %d",
        title,
        itemName(look.armor, critterGetArmor(gDude)).c_str(),
        itemName(look.right, critterGetItem2(gDude)).c_str(),
        itemName(look.left, critterGetItem1(gDude)).c_str(),
        interfaceGetCurrentHand() == HAND_LEFT ? "left" : "right",
        look.art,
        artName(gDude).c_str(),
        look.weapon,
        gDude->fid,
        gDude->tile);
    line(text);

    // The changes from here on.
    gDudeLook = look;
    gDudeHand = interfaceGetCurrentHand();
    gDudeMismatch = false;
    checkDudeMismatch(GameMode::getCurrentGameMode());
    gPartyLooks.clear();
    gLastPartyTime = 0;
    watchParty(SDL_GetTicks());
}

void actionLogFrame()
{
    // Between the game's start and its end only, not while a game or a map
    // loads (the objects are being replaced).
    bool playing = gDude != nullptr && gDude->tile != -1 && !_isLoadingGame() && !mapIsLoading();
    if (playing) {
        watchMap();
    }

    if (!enabled()) {
        return;
    }

    Uint32 now = SDL_GetTicks();

    int modes = GameMode::getCurrentGameMode();
    bool modesChanged = modes != gLastModes;
    if (modesChanged) {
        gLastModes = modes;
        line("screens: " + perfMonitorGameModeNames(modes));
    }

    if (playing) {
        watchDude(modes, modesChanged);
        watchParty(now);
        watchStaleEquipment(modes);
        if ((modes & GameMode::kWorldmap) == 0) {
            watchView(now);
        }
    }

    if (now - gLastFlushTime >= kFlushIntervalMs) {
        gLastFlushTime = now;
        writePending();
    }
}

void actionLogDudeFid(int oldFid, int newFid)
{
    if (!enabled()) {
        return;
    }

    const FrmId oldFrmId(oldFid);
    const FrmId newFrmId(newFid);
    char text[1024];
    snprintf(text, sizeof(text), "dude look: art %d -> %d, fid %08x -> %08x, screens %s, from%s",
        static_cast<int>(oldFrmId.frameId<CritterFrameId>()),
        static_cast<int>(newFrmId.frameId<CritterFrameId>()),
        oldFid,
        newFid,
        perfMonitorGameModeNames(GameMode::getCurrentGameMode()).c_str(),
        backtraceText().c_str());
    line(text);
}

void actionLogFinger(const SDL_TouchFingerEvent& event, Uint32 type, const char* route)
{
    if (!enabled() || type == SDL_FINGERMOTION) {
        return;
    }

    char text[160];
    snprintf(text, sizeof(text), "finger %lld %s at %.4f %.4f (%s)",
        static_cast<long long>(event.fingerId),
        type == SDL_FINGERDOWN ? "down" : "up",
        event.x,
        event.y,
        route);
    line(text);
}

void actionLogFlush()
{
    if (!enabled()) {
        return;
    }

    writePending();
}

} // namespace fallout
