# Injected into curl by patch_curl_android.cmake (BattleShip Android netmenu).
# Runs immediately after project(CURL) so CACHE FORCE wins over later option() calls.
#
# On Linux hosts (e.g. Arch), curl defaults CURL_USE_PKGCONFIG=ON and may find
# host c-ares/zlib, injecting -I/usr/include into NDK arm64 builds. Keep
# dependencies explicit (mbedTLS HTTPS + the in-tree c-ares static lib).
#
# IMPORTANT: this file runs inside curl's own configure and its FORCE overrides the
# parent ssb64_android_provide_curl() cache. It MUST mirror the parent's resolver
# choice. c-ares (not curl's threaded getaddrinfo resolver) is required on Android:
# the threaded resolver calls getaddrinfo -> netd/DnsResolver Binder, whose
# Parcel-owned fds are double-closed under bionic fdsan (matchmaking-entry SIGABRT).
# See docs/bugs/android_matchmaking_cares_dns_fdsan_2026-06-26.md.

set(CURL_DISABLE_INSTALL ON CACHE BOOL "SSB64 embedded curl" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "SSB64 embedded curl" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "SSB64 embedded curl" FORCE)

set(CURL_USE_PKGCONFIG OFF CACHE BOOL "SSB64 embedded curl: no host pkg-config" FORCE)
set(ENABLE_ARES ON CACHE BOOL "SSB64 embedded curl: in-tree c-ares async DNS (fdsan-safe)" FORCE)
set(ENABLE_THREADED_RESOLVER OFF CACHE BOOL "SSB64 embedded curl: c-ares replaces getaddrinfo" FORCE)
