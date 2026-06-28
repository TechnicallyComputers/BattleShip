# Android matchmaking-entry fdsan SIGABRT — system DNS resolver Parcel fd double-close

**Date:** 2026-06-26
**Status:** Fix corrected 2026-06-27 (embed file forced ARES OFF — see Correction below); Android build/soak pending — cannot be verified on desktop
**Area:** `cmake/Ssb64CurlAndroid.cmake`, `cmake/curl/FindCares.cmake` (new), `cmake/curl/patch_curl_android.cmake`, `cmake/curl/Ssb64CurlAndroidEmbed.cmake`, `port/net/matchmaking/mm_matchmaking.c`

## Symptom

Android client **SIGABRT** immediately on **entering matchmaking** (before any ICE agent
exists), Linux host in the same match unaffected. Port log:

```
Matchmaking: worker thread online base=https://netplay.technicallycomputers.ca
Automatch: state 0->1 (IDLE->ENSURE)
Matchmaking: POST .../v1/heartbeat -> HTTP 404        # expected (404 == auth OK)
Matchmaking: reusing cached player credential
!!!! CRASH SIGABRT fault_addr=0x... 
---- main-thread backtrace (fault context) ----
---- end backtrace ----                              # empty: abort off the main thread
```

logcat:

```
F libc: fdsan: attempted to close file descriptor 129, expected to be unowned,
        actually owned by Parcel 0x719fbeeff0   (pid 32132, tid 32326)
```

`tid 32326` matches the crash register `x1=0x7e46` and is ≠ pid → the abort is on the
**matchmaking worker thread** (`SSB64MmWorker`), not the game thread (hence the empty
in-process backtrace). It fires during the worker's second back-to-back HTTPS call — the
`GET /v1/turn-credentials` prefetch in `mmRunEnsurePushPlayerReady` — right after the
heartbeat.

## Root cause

The Android `netmenu` curl was built with `ENABLE_ARES=OFF` + `ENABLE_THREADED_RESOLVER=ON`
and mbedTLS. In that stack the **only** component that touches a **Parcel/Binder** fd is
Android's system `getaddrinfo`, serviced by the `netd`/`DnsResolver` Binder service. mbedTLS
and curl's raw sockets never do. So fd 129 ("owned by Parcel") is an **Android DNS-resolver
fd**: curl's threaded resolver closes a socket fd whose number was recycled into a framework
Parcel fd, and the second (untagged) close trips bionic **fdsan** → fatal abort. The rapid
heartbeat → turn-credentials request burst (fresh handle + new connection + new DNS lookup
each time, no keep-alive) maximizes the fd-reuse window.

This is the same **"owned by Parcel"** variant seen earlier during the queue-poll storm
(`android_ice_connect_fdsan_2026-05-30.md`), which was only ever *rate-limit-mitigated*,
never root-caused. fdsan is non-fatal on desktop, so Linux/Windows were never affected.

## Fix

Switch Android curl to the **c-ares async resolver**, which performs DNS over its own
UDP/TCP sockets — eliminating the Android system resolver and all its Parcel-backed fds.

- `cmake/Ssb64CurlAndroid.cmake`: new `ssb64_android_fetch_cares()` (FetchContent c-ares
  v1.34.5, static + PIC, no tools/tests/install), called before curl. Flip curl to
  `ENABLE_ARES=ON` / `ENABLE_THREADED_RESOLVER=OFF`. Pass `PATCH_FIND_CARES` to the patch.
- `cmake/curl/FindCares.cmake` (new): overrides curl's stock module (which `find_library`s
  the not-yet-built `.a` and fails at configure time). Resolves `CARES_LIBRARIES` to the
  in-tree `c-ares::cares` ALIAS target and `CARES_INCLUDE_DIR` to the c-ares source include —
  same shim pattern as `FindMbedTLS.cmake`.
- `cmake/curl/patch_curl_android.cmake`: optional `PATCH_FIND_CARES` arg copies the override
  into curl's `CMake/`.

### Android DNS-server requirement (important)

c-ares **cannot auto-discover DNS servers on Android** — the `net.dns*` system properties
were removed in Android 8.0. The client therefore supplies servers explicitly:

- `port/net/matchmaking/mm_matchmaking.c` (`mmHttpsRequestInternal`, `#if defined(__ANDROID__)`):
  `CURLOPT_DNS_SERVERS`, default `1.1.1.1,8.8.8.8,9.9.9.9`, overridable via
  `SSB64_MATCHMAKING_DNS_SERVERS`.

Trade-off: hardcoded public resolvers ignore the device's configured DNS and can fail on
networks that block external DNS / captive portals. Acceptable for a netplay client hitting a
fixed public matchmaking host (which already needs open UDP for ICE/TURN). **Future
hardening:** call `ares_library_init_jvm()` + `ares_library_init_android(connectivityManager)`
at startup (JNI plumbing in `android/app`) to use the device's own resolvers, then drop the
`CURLOPT_DNS_SERVERS` default.

## Verification

- Desktop netmenu (`build-netmenu`) rebuilds clean — the `__ANDROID__` block is excluded and
  the cmake changes are Android-only (desktop uses vcpkg/system curl). **Android NDK/Gradle
  build could not be run in the dev sandbox; build + soak on-device required.**
- On Android, confirm at startup: `SSB64 Android netmenu: CURL::libcurl (... c-ares async DNS ...)`.
- Soak: matchmaking entry no longer aborts; `GET /v1/turn-credentials` succeeds; no fdsan
  `owned by Parcel` in logcat through `ICE_CONNECT` and queue wait.
- If DNS fails on a restricted network: set `SSB64_MATCHMAKING_DNS_SERVERS` to a reachable
  resolver, or pursue the `ares_library_init_android` hardening.

## Correction (2026-06-27): c-ares was never actually linking

The 2026-06-26 fix flipped `ENABLE_ARES` in `ssb64_android_provide_curl()` (parent scope)
but **missed the injected embed file**. `patch_curl_android.cmake` splices
`cmake/curl/Ssb64CurlAndroidEmbed.cmake` into curl's *own* `CMakeLists.txt` right after
`project(CURL)`, and that file ran:

```cmake
set(ENABLE_ARES OFF CACHE BOOL "..." FORCE)
set(ENABLE_THREADED_RESOLVER ON CACHE BOOL "..." FORCE)
```

Because it executes **inside** curl's configure with `FORCE`, it overrode the parent's
`ENABLE_ARES ON`. Net result: curl was still built with the **threaded getaddrinfo
resolver, no c-ares** — i.e. the bug was never actually fixed, only masked once the
per-thread curl handle reuse (`mmCurlThreadHandle`, 2026-06-27) collapsed the fd-reuse
window enough to stop the abort.

Confirmed by a runtime probe added to `mmMatchmakingStartup`
(`curl_version_info`): the APK logged `async_dns=1 c-ares=(none)` — `async_dns` is the
threaded resolver, `(none)` means c-ares absent.

**Fix:** `cmake/curl/Ssb64CurlAndroidEmbed.cmake` now sets `ENABLE_ARES ON` /
`ENABLE_THREADED_RESOLVER OFF` to mirror the parent intent (it is the authoritative
in-curl-tree override, so it MUST match).

**Clean-rebuild requirement:** FetchContent runs `PATCH_COMMAND` only once, on first
populate, and the curl sub-build caches `ENABLE_ARES`. An incremental reconfigure will
**not** pick this up. Wipe the Android build's `_deps/curl-*` and `_deps/cares-*` (or the
whole build dir) and rebuild, then re-verify `c-ares=<ver>` in the startup log. This stale
`_deps` cache is the likely reason an earlier "rebuild" still showed `c-ares=(none)`.

## Related

- `docs/bugs/android_ice_connect_fdsan_2026-05-30.md` — prior fdsan variants (libjuice UDP fd
  + the queue-poll "owned by Parcel" variant this change root-causes)
