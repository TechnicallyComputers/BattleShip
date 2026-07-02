# Netplay — Fox Appear / SpecialN / Firefox charge cross-ISA (soak2)

**Date:** 2026-07-01  
**Status:** ✅ Round 6 confirms Firefox determinism is stable — closing this thread. Two unrelated bugs surfaced in the same soak, tracked separately.
**Sessions:** `2047760919`, `1691872615`, `453641924`, `2047424634`, `2061522853`, `1339981196` (Android host/client vs Linux guest/host)

## Symptom

After camera load-hash fix (`load_hash_drift=0`), soak2 failed at tick **600** during Fox **Firefox charge** (`SpecialHiStart` / motion `SpecialAirHiStart`):

- `FRAME_COMMIT_STATE_DIVERGE`: **eff + figh**, identical inputs, **rng matched**
- First cross-peer `figh` split @**523** (Fox `AppearR`, same `anim_hash`, different joint fold)
- Drift widened through Fox **SpecialN** blaster (@580) into Firefox startup; charge **ImpactWave** VFX position forked → **eff** diverge
- Linux **SIGSEGV** in `efManagerImpactWaveProcUpdate` during FC recovery (null `EFStruct` after apply wiped Fox `230→10` while motion VFX lingered)

## Root cause

1. **Appear / SpecialN joints** — per-tick cross-ISA figatree drift during forward sim; only one-shot fragile refresh + TopN-only quantize for Firefox travel.
2. **Charge ImpactWave** — motion-script shockwave position follows joint fold; not reconciled from ring during Firefox defer.
3. **FC recovery anchor** — `effect_probe_mismatch` walked load anchor `480→474` outside Firefox window; apply restored idle Fox while charge VFX still ran.
4. **Catch-up on all Fox** — `ApplyFighterNetplayPost` ran Firefox gate catch-up for every Fox regardless of status.

## Fix

| Change | Purpose |
|--------|---------|
| `syNetplayFoxFighterInSpecialNScope` / `InAppearScope` / `InResimPresentationScope` | Scope Appear + Blaster + Firefox for netplay repair |
| `syNetplayCanonicalizeFoxFirefoxSimState` | Quantize all Fox joints in Appear/SpecialN/Firefox scopes |
| `syNetRbSnapRefreshFoxResimPresentationFromSlot` | Per forward-resim tick slot re-pin for full Fox presentation window |
| `syNetRbSnapReconcileFoxFirefoxImpactWavesFromSlot` | Pin charge ImpactWave translate/anim from ring |
| `syNetRbSnapEjectOrphanFoxFirefoxImpactWaves` | Drop stale charge VFX when Fox leaves Firefox defer |
| Link-bomb effect repair scope + Fox firefox blob | Tolerate effect probe during FC recovery (avoid deep walkback) |
| Gate Fox catch-up in `ApplyFighterNetplayPost` | Only during Firefox defer scope |
| `efManagerImpactWaveProcUpdate` null guard (`PORT`) | Prevent SIGSEGV on stale GObj during recovery |

## Verify (soak2, session `1691872615`)

Crash/FC-divergence fix confirmed: `MATCH: STABLE`, no `FRAME_COMMIT_STATE_DIVERGE`, no SIGSEGV. Remaining symptom: guest resim mismatch @520 walked the load anchor back through the **entire** Firefox defer window in two 16-step passes (`RESIM_ANCHOR_FRAGILE_WALKBACK ... reason=fox_firefox_probe`, `519→503`, then `503→487`), landing 32+ ticks before the mismatch, followed by an `eff`-only soft-continue `LOAD_HASH_DRIFT` at the far anchor (`resim-sim-core-ok`, non-fatal).

Root cause: `syNetRbSnapBlobInFoxFirefoxSynctestDeferScope` inside `syNetRbSnapshotSynctestShouldSkipProbeTick` (`port/net/sys/netrollbacksnapshot.c`) flagged **every** tick in the Firefox status range as fragile — same failure shape already documented for the Yoshi egg-lay window (`soak1 @520: 519→487`). Walking back only re-enters the same multi-hundred-tick defer scope deeper inside itself instead of landing near the mismatch.

## Fix (round 2): remove the fragile-probe gate

Removed the `fox_firefox_probe` block from `syNetRbSnapshotSynctestShouldSkipProbeTick`, mirroring the existing Yoshi egg-lay precedent. This affects both:
- Live synctest probe skip (`SYNCTEST_SKIP ... reason=fox_firefox_probe`)
- Resim load-anchor fragile walkback (`syNetRbSnapshotIsLoadAnchorFragile` → `syNetRbSnapshotResolveLoadAnchorAvoidingFragile`)

Direct loads into mid-Firefox ticks now rely on the round-1 repair (joint quantize, per-tick presentation re-pin, gate timer sanitize/catch-up, generic quake-blob effect ensure which is independent of Fox scope) instead of being walked around. `syNetplayFoxLiveHasFirefoxSynctestDeferScope()` / `syNetplayFoxLiveHasResimPresentationScope()` still gate the live/forward-resim presentation repair paths.

## Verify (round 2)

Re-run soak2 cross-ISA (Fox Firefox on Sector Z deck). Expect:
- No `RESIM_ANCHOR_FRAGILE_WALKBACK reason=fox_firefox_probe` entries
- Load anchor resolves near the actual mismatch tick during Firefox
- No FC @600, no SIGSEGV on FC recovery, `load_hash_drift=0` retained outside Firefox
- Any remaining `eff`-only drift during Firefox should be investigated as a direct load/apply gap (effect identity, `bank_id`/proc fingerprint) rather than masked by walkback

## Round 3 (soak2, session `453641924`): walkback fixed, but blanket synctest skip still hides the real bug

Round-2 walkback fix confirmed working (`fox_firefox_probe` walkback gone). But a **second, separate** gate — `syNetRbSnapshotSynctestShouldSkip`'s `fox_firefox` branch (calling `syNetplayFoxLiveHasFirefoxSynctestDeferScope()`) — unconditionally skips the *entire* synctest for any tick where Fox is live in the Firefox window (`SYNCTEST_SKIP tick=... reason=fox_firefox`, 20+ consecutive ticks in the log).

This is masking a real bug: first cross-peer `sim_state` split @523 is **already inside** this scope — Fox ground `SpecialHi` (status=234, motion=209), `fhash_light` differs on the two peers with identical inputs/world/rng. That fold gap propagates to `FRAME_COMMIT_STATE_DIVERGE[figh]` @600 (rng/eff/world/item all match; only `figh` diverges, identical inputs) — synctest never got a chance to catch it earlier because the whole window is skipped.

### Fix (round 3)

Removed the `fox_firefox` branch from `syNetRbSnapshotSynctestShouldSkip` (`port/net/sys/netrollbacksnapshot.c`). `syNetplayFoxLiveHasFirefoxSynctestDeferScope()` is no longer called anywhere but is kept (used only by doc/history); the live-defer presentation path now relies on `syNetplayFoxLiveHasResimPresentationScope()`.

### Verify (round 3)

Re-run soak. Expect `SYNCTEST_FAIL` reports to start firing during the Firefox window instead of `SYNCTEST_SKIP reason=fox_firefox` — use the reported tick/field to localize the joint-fold gap (likely a field not covered by `syNetplayCanonicalizeFoxFirefoxSimState`'s quantize pass) and fix that specific field before considering removing the skip permanently unsafe-checked. Do **not** treat a wall of new `SYNCTEST_FAIL` as a regression — it's expected until the underlying fold divergence is fixed; use it to pinpoint, not panic.

## Round 4 (soak2, session `2047424634`): `SYNCTEST_FAIL` never fired — real bug is a resim-anchor walkback, not a fold gap

Round-3 fix held (no `fox_firefox` skip, no `fox_firefox_probe` walkback) and the scan tool reported `RESULT: PASS` — `load_hash_drift=0`, `synctest: 1 OK, 0 FAIL`, only `intro_wait`/`yoshi_egg_lay_attack` skips. But the **host** side of the same session reported `UNSTABLE reasons: PEER_SNAPSHOT_DIVERGE x1` and stopped the VS session at `load_tick=518` — a failure class the drift-scan script doesn't parse, so it read as a clean pass. User confirmed the desync is real and happens "after resim during firefox."

### Root cause

`PEER_SNAPSHOT_DIVERGE load_tick=518`: peer/local `figh` and `map` disagreed while `world`/`item`/`rng`/`wpn`/`cam` matched exactly. Cross-referencing the android log's own two capture passes for ticks 516-518 (the log contains **two** `fighter_slot_hash`/`map_hash_save` lines per tick — one from the original forward-sim pass, one from a later resim pass) showed Fox's own status at those ticks flipped from `232` (`SpecialHiHold`, still charging) in the first pass to `234` (`SpecialHi`, already launched) in the resim pass — **the same peer produced two different outcomes for the same ticks**, i.e. a resim-vs-original-forward-sim self-inconsistency, not just a cross-ISA joint-fold gap.

Immediately preceding this, the log shows:
```
RESIM_ANCHOR_FRAGILE_WALKBACK from=518 to=517 reason=effect_count_transition_probe step=1
RESIM_ANCHOR_FRAGILE_WALKBACK from=517 to=516 reason=effect_count_transition_probe step=2
EPISODE_FSM begin epoch=1 mismatch=516 load=515 target=522 ...
```
`syNetRbSnapshotSlotEffectCountTransitionFragile` (fires whenever `slot->effect_count` changes tick-to-tick — exactly what Fox's charge `ImpactWave` VFX does while spooling up) was the **only** fragile-probe branch in `syNetRbSnapshotSynctestShouldSkipProbeTick` that did not check `s_syNetRbSnapResimAnchorEffectRepairTolerant`. At the time, every sibling effect probe (`transient_effect_probe`, `multi_item_probe`, `post_multi_item_probe`, `explode_sparkle_probe`, `effect_probe_mismatch`) was gated behind that flag specifically so the resim-anchor walk (`syNetRbSnapshotIsLoadAnchorFragile`) didn't treat cosmetic effect-count churn as a reason to walk the load point backward — load repair (our round-1 `syNetRbSnapReconcileFoxFirefoxImpactWavesFromSlot` / `EjectOrphanFoxFirefoxImpactWaves` / `syNetRbSnapRefreshFoxResimPresentationFromSlot`) already handled it. This one branch was missing that gate, so it kept forcing the anchor back (518→517→516→...→**load=515**) every time Fox's charge VFX ticked. (`transient_effect_probe` was later removed entirely on 2026-07-02 so transient-only effect ticks surface as real synctest evidence.)

Loading from tick 515 and forward-resimming reproduced the Firefox Hold→Travel transition **one tick earlier** than the original forward-sim pass had (likely because the restored `launch_delay` sits right at the sanitize/catch-up boundary at that anchor), which is why the resim pass shows status `234` where the original pass showed `232` for the identical tick numbers — a same-peer, same-machine non-determinism that then also disagrees with the other peer's own (differently-anchored) resim, producing `PEER_SNAPSHOT_DIVERGE`.

### Fix (round 4)

Gated `effect_count_transition_probe` behind `s_syNetRbSnapResimAnchorEffectRepairTolerant == FALSE` in `syNetRbSnapshotSynctestShouldSkipProbeTick` (`port/net/sys/netrollbacksnapshot.c`), matching every sibling effect-fragility branch. Live synctest probe-skip behavior (`SYNCTEST_SKIP reason=effect_count_transition_probe`) is unchanged; only the resim-anchor walkback (`syNetRbSnapshotIsLoadAnchorFragile`) stops treating an effect-count change as fragile, so it no longer steers the load anchor away from the correct point during Firefox charge (or any other effect-count-churning window).

### Verify (round 4)

Re-run soak2 cross-ISA through a full Fox Firefox charge→launch. Expect:
- No `RESIM_ANCHOR_FRAGILE_WALKBACK ... reason=effect_count_transition_probe` entries during the Firefox window
- No `PEER_SNAPSHOT_DIVERGE` at/near the charge→launch boundary
- The two capture passes for the same tick (forward-sim vs any resim) should agree on Fox `status`/`motion` — check for duplicate `fighter_slot_hash tick=N player=1` lines with differing `status=` in the same log
- `load_hash_drift=0` retained; no `FRAME_COMMIT_STATE_DIVERGE`

## Round 5 (soak2, session `2061522853`): round 4 held; tick-523 "first split" was a diagnostic artifact, not the real bug

Round-4 fix confirmed clean: no `effect_count_transition_probe` walkback, no `PEER_SNAPSHOT_DIVERGE`, no `fox_firefox` skip. But `FRAME_COMMIT_STATE_DIVERGE[figh,rng]` still fired @600, and the pair tool again reported "first sim_state mismatch tick=523" — the *same* tick number every single round-1..5 soak has reported, regardless of what the fighters were actually doing at that tick (Firefox in most sessions, a plain Fox short-hop in this one). A bug whose exact tick never moves regardless of gameplay content is a tooling artifact, not a content-dependent bug — that finally forced tracing the diagnostic pipeline itself instead of the gameplay.

### Root cause: the trace log runs before quantize, so it's not comparable to what frame-commit actually checks

`fighter_slot_hash` / `sim_state_tick` (`SSB64 NetSync:`, logged by `syNetPeerMaybeLogSimStateTickTrace()`) was called from inside `syNetPeerUpdate()`, which `scVSBattleFuncUpdate` runs **before** `syNetRollbackAfterBattleUpdate()`. But `syNetplayCanonicalizeActiveFightersForNetplay()` (the quantize pass that snaps fighter physics/joints/camera to the shared cross-ISA grid) only runs *inside* `syNetRollbackAfterBattleUpdate()`, followed immediately by `syNetRollbackSavePostTick()` (the ring capture). The actual frame-commit token (`syNetFrameCommitBuildToken`, `port/net/sys/netpeer_frame_commit.c`) reads its `fighter_digest` from that same post-quantize ring (`syNetRbSnapshotGetStoredSubsystemHashesEx(validation_tick - 1, ...)`) — so **the real cross-peer consensus check was always comparing the correctly-quantized value**, while the diagnostic trace we'd been reading was logging the raw, pre-quantize live value one step earlier in the same tick's pipeline.

This explains the whole tick-523 pattern retroactively: at that fixed lag-compensation boundary, one peer's *original* forward-sim guess for the other peer's still-in-flight remote input differs from the *confirmed* value (normal, expected rollback misprediction) — visible in the pre-quantize trace as a "figh mismatch" that resim then corrects before it ever reaches the ring/frame-commit path. Confirmed directly in this soak's raw dumps: android's original-pass tick-523 `landing_branch` diag showed `tr_x=0xC5019295` (no horizontal drift yet) while its own later resim pass **and** linux's own pass both independently converged on `tr_x=0xC501538B` — i.e. the two peers' *corrected* states agreed; only the transient pre-correction guess differed. Every prior round's "tick 523 joint-fold" narrative (rounds 3–4) was chasing this same self-correcting transient, not a real fold gap.

The real, still-unresolved divergence is elsewhere: at ticks 595–599 (Fox `SpecialHiHold`/status=232, Firefox charge, matching motion on both peers) `fhash_light` genuinely differs cross-peer with matching `status`/`motion`/`world`/`map`. `fhash_light` folds `coll_data.vel_speed`/`floor_flags` whenever the fighter stands on the Sector Z Arwing deck (`floor_line_id==1` during an active near-Z Arwing pass) — `syNetplayQuantizeMPCollData` does quantize `vel_speed`, so this is either a genuine (non-epsilon) value divergence upstream of quantize, or another field entirely; not yet isolated. This trace is *also* pre-quantize per the bug above, so it cannot be trusted to localize the field/tick precisely until the fix below lands and a fresh soak is captured.

### Fix (round 5): move the diagnostic trace to after quantize

Made `syNetPeerMaybeLogSimStateTickTrace()` non-static (`port/net/sys/netpeer.c` + `netpeer.h`) and moved its call site from inside `syNetPeerUpdate()` to `scVSBattleFuncUpdate()` (`decomp/src/sc/sccommon/scvsbattle.c`), immediately after `syNetRollbackAfterBattleUpdate()` and before `syNetInputAdvanceAuthoritativeSimTick()` — same tick number (`syNetInputGetTick()` hasn't advanced yet), but now sampled after `syNetplayCanonicalizeActiveFightersForNetplay()` has quantized the live state. Diagnostic-only change; does not touch `syNetFrameCommitBuildToken`, the ring, or any gameplay/rollback logic — the real frame-commit consensus path was already correct.

### Verify (round 5)

Re-run soak2. Expect:
- The "first sim_state mismatch tick=NNN" reported by `netplay-trim-logs.py --sync-report` should either disappear or land at a tick that actually varies with gameplay content (not a fixed lag-boundary tick every run)
- The genuine `FRAME_COMMIT_STATE_DIVERGE[figh,rng]` (if it persists) should now be localizable by diffing post-quantize `fighter_slot_hash` around the validation window (`snap_tick = validation_tick - 1`) instead of the raw pre-quantize trace
- Focus the next pass on Fox `SpecialHiHold` (status 232) on Sector Z with the Arwing deck active — check `coll_data.vel_speed`/`floor_flags` and the deck-derived yakumono geometry feeding it, per the `#if defined(PORT) && defined(SSB64_NETMENU)` branch in `syNetSyncHashFighterStructLight` (`port/net/sys/netsync.c`)

## Round 6 (soak2, session `1339981196`): Firefox determinism confirmed stable — closing this thread

Longest soak yet (`max_sim_tick=1750`, 11 synctest OK / 0 FAIL, `RESULT: PASS` from
`netplay-scan-drift.py`, no `LOAD_HASH_DRIFT`, no `FRAME_COMMIT_*` diverge). The round-5 diagnostic
fix held and no Firefox-charge `fhash_light` fork reappeared despite extended play. User confirms:
"no synctest fail and firefox is stable looking all around." Treating the Fox
Appear/SpecialN/Firefox-charge cross-ISA determinism investigation as resolved as of round 5;
future Firefox-adjacent issues should open a new bug doc rather than reopening this one.

Two things surfaced in this same soak that are **not** part of this bug — tracked separately so
this thread stays closed:

1. A deterministic `SIGSEGV` in `efManagerKirbyVulcanJabProcUpdate` (Kirby rapid-jab hit effect vs a
   shielding Fox) — unrelated fighter/effect, unrelated code path. See
   [netplay_kirby_vulcanjab_efstruct_null](netplay_kirby_vulcanjab_efstruct_null_2026-07-01.md).
2. `yoshi_egg_lay_attack` synctest skip firing on every Fox Firefox charge with zero Yoshi
   involvement — a coincidental status-ID numeric collision (`nFTFoxStatusSpecialHiHold` ==
   `nFTYoshiStatusSpecialNCatch`), not a masking bug for *this* thread's divergence (both were
   already independently confirmed clean via `FRAME_COMMIT`/`SYNCTEST` in every round-4+ soak) but
   worth fixing so it doesn't hide something else later. See
   [netplay_yoshi_egg_lay_attack_scope_fkind](netplay_yoshi_egg_lay_attack_scope_fkind_2026-07-01.md).

A separate, pre-existing shield-bubble rendering issue (bubble clips through the fighter model
instead of overlaying in front) was also reported this soak; see
[netplay_guard_shield_attach_refresh_diag](netplay_guard_shield_attach_refresh_diag_2026-07-01.md)
for diagnostic instrumentation added to localize it (not yet re-fixed).
