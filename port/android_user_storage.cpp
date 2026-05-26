// android_user_storage.cpp — JNI bridge for externalFilesDir user-data path.
//
// BootActivity prepares the folder; BattleShipActivity publishes it to native
// after SDLActivity loads libSDL2.so + libmain.so (see publishUserDataDirToNative).
// Do not call SDL from JNI_OnLoad — SDL_AndroidGetExternalStoragePath needs the
// activity bridge, which is not ready during System.loadLibrary("main").

#if defined(__ANDROID__)

#include <android/log.h>
#include <jni.h>

#include <cstring>
#include <mutex>
#include <string>

#define LOG_TAG "ssb64.storage"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

std::mutex gDirMutex;
std::string gUserDataDir;

} // namespace

extern "C" void ssb64_android_set_user_data_dir(const char *path)
{
    if (path == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(gDirMutex);
    gUserDataDir = path;
    if (!gUserDataDir.empty() && gUserDataDir.back() != '/') {
        gUserDataDir += '/';
    }
}

extern "C" int ssb64_android_has_user_data_dir(void)
{
    std::lock_guard<std::mutex> lock(gDirMutex);
    return !gUserDataDir.empty();
}

extern "C" int ssb64_android_user_data_dir_utf8(char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(gDirMutex);
    if (gUserDataDir.empty() || gUserDataDir.size() + 1 > cap) {
        out[0] = '\0';
        return 0;
    }

    std::memcpy(out, gUserDataDir.c_str(), gUserDataDir.size() + 1);
    return 1;
}

extern "C" JNIEXPORT void JNICALL
Java_com_jrickey_battleship_UserStoragePaths_nativeSetUserDataDir(JNIEnv *env, jclass,
                                                                  jstring jpath)
{
    if (jpath == nullptr) {
        LOGW("nativeSetUserDataDir: null path");
        return;
    }

    const char *utf = env->GetStringUTFChars(jpath, nullptr);
    if (utf == nullptr) {
        LOGW("nativeSetUserDataDir: GetStringUTFChars failed");
        return;
    }

    ssb64_android_set_user_data_dir(utf);
    LOGI("native user-data dir: %s", utf);
    env->ReleaseStringUTFChars(jpath, utf);
}

#endif /* __ANDROID__ */
