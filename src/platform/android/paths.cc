#include "paths.h"

#include <jni.h>

#include <SDL.h>

namespace fallout {

namespace {

    // MainActivity's [method]: a path, or empty.
    std::string activityPath(const char* method)
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

    // The app's answer, else [fallback] (an app without games' folders).
    std::string pathOr(const char* method, const char* fallback)
    {
        std::string path = activityPath(method);
        if (path.empty() && fallback != nullptr) {
            path = fallback;
        }
        return path;
    }

} // namespace

const std::string& androidGameFolder()
{
    static const std::string path = pathOr("gameFolder", SDL_AndroidGetExternalStoragePath());
    return path;
}

const std::string& androidGameDataFolder()
{
    static const std::string path = pathOr("gameDataFolder", SDL_AndroidGetInternalStoragePath());
    return path;
}

std::string androidAppFile(const char* name)
{
    const char* folder = SDL_AndroidGetExternalStoragePath();
    if (folder == nullptr) {
        return name;
    }
    return std::string(folder) + "/" + name;
}

} // namespace fallout
