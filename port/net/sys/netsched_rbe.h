#ifndef SYS_NETSCHED_RBE_H
#define SYS_NETSCHED_RBE_H

/*
 * BattleShip bridge into the shared retcomm-rbengine admission scheduler
 * (rbe_sched): the time-domain policy layer (invent grace, cushion rebuild,
 * pcap freeze, timesync pacing, arrival-driven delay controllers) that the
 * tick-arithmetic phase-lock gate lacks.
 *
 * Opt-in via SSB64_NETPLAY_RBE_SCHED:
 *   0 (default) — off, all hooks are cheap early-outs.
 *   1           — SHADOW: run the rbe verdict alongside the authoritative
 *                 syNetPeerEvaluateSharedCommitStep verdict, count and log
 *                 agreements/disagreements, change nothing.
 *   2           — CONSERVATIVE VETO: shadow, plus rbe may convert a
 *                 prediction-window advance into an R-hold (stall instead of
 *                 predict — strictly more conservative; the strict-R abort
 *                 watchdog still applies). Promote only after tier-1 soak.
 *
 * The netpeer phase-lock gate remains authoritative for tick ownership.
 * See docs/netplay_rbe_sched_integration_2026-08-21.md.
 */

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

struct SYNetPeerSharedCommitStep;

/* 0 off / 1 shadow / 2 conservative veto (env SSB64_NETPLAY_RBE_SCHED). */
extern int syNetRbeSchedTier(void);

/* TRUE when tier 3 is on AND the consumption mapping makes D a latency budget
 * (REAL-DELAY). Under ZERO-DELAY the adaptive path is plumbed but inert. */
extern sb32 syNetRbeSchedAdaptiveDelayActive(void);

/* FuncRead wire admission: observe (and at tier >= 2 possibly veto) the
 * authoritative verdict. Called once per VS pass right after
 * syNetPeerEvaluateSharedCommitStep; dedupes internally. */
extern void syNetRbeSchedShadowObserve(u32 sim_tick, struct SYNetPeerSharedCommitStep *shared);

/* Ingress: a strict (non-provisional) remote wire row arrived from the
 * network for (player, wire_tick). First arrival per ring generation is
 * stamped for arrival-age / slack telemetry. */
extern void syNetRbeSchedNoteRemoteWireArrival(s32 player, u32 wire_tick);

/* ms since the strict wire row for (player, wire_tick) arrived; ~(u32)0 when
 * unknown (never seen, or aliased out of the stamp ring). */
extern u32 syNetRbeSchedRemoteWireArrivalAgeMs(s32 player, u32 wire_tick);

/* Rollback episode completed its resim (mismatch_tick..target exclusive).
 * Feeds rbe note_mispredict(depth) + note_episode_boundary (cushion rebuild). */
extern void syNetRbeSchedNoteResimComplete(u32 mismatch_tick, u32 resim_target_tick);

/* The commit gate held this tick for the zero-onset host policy — the shadow
 * respects it (skips comparison) instead of judging it. */
extern void syNetRbeSchedNoteZeroOnsetHold(u32 sim_tick);

/* VS session teardown: emit the final shadow scorecard and unbind. */
extern void syNetRbeSchedNoteSessionStop(void);

#endif /* PORT && SSB64_NETMENU */

#endif /* SYS_NETSCHED_RBE_H */
