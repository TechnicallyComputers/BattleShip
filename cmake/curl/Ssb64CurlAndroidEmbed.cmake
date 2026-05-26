# Injected into curl by patch_curl_android.cmake (BattleShip Android netmenu).
# Runs immediately after project(CURL) so CACHE FORCE wins over later option() calls.
#
# On Linux hosts (e.g. Arch), curl defaults CURL_USE_PKGCONFIG=ON and may find
# host c-ares/zlib, injecting -I/usr/include into NDK arm64 builds. Keep
# dependencies explicit (mbedTLS only) and use the threaded resolver.

set(CURL_DISABLE_INSTALL ON CACHE BOOL "SSB64 embedded curl" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "SSB64 embedded curl" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "SSB64 embedded curl" FORCE)

set(CURL_USE_PKGCONFIG OFF CACHE BOOL "SSB64 embedded curl: no host pkg-config" FORCE)
set(ENABLE_ARES OFF CACHE BOOL "SSB64 embedded curl: no host c-ares" FORCE)
set(ENABLE_THREADED_RESOLVER ON CACHE BOOL "SSB64 embedded curl: NDK DNS" FORCE)
