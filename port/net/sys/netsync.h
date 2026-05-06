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

/* XOR-FNV style fold of per-fighter contributions (order independent over fighter list). */
extern u32 syNetSyncHashBattleFighters(void);
/* Map collision / kinematic sentinel hash for broad “world moved” diagnostics. */
extern u32 syNetSyncHashMapCollisionKinematics(void);

#endif /* _SYNETSYNC_H_ */
