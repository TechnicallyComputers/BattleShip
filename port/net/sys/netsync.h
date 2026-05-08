#ifndef _SYNETSYNC_H_
#define _SYNETSYNC_H_

/*
 * Deterministic gameplay fingerprint helpers for netplay debugging — **not** authoritative desync repair.
 *
 * These hashes sample a narrow slice of fighter/map state so two machines can compare `port_log` lines
 * after the same confirmed input window. They intentionally ignore most of the ROM; expanding coverage
 * belongs in dedicated investigations, not silent hot paths.
 */
#include <PR/ultratypes.h>

struct FTStruct;

/* XOR-FNV style fold of per-fighter contributions (order independent over fighter list). */
extern u32 syNetSyncHashBattleFighters(void);
/* Map collision / kinematic sentinel hash for broad “world moved” diagnostics. */
extern u32 syNetSyncHashMapCollisionKinematics(void);
/*
 * `gcRunAll`-shaped traversal fingerprint (common links + process queue order). Cross-peer-stable fields only;
 * see `gcPortHashGcRunAllTraversalFingerprint` / `docs/netplay_frame_composition.md`.
 */
extern u32 syNetSyncHashGcRunAllTraversalFingerprint(void);
/* Narrow per-fighter fingerprint for phase tracing (subset of battle fighter hash). */
extern u32 syNetSyncHashFighterStructLight(const struct FTStruct *fp);

#endif /* _SYNETSYNC_H_ */
