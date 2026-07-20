#ifndef _SYNETSYNC_RNG_TRACE_H_
#define _SYNETSYNC_RNG_TRACE_H_

#include <PR/ultratypes.h>

#ifdef PORT

/* Game LCG hooks (syUtilsRandUShort and syUtilsRandFloat paths). Offline / netmenu-off: empty stubs in netsync_hash_stubs.c */
extern void syNetSyncRngTraceBeforeGameSeedStep(void);
/* `caller_site` = return address of syUtilsRandFloat/UShort (consumer PC). */
extern void syNetSyncRngTraceAfterGameSeedStep(s32 seed_after, u32 caller_site);

/*
 * Ordered per-tick RNG step log for cross-ISA bisect (`SSB64_NETPLAY_RNG_HASH_TRACE=1` and/or
 * `SSB64_NETPLAY_RNG_STEP_TRACE=1`). Also emitted automatically on RNG hash drift
 * (`frame_commit_rng_diverge`, `resim_tick`, `env_trace`).
 */
extern void syNetSyncLogRngHashWalkTrace(u32 sim_tick);
extern void syNetSyncLogRngHashDriftDiag(u32 sim_tick, u32 local_rng, u32 peer_rng, const char *reason);

#endif /* PORT */

#endif /* _SYNETSYNC_RNG_TRACE_H_ */
