#pragma once

/**
 * debug_env.h — Optional developer-only env file in the user-data directory.
 *
 * Loads <userDataDir>/debug.env after PortInit (see ssb64_load_debug_env_file).
 * Not a player-facing settings file; values only apply when unset in the process
 * environment (shell exports on desktop still win).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Resolve user-data dir, read debug.env if present, apply allowed keys via setenv. */
void ssb64_load_debug_env_file(void);

#ifdef __cplusplus
}
#endif
