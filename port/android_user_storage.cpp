// android_user_storage.cpp — JNI bridge for externalFilesDir user-data path.
//
// BootActivity / BattleShipActivity prepare the folder before SDL_main so
// ssb64_UserDataDirUtf8() can resolve saves, logs, and matchmaking.cred.

#if defined(__ANDROID__)

#include <SDL2/SDL.h>
#include <android/log.h>
#include <jni.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

#define LOG_TAG "ssb64.storage"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

std::mutex gDirMutex;
std::string gUserDataDir;

static void setUserDataDirFromBase(const char *base)
{
    if (base == nullptr || base[0] == '\0') {
        return;
    }
    gUserDataDir = base;
    if (gUserDataDir.back() != '/') {
        gUserDataDir += '/';
    }
}

/** BootActivity writes the sentinel before BattleShipActivity loads libmain. */
static void tryDiscoverUserDataDir()
{
    const char *ext = SDL_AndroidGetExternalStoragePath();
    if (ext == nullptr || ext[0] == '\0') {
        return;
    }

    std::string sentinel = std::string(ext) + ".battleship_user_data_dir";
    std::ifstream in(sentinel);
    if (!in.is_open()) {
        return;
    }

    std::string path;
    std::getline(in, path);
    while (!path.empty() && (path.back() == '\r' || path.back() == '\n')) {
        path.pop_back();
    }
    if (!path.empty()) {
        setUserDataDirFromBase(path.c_str());
        LOGI("loaded user-data dir from sentinel: %s", gUserDataDir.c_str());
    }
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)vm;
    (void)reserved;
    tryDiscoverUserDataDir();
    return JNI_VERSION_1_6;
}

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
