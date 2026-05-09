#ifndef _SYNETPEER_H_
#define _SYNETPEER_H_

/*
 * ------------------------------------------------------------
 * NetPeer — debug UDP simulation transport for networked VS.
 *
 * Scope: configures a bound socket (`SSB64_NETPLAY_BIND`) → peer (`…_PEER`),
 * exchanges bootstrap metadata (MATCH_CONFIG), then bundles per-tick input.
 *
 * Layers you must mentally separate:
 *   1) Bootstrap control plane (READY / START …) enters both processes into identical VS metadata.
 *   2) Battle barrier + post-barrier gates (`syNetPeerCheckBattleExecutionReady`): wall-clock deadline release,
 *      optional strict INPUT_BIND, then host-led BATTLE_EXEC_SYNC (frozen sim tick + VI phase bucket)
 *      before the first `syNetInput` tick increment (Linux UDP); optional `SSB64_NETPLAY_TICK_GRID_EXEC_GATE=1`
 *      additionally requires `syNetTickGridLockIsLocked()` for guests until tick-grid calibration completes.
 *      Taskman + PortPushFrame counters resync at barrier release.
 *   3) Runtime transport (`syNetPeerUpdate`): after execution is ready, emits INPUT payloads + recv pump.
 *
 * Game code usually calls `syNetPeerUpdateBattleGate` early inside VS scenes (receive + barrier),
 * then `syNetPeerUpdate` only when execution is unlocked so rollback/input ordering stays coherent.
 *
 * Automatch HTTPS wiring lives behind `#if defined(SSB64_NETMENU)` — those symbols configure the UDP
 * path from matcher output without inventing gameplay authority (wall clocks still negotiated P2P).
 * ------------------------------------------------------------
 */

#include <PR/ultratypes.h>
#include <ssb_types.h>

/* Parses env overrides + optional automatch-derived runtime (Linux). Initializes globals / socket config. */
extern void syNetPeerInitDebugEnv(void);
/* Enables socket + resets transport counters/barrier after MATCH_CONFIG handshake (debug + automatch). */
extern void syNetPeerStartVSSession(void);
/* FALSE while VS gameplay must remain frozen waiting on barrier handshake or rollback warmup gate inside peer. */
extern sb32 syNetPeerCheckBattleExecutionReady(void);
/* Alias semantic: identical to execution-ready for legacy callsites verifying barrier release semantics. */
extern sb32 syNetPeerCheckStartBarrierReleased(void);
/*
 * Ingress + barrier driver invoked from VS scenes before input publish.
 * Receives queued packets, emits BATTLE_READY, advances clock-aligned barrier bookkeeping, retries INPUT_BIND.
 */
extern void syNetPeerUpdateBattleGate(void);
/*
 * For **active** Linux UDP VS sessions, `syNetInputFuncRead` calls `syNetPeerUpdateBattleGate()` first (superset of recv + delay apply); this entry point remains for call sites that only need ingress + delay sync without the full gate.
 * Keeps `sSYNetInputRemoteHistory` fresh for the current sim tick (Linux UDP; no-op elsewhere).
 */
extern void syNetPeerPumpIngressBeforeInputRead(void);
/* Full net tick after execution-ready: emits INPUT payloads, stats logs, invokes rollback mismatch scan hooks. */
extern void syNetPeerUpdate(void);
/* Closes socket + tears down bookkeeping at VS unload / session end. */
extern void syNetPeerStopVSSession(void);
extern sb32 syNetPeerIsVSSessionActive(void);
/*
 * Barrier VI contract Hz (host default 60; guest latched from BATTLE_START_TIME wire layout).
 * Used by PortPushFrame to lock netplay sim stepping / taskman pacing independently of host monitor Hz.
 * Returns 0 when no VS session is active.
 */
extern u32 syNetPeerGetVsContractViHz(void);
#ifdef PORT
/*
 * Applies staged MATCH_CONFIG to replay/battle globals, RNG seed, and (unless suppressed) scene_curr.
 * Safe to call once before syNetReplayStartVSSession from scVSBattleStartBattle. Idempotent.
 */
extern void syNetPeerCommitStagedBootstrapMetadataForBattleStart(void);
/*
 * Host-frame barrier pump (`PortPushFrame`): keeps UDP recv + clock/barrier state moving during VS load
 * or taskman net-freeze. Disable with `SSB64_NETPLAY_HOSTFRAME_GATE_PUMP=0`.
 */
extern sb32 syNetPeerShouldPumpBattleGateOnHostFrame(void);
extern void syNetPeerPumpBattleGateOnHostFrame(void);
/*
 * When true, `PortPushFrame` skips idle framebuffer present so both peers show a static image during sync.
 * Disable with `SSB64_NETPLAY_SYNC_PRESENT_HOLD=0`.
 */
extern sb32 syNetPeerWantsSyncPresentHold(void);
/*
 * Netplay tick / frame alignment diagnostics (`SSB64_NETPLAY_TICK_DIAG`, cached): 0 default if unset; 1 gates
 * `tick_diag` at execution begin, the extra NetSync `tick_diag` line, barrier_wait clock/VI fields, INPUT send/recv
 * slot routing, taskman barrier resync logs on Linux UDP, and **extended NetSync input diagnostics** (remote-ring
 * value/diag checksum windows, `hist_diag_win` / `remote_ring_diag_win`, and `pub_vs_remote mismatch` when publish vs
 * wire ring diverges). Disable those extras only with **`SSB64_NETPLAY_NETSYNC_INPUT_DIAG=0`** (legacy
 * **`SSB64_NETPLAY_REMOTE_RING_CHECKSUM=1`** still forces extended lines even when suppressed). While
 * `syNetPeerIsVSSessionActive`, the effective level is at least 1 for those gated lines. Host `clock_sync_sample`
 * (each completed TIME_PONG) and `tick_diag tag=barrier_release` after the main barrier release line are always logged
 * on Linux UDP (not env-gated). Level 2 is currently unused (reserved).
 */
extern s32 syNetPeerGetTickDiagLevel(void);
#endif
/* TRUE when networked VS is actively decoupling local hardware polling from simulated player slots — netinput uses this latch. */
extern sb32 syNetPeerIsOnlineP2PHardwareDecoupleActive(void);
/*
 * Resolve which SDL/controller index publishes into the simulated player slot controlled by THIS machine online.
 * (Sim slot differs from SDL index when hosting/guest swaps P1 wiring.)
 */
extern s32 syNetPeerResolveLocalHardwareDevice(s32 sim_player);
extern s32 syNetPeerGetLocalSimSlot(void);
/* Dual-local INPUT (v5): second sim slot controlled from this machine, or -1. */
extern s32 syNetPeerGetExtraLocalSenderSimSlot(void);
extern s32 syNetPeerGetRemotePlayerSlot(void);
extern s32 syNetPeerGetRemoteHumanSlotCount(void);
extern sb32 syNetPeerGetRemoteHumanSlotByIndex(s32 index, s32 *out_slot);
extern u32 syNetPeerGetHighestRemoteTick(void);
/* Committed VS input delay (wire tick = sim tick + delay for GatherHistoryBundle / staged INPUT). */
extern u32 syNetPeerGetCommittedInputDelay(void);
/* Authoritative wire index: `sim_tick + committed_delay` (saturating add). Inverse clamps to 0. */
extern u32 syNetPeerDelayWireTickFromSim(u32 sim_tick);
extern u32 syNetPeerDelaySimTickFromWire(u32 wire_tick);
#if defined(PORT) && !defined(_WIN32)
/*
 * Applies host ramp + guest INPUT_DELAY_SYNC pending in one place (after `ReceiveRemoteInput`).
 * See `syNetPeerPumpIngressBeforeInputRead` / `syNetPeerUpdateBattleGate`.
 */
extern void syNetPeerApplyPendingDelayContract(void);
/* `~(u32)0` until first execution-begin for this session; used by delay-sync diagnostics. */
extern u32 syNetPeerGetDelaySyncDiagExecReadySimTick(void);
#endif
#ifdef PORT
/*
 * Skew pacing: returns TRUE when the following full `scVSBattleFuncUpdate` should be skipped because local sim tick leads `HighestRemoteTick` by
 * more than the configured lead cap (`SSB64_NETPLAY_SKEW_LEAD_MAX_TICKS`, default 4 — see netpeer.c). See docs/netplay_pacing.md.
 */
extern sb32 syNetPeerShouldHoldSimTickForSkewPacing(u32 tick, s32 *out_skew);
extern u32 syNetPeerGetSkewPacingHoldFrameCount(void);
#if !defined(_WIN32)
/*
 * Catch-up when **behind** the remote tick frontier: `HighestRemoteTick - local_sim_tick >= threshold`.
 * `SSB64_NETPLAY_SKEW_BEHIND_MAX_TICKS` (default **0** = off): extra `syNetPeerUpdateBattleGate` before
 * `SSB64_NETPLAY_STALL_UNTIL_REMOTE`, and bypass strict stall for that `syNetInputFuncRead` pass (experimental).
 * Optional: `SSB64_NETPLAY_SKEW_BEHIND_LOG=1` for rate-limited `catch_up_behind` lines. See docs/netplay_pacing.md.
 */
extern sb32 syNetPeerRunCatchUpBehindBeforeInputStall(u32 local_sim_tick);
extern sb32 syNetPeerShouldRelaxStallUntilRemoteForCatchUp(u32 local_sim_tick);
#endif
/*
 * When tick-grid exec gate is enabled (SSB64_NETPLAY_TICK_GRID_EXEC_GATE=1) and the grid is locked, PortPushFrame
 * skips decouple deadline holds for sim-tick indexing (Linux UDP).
 */
extern sb32 syNetPeerShouldBypassDecoupleSimPacingForTickGrid(void);
#endif

#if defined(PORT) && !defined(_WIN32)
/*
 * Netplay sync pipeline (bootstrap UDP probe → MATCH_CONFIG → barrier → INPUT_BIND → battle_exec_sync → running).
 * Use for UX/debug logging; `syNetPeerGetSyncPipelineProgress` reports coarse steps (UDP rounds count precisely).
 */
typedef enum SYNetPeerSyncPipelinePhase
{
	nSYNetPeerSyncPipeline_Inactive = 0,
	nSYNetPeerSyncPipeline_Disabled,
	nSYNetPeerSyncPipeline_UdpLink,
	nSYNetPeerSyncPipeline_Bootstrap,
	nSYNetPeerSyncPipeline_ClockBarrier,
	nSYNetPeerSyncPipeline_InputBind,
	nSYNetPeerSyncPipeline_BattleExecSync,
	nSYNetPeerSyncPipeline_Running

} SYNetPeerSyncPipelinePhase;

extern SYNetPeerSyncPipelinePhase syNetPeerGetSyncPipelinePhase(void);
extern void syNetPeerGetSyncPipelineProgress(u32 *out_step, u32 *out_total);
/*
 * When `SSB64_NETPLAY_ABORT_ON_INPUT_MISMATCH_FATAL=1`, hard `abort()` only while the sync pipeline is `Running`
 * (steady battle). During bootstrap / barrier / exec-sync, mismatches are logged but do not kill the process.
 */
extern sb32 syNetPeerShouldHardAbortOnNetplayInputMismatch(void);
/*
 * Clears cached `getenv` reads in netpeer/netinput so per-match env changes apply without restarting the binary.
 * Call once per VS session start (invoked from `syNetPeerStartVSSession` and `syNetInputStartVSSession`).
 */
extern void syNetPeerRefreshCachedNetplayEnvForNewMatch(void);
/* GGPO-style merged max(last_confirmed tick) across INPUT peer_connect_status; FALSE if no confirmed ticks yet. */
extern sb32 syNetPeerGetMergedMinConfirmedSimTick(s32 *out_min_tick);
#endif

#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)

/* Automatch UX wants HTTPS bootstrap to stall scene transitions until metadata is intentionally applied. */
extern sb32 gSYNetPeerSuppressBootstrapSceneAdvance;

/* Runtime netplay/session (explicit config wins vs env vars for automatch bootstrap). */
extern sb32 syNetPeerConfigureUdpForAutomatch(const char *bind_hostport, const char *peer_hostport, u32 session_id,
                                              sb32 you_are_host, u32 input_delay);
extern sb32 syNetPeerSetAutomatchNegotiation(sb32 enabled);
extern void syNetPeerSetAutomatchLocalOffer(u16 ban_mask_le, u8 fkind, u8 costume, u32 nonce_opt);
extern s32 syNetPeerGetUdpSocketFd(void); /* Requires bound socket (-1 otherwise). */
extern sb32 syNetPeerOpenSocket(void);
extern sb32 syNetPeerRunBootstrap(void);

#endif

#endif /* _SYNETPEER_H_ */
