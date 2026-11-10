#include "game_card.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include <SDL.h>
#include <lodepng.h>

#include "art.h"
#include "debug.h"
#include "dev_autotest.h"
#include "mui_draw.h"
#include "save_storage.h"
#include "version.h"

#ifdef __ANDROID__
#include <jni.h>

#include "platform/android/paths.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include "game.h"
#include "platform/web/storage.h"
#include "platform/web/window.h"
#endif

namespace fallout {

namespace {

    // The card's picture whole: the game's own 640x480 menu picture with its
    // button plates too (the main menu shows only the part right of them,
    // mui_main_menu.cc; the list holds a picture at its right edge). Older
    // cards had that part only: card.txt says which one there is.
    constexpr const char* kWholePicture = "picture=whole";

    // The mark of a start under way (MainActivity writes it, the app's
    // folder): gone once the game gets to its menu or into play.
    constexpr const char* kStartingFile = "starting.txt";

    bool gPictureWritten = false;

    // [name] in the game's private folder, empty where there's no card.
    std::string cardPath(const char* name)
    {
#ifdef __ANDROID__
        const std::string& folder = androidGameDataFolder();
        if (!folder.empty()) {
            return folder + "/" + name;
        }
#endif
#ifdef __EMSCRIPTEN__
        // The game's port folder in the user's storage (its launcher reads it).
        return webPortDataPath(name);
#endif
        if (devAutotestIsEnabled()) {
            return std::string("ce-") + name;
        }
        return std::string();
    }

    std::string encodeUtf8(const std::u32string& text)
    {
        std::string result;
        for (char32_t ch : text) {
            if (ch < 0x80) {
                result += static_cast<char>(ch);
            } else if (ch < 0x800) {
                result += static_cast<char>(0xC0 | (ch >> 6));
                result += static_cast<char>(0x80 | (ch & 0x3F));
            } else if (ch < 0x10000) {
                result += static_cast<char>(0xE0 | (ch >> 12));
                result += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (ch & 0x3F));
            } else {
                result += static_cast<char>(0xF0 | (ch >> 18));
                result += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
                result += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (ch & 0x3F));
            }
        }
        return result;
    }

    // One line of the card: no line breaks in it.
    std::string cardLine(std::string text)
    {
        for (char& ch : text) {
            if (ch == '\n' || ch == '\r') {
                ch = ' ';
            }
        }
        return text;
    }

#ifdef __EMSCRIPTEN__
    // The card at [path] (card.txt) is of a whole picture.
    bool cardOfWholePicture(const std::string& path)
    {
        FILE* stream = fopen(path.c_str(), "rb");
        if (stream == nullptr) {
            return false;
        }
        char line[256];
        bool whole = false;
        while (!whole && fgets(line, sizeof(line), stream) != nullptr) {
            whole = strncmp(line, kWholePicture, strlen(kWholePicture)) == 0;
        }
        fclose(stream);
        return whole;
    }
#endif

    // The menu's picture whole (the high resolution one, else the game's
    // own with its button plates), once a run: a mod added or replaced may
    // bring another.
    void writePicture(const std::string& path)
    {
        if (gPictureWritten) {
            return;
        }
        gPictureWritten = true;
#ifdef __EMSCRIPTEN__
        // Once: every write is a new version in the browser's storage (an
        // older card's part of the picture written again).
        saveStorage::Info info;
        if (saveStorage::inspect(path, info) && info.exists && cardOfWholePicture(cardPath("card.txt"))) {
            return;
        }
#endif

        std::vector<unsigned char> pixels;
        int width;
        int height;
        if (!muiPictureRgba(InterfaceFrmId("HR_MAINMENU.FRM"), pixels, &width, &height)
            && !muiPictureRgba(FrmId(InterfaceFrameId::MainMenuBackgroundImage), pixels, &width, &height)) {
            return;
        }

        std::vector<unsigned char> encoded;
        if (lodepng::encode(encoded, pixels, static_cast<unsigned>(width), static_cast<unsigned>(height)) != 0) {
            return;
        }
        std::string temporary = path + ".tmp";
        FILE* stream = fopen(temporary.c_str(), "wb");
        if (stream == nullptr) {
            return;
        }
        bool written = fwrite(encoded.data(), 1, encoded.size(), stream) == encoded.size();
        written = fclose(stream) == 0 && written;
        if (!written || rename(temporary.c_str(), path.c_str()) != 0) {
            remove(temporary.c_str());
            debugPrint("gameCardWrite: picture not written\n");
        }
    }

#ifdef __EMSCRIPTEN__
    // clang-format off
    EM_JS(char*, webGameNameJs, (), {
        const name = Module['webGameName'];
        return typeof name === 'string' && name ? stringToNewUTF8(name) : 0;
    });
    // clang-format on
#endif

#ifdef __ANDROID__
    // MainActivity's [method] () -> String, empty when there's none.
    std::string activityString(const char* method)
    {
        std::string result;
        JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (env == nullptr || activity == nullptr) {
            return result;
        }
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID methodId = env->GetMethodID(activityClass, method, "()Ljava/lang/String;");
        jstring value = methodId != nullptr ? static_cast<jstring>(env->CallObjectMethod(activity, methodId)) : nullptr;
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
        return result;
    }
#endif

} // namespace

void gameCardWrite(const std::u32string& place)
{
    std::string picture = cardPath("card.png");
    if (picture.empty()) {
        return;
    }
    writePicture(picture);

    // Longer than the game's VERSION_MAX: a mod's own version line whole.
    char version[128];
    versionGetVersion(version, sizeof(version));
    std::string text = "version=" + cardLine(version) + "\n"
        + "place=" + cardLine(encodeUtf8(place)) + "\n"
        + kWholePicture + "\n";
    if (!saveStorage::writeFile(cardPath("card.txt"), text)) {
        debugPrint("gameCardWrite: card not written\n");
    }
#ifdef __EMSCRIPTEN__
    // Kept in the browser's storage: the launcher reads it.
    webCommitUserFiles();
#endif
}

void gameCardStarted()
{
#ifdef __ANDROID__
    remove(androidAppFile(kStartingFile).c_str());
#endif
}

const std::string& gameCardName()
{
    static std::string name;
    static bool known = false;
    static Uint32 askedAt = 0;
#ifdef __ANDROID__
    // Asked again now and then: the list (over the game) may rename it.
    if (known && SDL_GetTicks() - askedAt >= 1000) {
        known = false;
    }
#endif
    if (!known) {
        known = true;
        askedAt = SDL_GetTicks();
#ifdef __ANDROID__
        name = activityString("gameName");
#elif defined(__EMSCRIPTEN__)
        char* value = webGameNameJs();
        if (value != nullptr) {
            name = value;
            free(value);
        }
#else
        // The menu's button in automated tests (`mainmenu`).
        if (devAutotestIsEnabled()) {
            name = "Fallout 2";
        }
#endif
    }
    return name;
}

void gameCardOpenGames()
{
#ifdef __ANDROID__
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(activityClass, "openGames", "()V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
#elif defined(__EMSCRIPTEN__)
    // The page's launcher, after the game ends as on Exit.
    webOpenGameFiles("games");
    _game_user_wants_to_quit = GAME_QUIT_REQUEST_EXIT;
#else
    debugPrint("gameCardOpenGames: no list of games here\n");
#endif
}

} // namespace fallout
