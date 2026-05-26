#include "app_paths.h"
#include <ssb64_paths_capi.h>

#include <libultraship/libultraship.h>
#include <SDL2/SDL.h>

#include <cstring>
#include <filesystem>
#include <system_error>

#if defined(__ANDROID__)
extern "C" int ssb64_android_user_data_dir_utf8(char *out, size_t cap);
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace ssb64 {

std::string RealAppBundlePath() {
#if defined(__ANDROID__)
    /* externalFilesDir — same tree AssetExtractor uses for f3d.o2r / ssl / port/net/assets. */
    return Ship::Context::GetAppDirectoryPath();
#elif defined(__linux__)
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !exe.empty()) {
        return exe.parent_path().string();
    }
#elif defined(_WIN32)
    /* Match libultraship Context::GetAppBundlePath(): UTF-8 exe dir for portable zip layouts. */
    std::wstring progpath(MAX_PATH, L'\0');
    int len = GetModuleFileNameW(NULL, progpath.data(), static_cast<DWORD>(progpath.size()));
    if (len != 0 && len < static_cast<int>(progpath.size())) {
        progpath.resize(static_cast<size_t>(len));
        const auto lastSlash = progpath.find_last_of(L'\\');
        if (lastSlash != std::wstring::npos) {
            progpath.erase(lastSlash);
        }
        len = WideCharToMultiByte(CP_UTF8, 0, progpath.data(), static_cast<int>(progpath.size()),
                                  nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string utf8(static_cast<size_t>(len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, progpath.data(), static_cast<int>(progpath.size()),
                                utf8.data(), len, nullptr, nullptr);
            return utf8;
        }
    }
#endif
    return Ship::Context::GetAppBundlePath();
}

} // namespace ssb64

extern "C" int ssb64_RealAppBundlePathUtf8(char *out, size_t cap)
{
    std::string p;

    if (out == nullptr || cap == 0) {
        return 0;
    }

    p = ssb64::RealAppBundlePath();
    if (p.size() + 1 > cap) {
        out[0] = '\0';
        return 0;
    }

    memcpy(out, p.c_str(), p.size() + 1);

    return 1;
}

extern "C" int ssb64_UserDataDirUtf8(char *out, size_t cap)
{
    std::string dir;

    if (out == nullptr || cap == 0) {
        return 0;
    }

#if defined(__ANDROID__)
    /* Primary: externalFilesDir/ (set from Java before SDL_main). */
    if (ssb64_android_user_data_dir_utf8(out, cap) != 0) {
        return 1;
    }
#endif

    /* GetAppDirectoryPath() uses GetInstance()->mShortName when appName is empty; safe only after PortInit. */
    if (Ship::Context::GetInstance() != nullptr) {
        try {
            dir = Ship::Context::GetAppDirectoryPath();
        } catch (...) {
            dir.clear();
        }
    }

    if (dir.empty()) {
        if (char *pref = SDL_GetPrefPath(nullptr, "BattleShip")) {
            dir = pref;
            SDL_free(pref);
        }
    }

    if (dir.empty() || dir.size() + 1 > cap) {
        out[0] = '\0';
        return 0;
    }

    memcpy(out, dir.c_str(), dir.size() + 1);
    return 1;
}
