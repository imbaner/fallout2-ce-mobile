// The game folders' walks for the Android app's Java side
// (com.alexbatalov.fallout2ce.FileTree, src/file_tree.h): names as bytes.
// Failures are IOExceptions; an exception of the progress callback (cancel)
// stops the copy and goes on to the Java caller.

#include <jni.h>

#include <string>

#include "file_tree.h"

using fallout::FileTreeError;

namespace {

void throwIo(JNIEnv* env, const std::string& message)
{
    if (env->ExceptionCheck()) {
        return;
    }
    jclass exceptionClass = env->FindClass("java/io/IOException");
    if (exceptionClass != nullptr) {
        env->ThrowNew(exceptionClass, message.c_str());
    }
}

// The app's own paths (its folders, game ids): plain text.
std::string pathOf(JNIEnv* env, jstring path)
{
    const char* chars = env->GetStringUTFChars(path, nullptr);
    if (chars == nullptr) {
        return std::string();
    }
    std::string text = chars;
    env->ReleaseStringUTFChars(path, chars);
    return text;
}

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_alexbatalov_fallout2ce_FileTree_nativeSize(JNIEnv* env, jclass, jstring path)
{
    uint64_t size = 0;
    FileTreeError error;
    if (!fallout::fileTreeSize(pathOf(env, path), &size, &error)) {
        throwIo(env, error.message());
        return 0;
    }
    return static_cast<jlong>(size);
}

JNIEXPORT void JNICALL Java_com_alexbatalov_fallout2ce_FileTree_nativeCopy(JNIEnv* env, jclass, jstring from, jstring to, jobject progress)
{
    jmethodID update = nullptr;
    if (progress != nullptr) {
        jclass progressClass = env->GetObjectClass(progress);
        update = env->GetMethodID(progressClass, "update", "(J)V");
        env->DeleteLocalRef(progressClass);
        if (update == nullptr) {
            return;
        }
    }
    FileTreeError error;
    bool copied = fallout::fileTreeCopy(pathOf(env, from), pathOf(env, to), [&](uint64_t done) {
        if (update == nullptr) {
            return true;
        }
        env->CallVoidMethod(progress, update, static_cast<jlong>(done));
        return !env->ExceptionCheck();
    }, &error);
    if (!copied) {
        throwIo(env, error.message());
    }
}

JNIEXPORT void JNICALL Java_com_alexbatalov_fallout2ce_FileTree_nativeDelete(JNIEnv* env, jclass, jstring path)
{
    FileTreeError error;
    if (!fallout::fileTreeDelete(pathOf(env, path), &error)) {
        throwIo(env, error.message());
    }
}

} // extern "C"
