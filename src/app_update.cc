#include "app_update.h"

#include "debug.h"
#include "dev_autotest.h"
#include "game.h"
#include "settings.h"

#ifdef __ANDROID__
#include <SDL.h>
#include <jni.h>
#endif

namespace fallout {

namespace {

    AppUpdateState gTestState = AppUpdateState::Idle;
    AppUpdateError gTestError = AppUpdateError::Download;
    std::string gTestTag;
    bool gTestReady = false;
    bool gTestActive = false;
    int gTestInstalls = 0;

#ifdef __ANDROID__
    // Every SDL_AndroidGetActivity result and JNI result is a local ref.
    // No Java exception may escape onto SDL's native game thread.
    class UpdateJni {
    public:
        UpdateJni()
        {
            env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
            if (env == nullptr) return;
            activity = static_cast<jobject>(SDL_AndroidGetActivity());
            if (activity == nullptr) return;
            type = env->GetObjectClass(activity);
            if (type == nullptr) return;

            static bool configured = false;
            static std::string repository;
            if (!configured || repository != settings.debug.update_repo) {
                jmethodID configure = method("updateConfigure", "(Ljava/lang/String;)V");
                if (configure != nullptr) {
                    jstring value = env->NewStringUTF(settings.debug.update_repo.c_str());
                    env->CallStaticVoidMethod(type, configure, value);
                    env->DeleteLocalRef(value);
                    if (!failed()) {
                        repository = settings.debug.update_repo;
                        configured = true;
                    }
                }
            }
        }

        ~UpdateJni()
        {
            if (env == nullptr) return;
            failed();
            if (type != nullptr) env->DeleteLocalRef(type);
            if (activity != nullptr) env->DeleteLocalRef(activity);
        }

        bool failed()
        {
            if (!env->ExceptionCheck()) return false;
            env->ExceptionClear();
            debugPrint("Update: Java bridge failed\n");
            return true;
        }

        jmethodID method(const char* name, const char* signature)
        {
            if (type == nullptr) return nullptr;
            jmethodID result = env->GetStaticMethodID(type, name, signature);
            return failed() ? nullptr : result;
        }

        void action(const char* name)
        {
            jmethodID call = method(name, "()V");
            if (call != nullptr) env->CallStaticVoidMethod(type, call);
        }

        void flag(const char* name, bool value)
        {
            jmethodID call = method(name, "(Z)V");
            if (call != nullptr) env->CallStaticVoidMethod(type, call, static_cast<jboolean>(value));
        }

        int integer(const char* name, int fallback)
        {
            jmethodID call = method(name, "()I");
            if (call == nullptr) return fallback;
            int result = env->CallStaticIntMethod(type, call);
            return failed() ? fallback : result;
        }

        bool boolean(const char* name)
        {
            jmethodID call = method(name, "()Z");
            if (call == nullptr) return false;
            bool result = env->CallStaticBooleanMethod(type, call);
            return !failed() && result;
        }

        std::string string(int field)
        {
            jmethodID call = method("updateString", "(I)Ljava/lang/String;");
            if (call == nullptr) return {};
            jstring value = static_cast<jstring>(env->CallStaticObjectMethod(type, call, field));
            if (failed() || value == nullptr) return {};
            // GetStringUTFChars is modified UTF-8 (supplementary characters
            // are surrogate pairs). Read UTF-16 and encode real UTF-8.
            const jchar* chars = env->GetStringChars(value, nullptr);
            std::string result;
            if (chars != nullptr) {
                jsize length = env->GetStringLength(value);
                for (jsize index = 0; index < length; index++) {
                    unsigned int cp = chars[index];
                    if (cp >= 0xD800 && cp <= 0xDBFF && index + 1 < length && chars[index + 1] >= 0xDC00 && chars[index + 1] <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (chars[++index] - 0xDC00);
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    if (cp < 0x80)
                        result += static_cast<char>(cp);
                    else if (cp < 0x800) {
                        result += static_cast<char>(0xC0 | (cp >> 6));
                        result += static_cast<char>(0x80 | (cp & 63));
                    } else if (cp < 0x10000) {
                        result += static_cast<char>(0xE0 | (cp >> 12));
                        result += static_cast<char>(0x80 | ((cp >> 6) & 63));
                        result += static_cast<char>(0x80 | (cp & 63));
                    } else {
                        result += static_cast<char>(0xF0 | (cp >> 18));
                        result += static_cast<char>(0x80 | ((cp >> 12) & 63));
                        result += static_cast<char>(0x80 | ((cp >> 6) & 63));
                        result += static_cast<char>(0x80 | (cp & 63));
                    }
                }
                env->ReleaseStringChars(value, chars);
            }
            env->DeleteLocalRef(value);
            return result;
        }

        JNIEnv* env = nullptr;
        jobject activity = nullptr;
        jclass type = nullptr;
    };
#endif

} // namespace

AppUpdateState appUpdateState()
{
    if (devAutotestIsEnabled()) return gTestState;
#ifdef __ANDROID__
    return static_cast<AppUpdateState>(UpdateJni().integer("updateState", -1));
#else
    return AppUpdateState::Unsupported;
#endif
}

AppUpdateError appUpdateError()
{
    if (devAutotestIsEnabled()) return gTestError;
#ifdef __ANDROID__
    return static_cast<AppUpdateError>(UpdateJni().integer("updateError", 0));
#else
    return AppUpdateError::None;
#endif
}

std::string appUpdateTag()
{
    if (devAutotestIsEnabled()) return gTestTag;
#ifdef __ANDROID__
    return UpdateJni().string(0);
#else
    return {};
#endif
}

std::string appUpdateNotes()
{
    if (devAutotestIsEnabled()) return "## What's new\n- Clearer touch controls and **settings**.\n- Updates from GitHub, with a verified APK.\n- Saves and game files stay in place.\n\nClose this window to continue playing.";
#ifdef __ANDROID__
    return UpdateJni().string(2);
#else
    return {};
#endif
}

void appUpdateProgress(long long* done, long long* total)
{
    *done = *total = 0;
    if (devAutotestIsEnabled()) {
        *total = 28 * 1024 * 1024;
        *done = gTestReady ? *total : *total * 4 / 10;
        return;
    }
#ifdef __ANDROID__
    UpdateJni jni;
    jmethodID call = jni.method("updateProgress", "()[J");
    if (call == nullptr) return;
    jlongArray value = static_cast<jlongArray>(jni.env->CallStaticObjectMethod(jni.type, call));
    if (jni.failed() || value == nullptr) return;
    if (jni.env->GetArrayLength(value) == 2) {
        jlong progress[2];
        jni.env->GetLongArrayRegion(value, 0, 2, progress);
        if (!jni.failed()) {
            *done = progress[0];
            *total = progress[1];
        }
    }
    jni.env->DeleteLocalRef(value);
#endif
}

bool appUpdateReady()
{
    if (devAutotestIsEnabled()) return gTestReady;
#ifdef __ANDROID__
    return UpdateJni().boolean("updateReady");
#else
    return false;
#endif
}

bool appUpdateHasRelease()
{
    return !appUpdateTag().empty();
}

void appUpdateCheck(bool manual)
{
    if (devAutotestIsEnabled()) {
        if (manual) gTestState = gTestTag.empty() ? AppUpdateState::UpToDate : gTestReady ? AppUpdateState::Downloaded
                                                                                          : AppUpdateState::Available;
        return;
    }
#ifdef __ANDROID__
    UpdateJni().flag("updateCheck", manual);
#endif
}

void appUpdateDownload()
{
    if (devAutotestIsEnabled()) {
        gTestState = AppUpdateState::Downloading;
        return;
    }
#ifdef __ANDROID__
    UpdateJni().action("updateDownload");
#endif
}

void appUpdateInstall()
{
    if (gGameLoaded) return;
    if (devAutotestIsEnabled()) {
        if (gTestActive && gTestReady) {
            gTestState = AppUpdateState::Installing;
            gTestInstalls++;
        }
        return;
    }
#ifdef __ANDROID__
    UpdateJni().action("updateInstall");
#endif
}

void appUpdateCancel()
{
    if (devAutotestIsEnabled()) {
        gTestState = AppUpdateState::Available;
        return;
    }
#ifdef __ANDROID__
    UpdateJni().action("updateCancel");
#endif
}

void appUpdateDelete()
{
    if (devAutotestIsEnabled()) {
        gTestReady = false;
        gTestState = AppUpdateState::Available;
        return;
    }
#ifdef __ANDROID__
    UpdateJni().action("updateDelete");
#endif
}

void appUpdateSetActive(bool active)
{
    active = active && !gGameLoaded;
    if (devAutotestIsEnabled()) {
        gTestActive = active;
        return;
    }
#ifdef __ANDROID__
    UpdateJni().flag("updateSetActive", active);
#endif
}

void appUpdateTestState(AppUpdateState state, const char* tag, AppUpdateError error)
{
    if (!devAutotestIsEnabled()) return;
    gTestState = state;
    gTestError = error;
    gTestTag = tag != nullptr ? tag : "";
    gTestReady = state == AppUpdateState::Downloaded || state == AppUpdateState::Permission;
}

int appUpdateTestInstalls()
{
    return devAutotestIsEnabled() ? gTestInstalls : 0;
}

} // namespace fallout
