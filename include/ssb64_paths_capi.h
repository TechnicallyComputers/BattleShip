#pragma once

/**
 * C-callable path helpers for port/net TUs that cannot include C++ headers.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Writes UTF-8 bundle/exe directory into `out` (NUL-terminated). Returns 1 on success, 0 if truncated/error. */
int ssb64_RealAppBundlePathUtf8(char *out, size_t cap);

/**
 * Writable per-user app data directory (same tree as ssb64.log / ssb64_save.bin / debug.env).
 * On Android this is Internal storage/Documents/BattleShip/ when Java has published the path.
 * May include a trailing path separator. Returns 1 on success, 0 if truncated/error.
 */
int ssb64_UserDataDirUtf8(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
