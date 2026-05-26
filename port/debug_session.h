#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ssb64_debug_session_kind {
    SSB64_DEBUG_SESSION_NONE = 0,
    SSB64_DEBUG_SESSION_LOG_ONLY = 1,
    SSB64_DEBUG_SESSION_ENV = 2,
} ssb64_debug_session_kind;

/**
 * Read and delete <userDataDir>/.battleship_debug_session if present.
 * user_dir may include a trailing slash.
 */
ssb64_debug_session_kind ssb64_consume_debug_session(const char *user_dir);

#ifdef __cplusplus
}
#endif
