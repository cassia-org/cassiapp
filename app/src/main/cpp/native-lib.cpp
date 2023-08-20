#include <jni.h>
#include <string>
#include <android/native_window_jni.h>
#include "cassiasrv/server.h"

cassiasrv::Core gCore{};
cassiasrv::Server gServer{gCore};

extern "C" JNIEXPORT void JNICALL
Java_cassia_app_activity_RunnerActivity_runServer(
        JNIEnv* env,
        jobject /* this */) {
    gServer.Initialise();
    gServer.Run();
}

extern "C" JNIEXPORT void JNICALL
Java_cassia_app_activity_RunnerActivity_setSurface(
        JNIEnv* env,
        jobject /* this */,
        jobject surface) {
    ANativeWindow *nativeWindow{ANativeWindow_fromSurface(env, surface)};
    gCore.SetSurface(nativeWindow);
}
