// Copyright © 2023 Cassia Developers, all rights reserved.

#include "cassia/wine_ctx.h"
#include <filesystem>
#include <jni.h>
#include <android/native_window_jni.h>

std::mutex stateMutex;
std::optional<cassia::WineContext> wineCtx;
ANativeWindow *nativeWindow{nullptr};

extern "C" JNIEXPORT void JNICALL
Java_cassia_app_CassiaManager_startServer(
        JNIEnv* env,
        jobject /* this */,
        jstring jRuntimePath, jstring jPrefixPath) {
    const char *runtimePathStr{env->GetStringUTFChars(jRuntimePath, nullptr)};
    const char *prefixPathStr{env->GetStringUTFChars(jPrefixPath, nullptr)};
    std::filesystem::path runtimePath{runtimePathStr};
    std::filesystem::path prefixPath{prefixPathStr};
    env->ReleaseStringUTFChars(jRuntimePath, runtimePathStr);
    env->ReleaseStringUTFChars(jPrefixPath, prefixPathStr);

    {
        std::scoped_lock lock{stateMutex};
        wineCtx.emplace(runtimePath, prefixPath);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_cassia_app_CassiaManager_stopServer(
        JNIEnv* env,
        jobject /* this */) {
    std::scoped_lock lock{stateMutex};
    wineCtx.reset();
}

extern "C" JNIEXPORT void JNICALL
Java_cassia_app_CassiaManager_setSurface(
        JNIEnv* env,
        jobject /* this */,
        jobject surface) {
    std::scoped_lock lock{stateMutex};
    nativeWindow = surface == nullptr ? nullptr : ANativeWindow_fromSurface(env, surface);

    // TODO: Hook up to compositor.
}
