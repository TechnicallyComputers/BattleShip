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
 *   2) Ordered sync pipeline (see `syNetPeerGetSyncPipelinePhase`): UDP link → bootstrap → **clock barrier**
 *      (BATTLE_READY + TIME_PING/TIME_PONG NTP samples + `BATTLE_START_TIME` deadline; advanced from
 *      `syNetPeerUpdateBattleGate` via `syNetPeerUpdateStartBarrier`) → optional strict **INPUT_BIND** → host-led
 *      **BATTLE_EXEC_SYNC** → `syNetPeerCheckBattleExecutionReady` true → **Running** (steady INPUT).
 *      Optional `SSB64_NETPLAY_TICK_GRID_EXEC_GATE=1` additionally requires `syNetTickGridLockIsLocked()` for guests
 *      until tick-grid calibration completes.
 *      Taskman + PortPushFrame counters resync at barrier release.
 *   3) Runtime transport (`syNetPeerUpdate`): after execution is ready, emits INPUT payloads + recv pump.
 *
 * Game code usually calls `syNetPeerUpdateBattleGate` early inside VS scenes (recv + barrier + delay apply),
 * then `syNetPeerUpdate` only when execution is unlocked so rollback/input ordering stays coherent.
 *
 * **Recv cadence vs sim cadence:** `syNetPeerPumpIngressTransport` drains UDP independently of whether the
 * sim advances (e.g. `PortPushFrame` when `SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM` skips a tick, or optional
 * stall-path extra pumps in `syNetInputFuncRead`). `syNetPeerUpdateBattleGate` still owns delay-sync apply,
 * barrier, and bind transport after that recv step.
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
/* When SSB64_NETPLAY_BOOTSTRAP_INGRESS_SYMMETRY is set (Linux UDP), require outbound INPUT + inbound hr before first sim publish (see docs/netplay_timebase_authority.md). */
extern sb32 syNetPeerBootstrapIngressSymmetrySatisfied(void);
/* Alias semantic: identical to execution-ready for legacy callsites verifying barrier release semantics. */
extern sb32 syNetPeerCheckStartBarrierReleased(void);
/*
 * Hard lockstep clock gate (running VS): TRUE only when wall-clock schedule authorizes executing `sim_tick`.
 * Returns TRUE outside active VS / non-running phases so menus and non-netplay scenes are unaffected.
 */
extern sb32 syNetPeerIsClockReadyForSimTick(u32 sim_tick);
/*
 * Ingress + barrier driver invoked from VS scenes before input publish.
 * Receives queued packets, emits BATTLE_READY, advances clock-aligned barrier bookkeeping, retries INPUT_BIND.
 */
extern void syNetPeerUpdateBattleGate(void);
/*
 * For **active** Linux UDP VS sessions, `syNetInputFuncRead` calls `syNetPeerUpdateBattleGate()` first (recv + delay apply + barrier + bind); this entry point remains when VS is **inactive** but a session still needs UDP drained: **`syNetPeerPumpIngressTransport`** then **`syNetPeerApplyPendingDelayContract`** (Linux UDP; no-op elsewhere).
 */
extern void syNetPeerPumpIngressBeforeInputRead(void);
/*
 * Recv-only ingress: drain UDP (`recvfrom` until empty) and dispatch packets into remote staging / rings.
 * Does not apply delay-sync pending contract, barrier, or bind transport — use `syNetPeerUpdateBattleGate` / FuncRead for that.
 * Safe to call from `PortPushFrame` when decoupled sim skips a scheduler tick so ingress cadence stays ahead of sim stalls.
 * `caller_tag` labels `SSB64_NETPLAY_INGRESS_DIAG` lines (e.g. `port_push`, `funcread`, `inactive_pre_read`, `stall_extra`); NULL uses tag `pump`.
 */
extern void syNetPeerPumpIngressTransport(const char *caller_tag);
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
extern void syNetPeerTrySendRollbackBaselineDigest(void);
extern void syNetPeerTrySendRollbackSyncNotice(void);
extern void syNetPeerSendLocalInput(void);
/* Best-effort notify before local rollback fatal teardown so peer stops without strict-input stall. */
extern void syNetPeerSendVsSessionEndNotifyPeer(void);
/* Committed VS input delay (wire tick = sim tick + delay for GatherHistoryBundle / staged INPUT). */
extern u32 syNetPeerGetCommittedInputDelay(void);
/* Alias used by strict-contract callsites: authoritative committed delay D. */
extern u32 syNetPeerGetInputDelay(void);
/* Authoritative wire index: `sim_tick + committed_delay` (saturating add). Inverse clamps to 0. */
extern u32 syNetPeerDelayWireTickFromSim(u32 sim_tick);
/*
 * Receive-side lookup wire index for strict admission / remote ring reads.
 * Phase-locked VS keeps this as the same pure `sim_tick + D` mapping used by senders.
 */
extern u32 syNetPeerDelayWireLookupTickFromSim(u32 sim_tick);
extern u32 syNetPeerDelaySimTickFromWire(u32 wire_tick);
/* `sim_tick + D` with saturating add (base strict frontier; no extra slack). */
extern u32 syNetPeerGetBaseRequiredWireTick(u32 sim_tick);
/* `sim_tick + D + strict_extra_slack` with saturating add (strict-only frontier). */
extern u32 syNetPeerGetStrictRequiredWireTick(u32 sim_tick);
#ifdef PORT
typedef struct SYNetPeerSharedCommitStep
{
	sb32 advance;
	sb32 uses_prediction;
	char hold_reason; /* P / E / R */
	u32 sim_tick;
	u32 required_wire;
	u32 shared_confirmed_sim;
	u32 prediction_window;
	u32 commit_gen;

} SYNetPeerSharedCommitStep;

extern void syNetPeerEvaluateSharedCommitStep(u32 sim_tick, SYNetPeerSharedCommitStep *out);
extern void syNetPeerNoteSharedCommitAdvanced(u32 completed_sim_tick);
extern u32 syNetPeerGetGlobalCommitGen(void);
extern u32 syNetPeerGetPhaseLockPredictionWindowTicks(void);
/* Env-only phase-lock window (ignores auto-negotiated session params). */
extern u32 syNetPeerGetPhaseLockPredictionWindowTicksFromEnv(void);
extern u32 syNetPeerGetInputDelayCeil(void);
extern sb32 syNetPeerSessionParamsNegotiationSatisfied(void);
extern void syNetPeerApplyAutoNegotiatedDelayContract(u32 delay, u32 delay_ceil, const char *tag);
extern void syNetPeerApplyAutoNegotiatedSkewLeadMax(u32 lead_max_ticks);
extern void syNetPeerApplyAutoNegotiatedTransportParams(u32 phase_lock_ticks, u32 bundle_redundancy,
                                                        u32 ingress_extra_pumps, u32 strict_ring_fuzz_ticks);
extern u32 syNetPeerGetSessionIngressExtraPumps(void);
/* Phase-locked effective wire row: exact `sim_tick + D`; `hr` no longer reinterprets placement. */
extern u32 syNetPeerGetEffectiveWireFrontierForAdmission(u32 sim_tick);
/*
 * Match-linked delay only: buffer slack B (0 = off). When `hr > sim+D`, require `(hr - (sim+D)) >= B`;
 * when `hr <= sim+D`, B adds no bar (lockstep-safe). Cached per VS.
 */
extern u32 syNetPeerGetMatchInputBufferMinSlackTicks(void);
/* Per-match: clears lazy cache for `SSB64_NETPLAY_STRICT_RING_FUZZ_TICKS` (`syNetInputRefreshCachedNetplayEnvForNewMatch`). */
extern void syNetPeerResetStrictRingFuzzEnvCacheForNewMatch(void);
#endif
/* TRUE after startup bind/exec-sync latch has been reached for this VS session. */
extern sb32 syNetPeerHasBothSidesLatchedStartup(void);
/*
 * full_aux_checks TRUE: full tier-2 admission (match buffer B, STRICT_REMOTE_LEAD_BUFFER_TICKS hr folding).
 * FALSE: tier-1 lite: ring cells at the effective wire frontier only (same required_wire row; no B, no lead-B hr bar).
 */
extern sb32 syNetPeerIsRemoteInputReadyForSimTickEx(u32 sim_tick, sb32 full_aux_checks);
/* Wrapper: same as `syNetPeerIsRemoteInputReadyForSimTickEx(sim_tick, TRUE)`. */
extern sb32 syNetPeerIsRemoteInputReadyForSimTick(u32 sim_tick);
#ifdef PORT
/*
 * Applies host ramp + guest INPUT_DELAY_SYNC pending in one place (after `ReceiveRemoteInput`).
 * See `syNetPeerPumpIngressBeforeInputRead` / `syNetPeerUpdateBattleGate`.
 */
extern void syNetPeerApplyPendingDelayContract(void);
/* `~(u32)0` until first execution-begin for this session; used by delay-sync diagnostics. */
extern u32 syNetPeerGetDelaySyncDiagExecReadySimTick(void);
/*
 * Opt-in (`SSB64_NETPLAY_DELAY_SYNC_STARVATION_HANDLER`): when match-linked delay is active, latch sustained
 * `hr < required_wire` underrun and return TRUE so netinput can pause full publish (admission `V`) until the buffer
 * refills. `required_wire` must match the effective admission frontier (`syNetPeerGetEffectiveWireFrontierForAdmission`),
 * not the strict cap alone. Updates internal hysteresis counters; call once per strict FuncRead after computing
 * `required_wire`/`hr`.
 */
extern sb32 syNetPeerMatchDelayStarvationUpdateAndShouldHold(u32 sim_tick, u32 required_wire, u32 hr);
#endif
#ifdef PORT
/* Suppress sim advance when local tick leads confirmed remote ingress (wired into shared commit + tick commit). */
extern sb32 syNetPeerShouldHoldSimTickForSkewPacing(u32 tick, s32 *out_skew);
extern u32 syNetPeerGetSkewPacingHoldFrameCount(void);
/*
 * When tick-grid exec gate is enabled (SSB64_NETPLAY_TICK_GRID_EXEC_GATE=1) and the grid is locked, PortPushFrame
 * skips decouple deadline holds for sim-tick indexing (Linux UDP).
 */
extern sb32 syNetPeerShouldBypassDecoupleSimPacingForTickGrid(void);
#endif

#if defined(PORT)
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

#if defined(PORT) && defined(SSB64_NETMENU)

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
/*
 * Tear down socket + automatch bootstrap transport after failure or before another peer candidate.
 * Does not clear `syNetPeerConfigureUdpForAutomatch` session/bind settings.
 */
extern void syNetPeerCancelAutomatchBootstrap(void);
/* Brief cooperative pause between LAN/reflexive bootstrap attempts (frame pump + audio). */
extern void syNetPeerPauseBetweenBootstrapAttempts(void);
/* After bootstrap success in staging: arm and poll a synchronized scene-go rendezvous. */
extern sb32 syNetPeerBeginStageSceneRendezvous(void);
extern sb32 syNetPeerUpdateStageSceneRendezvous(void);

#endif

#endif /* _SYNETPEER_H_ */
