#include "game_card.h"

#include <stdio.h>
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

namespace fallout {

namespace {

    // The game's 640x480 menu picture has its button plates at the left
    // (mui_main_menu.cc): the card shows the part right of them.
    constexpr int kVanillaArtLeft = 215;

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

    // The menu's picture as the menu shows it (the high resolution one,
    // else the game's own right of its button plates), once a run: a mod
    // added or replaced may bring another.
    void writePicture(const std::string& path)
    {
        if (gPictureWritten) {
            return;
        }
        gPictureWritten = true;

        std::vector<unsigned char> rgba;
        int width;
        int height;
        int left = 0;
        if (!muiPictureRgba(InterfaceFrmId("HR_MAINMENU.FRM"), rgba, &width, &height)) {
            if (!muiPictureRgba(FrmId(InterfaceFrameId::MainMenuBackgroundImage), rgba, &width, &height)) {
                return;
            }
            left = width > kVanillaArtLeft ? kVanillaArtLeft : 0;
        }

        int cropped = width - left;
        std::vector<unsigned char> pixels(static_cast<size_t>(cropped) * height * 4);
        for (int y = 0; y < height; y++) {
            memcpy(&pixels[static_cast<size_t>(y) * cropped * 4], &rgba[(static_cast<size_t>(y) * width + left) * 4], static_cast<size_t>(cropped) * 4);
        }

        std::vector<unsigned char> encoded;
        if (lodepng::encode(encoded, pixels, static_cast<unsigned>(cropped), static_cast<unsigned>(height)) != 0) {
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
        + "place=" + cardLine(encodeUtf8(place)) + "\n";
    if (!saveStorage::writeFile(cardPath("card.txt"), text)) {
        debugPrint("gameCardWrite: card not written\n");
    }
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
#else
    debugPrint("gameCardOpenGames: no list of games here\n");
#endif
}

} // namespace fallout
