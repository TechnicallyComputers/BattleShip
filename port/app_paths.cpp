#include "app_paths.h"
#include <ssb64_paths_capi.h>

#include <libultraship/libultraship.h>

#include <cstring>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace ssb64 {

std::string RealAppBundlePath() {
#if defined(__linux__)
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !exe.empty()) {
        return exe.parent_path().string();
    }
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (len != 0 && len < MAX_PATH) {
        std::filesystem::path exe(buf, buf + len);
        return exe.parent_path().string();
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
