# Injected into curl by patch_curl_android.cmake (BattleShip Android netmenu).
# Runs immediately after project(CURL) so CACHE FORCE wins over later option() calls.
set(CURL_DISABLE_INSTALL ON CACHE BOOL "SSB64 embedded curl" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "SSB64 embedded curl" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "SSB64 embedded curl" FORCE)
