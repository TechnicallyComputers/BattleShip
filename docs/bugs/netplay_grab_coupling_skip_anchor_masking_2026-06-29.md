# Netplay: grab-coupling probe-skip masks (and anchors away from) a dropped grab on rollback

**Date:** 2026-06-29
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `decomp/src/mp/mpprocess.c`, `decomp/src/mp/mpcommon.c`.
**Status:** SKIP REMOVED PERMANENTLY + ROOT CAUSE FIXED (2026-06-29 update). The grab-coupling synctest skip, probe-skip, release-boundary probe, and the load-anchor fragility they drove are deleted, and the underlying cargo-carry landing fork is fixed at its source: an `is_coll_end` latch that skipped the airborne collision loop (see "Uninitialized `result` UB" below). No `FRAME_COMMIT_STATE_DIVERGE` in the latest soak; one unrelated single-peer `SYNCTEST_FAIL` remains to be diagnosed from a paired capture.
**Class:** rollback-correctness regression formerly hidden by a synctest probe-skip + load-anchor fragility heuristic (not a cross-peer desync).

## 2026-06-29 update — skip removed, gap localized

The A/B switch (`SSB64_NETPLAY_DISABLE_GRAB_COUPLING_SKIP`) and all three skip sites have been **removed permanently** (no env gate left). Deleted: `syNetRbSnapGrabCouplingSkipDisabled`, the live `grab_coupling` skip in `syNetRbSnapshotSynctestShouldSkip`, the `grab_coupling_probe` + `grab_coupling_release_boundary_probe` in `syNetRbSnapshotSynctestShouldSkipProbeTick`, the `syNetRbSnapshotSynctestProbeGrabThrowReleaseBoundaryFragile` helper, and the `syNetRbSnapBlobInGrabThrowSynctestFragileScope` blob predicate. `syNetRbSnapshotIsLoadAnchorFragile` reuses the probe predicate, so removing the probe entries also stops biasing the resim anchor away from grab ticks. Kept: live coupling maintenance (`syNetRbSnapshotRefreshGrabCouplingGeometry`, `syNetRbSnapRebindFighterGrabCoupling`, `syNetRbSnapScrubFighterGrabCouplingState`), `syNetRbSnapshotAnyFighterGrabCouplingActive` (used by `netrollback.c`), and `syNetRbSnapFighterInGrabThrowSynctestFragileScope` (other call sites).

With the skip gone, the exposed DK cargo-carry failure (`soak2`, first `FRAME_COMMIT_STATE_DIVERGE` tick 600, `SYNCTEST_FAIL` tick 637) localizes to a **forward-sim vs resim landing-timing fork**, NOT a capture-fidelity hole:

- At tick 637 the snapshot blob vs live diff is **`status_id` (blob 241 / live 242) and `motion_id` (blob 216 / live 217) ONLY** — every other captured scalar, coupling id, and coll_data field round-trips. So the slot faithfully stored what the forward sim produced; the forward sim simply left DK stuck in `ThrowFFall` (241, frozen in place from tick 569) while the resim lands him to `ThrowFLanding` (242).
- At the fork tick 569 **every dumped per-fighter field is bit-identical cross-peer** (position, velocity, all `coll_data`, `vel_speed`/`vel_push`/`line_coll_dist`/`floor_angle`, `update_tic`, throwf vars, catch/capture coupling) yet committed `status` forks (Android 241, Linux 242).
- The `landing_branch` probe shows the floor verdict `vv0` is identical cross-peer (and across forward/resim). So `mpProcessCheckTestFloorCollisionAdjNew`'s core compare is deterministic. The remaining unlogged inputs to its `return TRUE` gate (line ~2081) are `floor_flags & MAP_VERTEX_COLL_PASS` and `floor_line_id != ignore_line_id`, captured at the landing-check instant (not end-of-tick). The probe was extended to log `fflags`, `ignore`, `mask_unk`, and the computed `gated` verdict to settle this on the next soak.

### Witness-stomp false positive (fixed)

The ~390 `NetStatusVars: witness stomp ... accessed=throwf` lines per peer were an artifact of the `fighter_cargo_diag2` dump calling `ftStatusVarsThrowF(fp)` on every dumped fighter (the accessor calls `ftStatusVarsNoteAccess(..ThrowF)`, tripping the witness for any fighter not in a ThrowF status). The dump now reads `&fp->status_vars.common.throwf` directly. These were not real union stomps.

### Uninitialized `result` UB in `mpProcessUpdateMain` — fix + correction

`mpProcessUpdateMain` (`decomp/src/mp/mpprocess.c`, 0x800DA034) returns `result` uninitialized when its collision loop runs zero times — i.e. when `coll_data->is_coll_end` is already TRUE at entry. Deterministic stack garbage on N64, but cross-ISA-divergent on LP64, which is the real cross-ISA seed: the old desync was Linux reading garbage=TRUE (DK lands) vs Android garbage=FALSE (DK stuck).

**First attempt (wrong):** initialized to `FALSE`. This removed the desync but regressed gameplay — DK now deterministically *never* lands during a cargo carry. Soak confirmed `is_coll_end` is stuck TRUE for the whole carry (1330 ticks, `status=241`/`ga=1`, pinned to the floor with `mask_curr/stat=0x0800`), so the landing check's loop is always skipped and `mpCommonCheckFighterLanding` always returned the hardcoded FALSE → DK frozen in `ThrowFFall`, treated as airborne (no walk, fast-fall spark on down), recovering only on throw. Symptom: every per-peer DK `status=242` count = 0.

**Second attempt (also wrong):** initialize from floor contact —
`sb32 result = (coll_data->mask_stat & MAP_FLAG_FLOOR) ? TRUE : FALSE;`
This restored landing but returned **stale** `mask_stat` whenever the loop was skipped. Landing onto ground read FLOOR (correct, lands); walking off a ledge also read FLOOR (stale — DK just left it) → "still on floor" → DK could not walk off a ledge while carrying (only jump off, because `ftDonkeyThrowFFallProcMap` routes high upward velocity to `ThrowFWait` regardless of the bogus landing verdict). Same defect, opposite sign: it guessed a return value instead of letting the collision run.

**Root-cause fix (shipped):** break the `is_coll_end` latch at its source and let the loop run.

`is_coll_end` is only reset to FALSE by `mpCommonRunFighterAllCollisions` (grounded floor-found path, mpcommon.c ~line 219) or `mpCommonCopyCollDataStats` (captive slaving). The airborne landing check (`mpCommonRunFighterSpecialCollisions`) has no such reset, so a fighter that goes airborne with `is_coll_end` already TRUE skips `mpProcessUpdateMain`'s loop every tick — and the loop body (`proc_coll`) is the only code that can clear the flag. The flag self-latches for the whole airborne span and **no real floor test runs**. DK's cargo carry hits this because walking off a ledge sets `is_coll_end` TRUE (no-floor path) and then enters `ThrowFFall` airborne carrying that TRUE forward.

Fix: `mpCommonSetFighterAir` now clears `fp->coll_data.is_coll_end = FALSE` on every ground→air transition (the canonical chokepoint — `ftDonkeyThrowFFallSetStatus` and `ftCommonFallSetStatus` both route through it). A freshly-airborne fighter has no in-progress collision to keep "ended", so FALSE is the correct deterministic start. The loop now runs each airborne tick and `proc_coll` produces a real per-tick floor verdict — fixing both the stuck landing and the ledge walk-off. No-op for normal play (grounded ticks already leave `is_coll_end` FALSE via line 219).

With the latch fixed, `mpProcessUpdateMain`'s init reverts to a plain deterministic `sb32 result = FALSE;` (a genuinely skipped pass processed no new collision — the honest answer; no UB, no cross-ISA fork). Built clean (`build-netmenu` + `build-offline`).

### Load-path sim-advance in `syNetRbSnapshotRefreshGrabCouplingGeometry` (the exposed SYNCTEST_FAIL)

After the latch fix, gameplay was correct (DK lands, walks off ledges) but a **paired** soak showed 6 `SYNCTEST_FAIL` (629, 869, 989, 1109, 1229, 1349), each at a DK cargo-carry landing, plus a transient "stuck on landing, recovers after a few seconds" hitch — i.e. rollback churn around the landing tick, no `FRAME_COMMIT_STATE_DIVERGE`.

Root cause: the snapshot **finalize/verify** path (`syNetRbSnapshotFinalizeLoadFromSlot` -> `syNetRbSnapshotRefreshGrabCouplingGeometry`) ran the **grabber's gameplay map proc** (`grabber_fp->proc_map(grabber_gobj)`) on a freshly-loaded slot. The status trail proves it: `A_apply_status_restore` loads DK at 241, and `D_finalize_end` reports 242 — the finalize itself executed DK's `ftDonkeyThrowFFallProcMap` -> `mpCommonCheckFighterLanding`, landed him, and flipped status/motion/ga/position. The synctest verify then sees the loaded slot mutate (LOAD `status_id` live=242/blob=241, `motion_id` 217/216, anim hash drift) -> `SYNCTEST_FAIL`. In a real rollback load it lands the grabber one tick early vs the authoritative resim — a desync seed.

This was latent: with the `is_coll_end` latch, the landing check inside that proc_map was a no-op, so finalize never transitioned. The `mpCommonSetFighterAir` latch fix exposed it.

Fix: `syNetRbSnapshotRefreshGrabCouplingGeometry` no longer calls the grabber's `proc_map` (or the no-proc_map `mpCommonRunFighterCollisionDefault` fallback). It keeps only the **geometry** refresh (`ftParamInvalidateFighterTransformFromRoot` + `ftParamsUpdateFighterPartsTransformAll`) needed to re-anchor the captive; the grabber's gameplay state is already restored faithfully from the slot blob, so a restore must not run a gameplay collision pass. Same class as the yakumono "anim advanced during restore" bug. Built clean (`build-netmenu`). Re-soak pending.

## Symptom

`soak2` cross-ISA pair (android host / linux guest, current build), Peach's Castle (`AUTOMATCH_STAGE_KIND=0`), DK (slot 0) vs Link (slot 1), `FORCE_MISMATCH=1 INJECT_TICK=520`:

- The match **no longer crashes** at the grab — runs to a clean `vs_stop` (1418 frames). The effect-pool guard + 06-27 grab-coupling-by-player fix removed the deterministic crash.
- `netplay-trim-logs.py --sync-report` → `STABLE (soft recovery)`: `resim=1`, `fc_rng_div=0`, `fc_item_div=0`, `sigsegv=0`, only the benign tick-389 `cam` `LOAD_HASH_DRIFT`.
- **But the grab visibly fails to replay.** Link's grab connected in the live timeline (`SYNCTEST_SKIP reason=grab_coupling` runs continuously over ticks **510–520** pre-rollback and **523–531** post-rollback — the live coupling check was active). After the rollback the grab does not re-form on replay: `capture=1` appears **nowhere** in either log; at the only sampled post-resim tick DK is `status=10 capture=0` (free) while Link is `status=166 catch=1` (reaching). A landed grab became a whiff, and nothing flagged it.

## Root cause of the masking (two mechanisms, one predicate)

The FORCE_MISMATCH rollback anchored `load_tick=487` (Link `status=154`, pre-catch), `mismatch=488`, `target=522` — i.e. **before** the grab connected (~510), spanning it. Two behaviors driven by the same grab-coupling predicate (`syNetRbSnapBlobInGrabThrowSynctestFragileScope`: `catch_gobj_id`/`capture_gobj_id != 0`, or status in Catch…ThrowB / captured / thrown ranges) conspire to hide the result:

1. **Verification blackout.** `syNetRbSnapshotSynctestShouldSkip` (live `grab_coupling`) and `syNetRbSnapshotSynctestShouldSkipProbeTick` (`grab_coupling_probe`, `grab_coupling_release_boundary_probe`) skip the synctest save→restore→re-hash on every grab tick. So an imperfect grab replay is never checked → run reads `STABLE`.
2. **Anchor steering.** `syNetRbSnapshotIsLoadAnchorFragile` **reuses** `ShouldSkipProbeTick` (checks `load_tick` and `load_tick+1`), and `syNetRbSnapshotResolveLoadAnchorAvoidingFragile` walks the resim load anchor backward while fragile. Grab ticks are therefore avoided as anchors, biasing the resim to start before the grab — exactly "it resimmed before Link did the grab move."

The skips are a **workaround** for the real gap: grab/catch coupling (two fighters aliased by shared `gobj_id`; crashes fixed 06-27, full round-trip fidelity not) cannot yet survive a snapshot save→restore. The skips guarantee you never *see* it fail. The whiff is the unfaithful restore surfacing whenever a rollback window crosses the grab anyway.

## A/B switch shipped

`SSB64_NETPLAY_DISABLE_GRAB_COUPLING_SKIP=1` (new, default OFF, netmenu-only) force-disables all three grab-coupling skip sites via `syNetRbSnapGrabCouplingSkipDisabled()`:

- live `grab_coupling` skip in `syNetRbSnapshotSynctestShouldSkip`,
- `grab_coupling_probe` + `grab_coupling_release_boundary_probe` in `syNetRbSnapshotSynctestShouldSkipProbeTick`.

Because `IsLoadAnchorFragile` reuses the probe predicate, disabling it simultaneously (a) lets the synctest run on grab ticks and (b) lets the resim anchor sit inside the grab. Logs a one-shot banner when enabled. Default OFF = byte-for-byte current behavior. Built clean (`build-netmenu`); `build-offline` unaffected (never compiles this TU; helper is `#if defined(PORT) && defined(SSB64_NETMENU)`, else `FALSE`).

## Soak procedure / expected outcomes

Re-run the Castle Link-grab-DK pair with `SSB64_NETPLAY_DISABLE_GRAB_COUPLING_SKIP=1` on both peers:

- **A `SYNCTEST_FAIL` / `fhash` divergence now fires on the grab ticks (510–531)** → proves the coupling doesn't round-trip and names the field/tick. This is the expected result and the concrete fix target (likely the catch/capture coupling re-formation or a per-status union field not restored).
- **Grab replays correctly with the anchor inside it** → the anchor-avoidance walkback was over-rewinding past a recoverable grab; the fix is to stop treating grab ticks as load-anchor-fragile.

## Audit hook

A rollback that reads `STABLE` but visibly changes a gameplay outcome (landed grab → whiff) on a window the synctest `*_probe`/live skip covers = a probe-skip masking the result, not a clean recovery. The same predicate that skips verification also steers the load anchor (`IsLoadAnchorFragile` → `ShouldSkipProbeTick`); use `SSB64_NETPLAY_DISABLE_GRAB_COUPLING_SKIP=1` to convert the silent drop into a located synctest failure before assuming recovery.

## Related

- [`netrollback_fighter_coupling_gobjid_ambiguity`](netrollback_fighter_coupling_gobjid_ambiguity_2026-06-27.md) — fixed the grab-coupling *crash* (resolve by player slot); this is the residual *fidelity* gap the skips still hide.
- [`netplay_grab_throw_release_eff_drift`](netplay_grab_throw_release_eff_drift_2026-06-27.md) — added `grab_coupling_release_boundary_probe`; same predicate now also feeds load-anchor fragility.
- [`netplay_castle_bumper_item_resim_diverge`](netplay_castle_bumper_item_resim_diverge_2026-06-28.md) — separate item-domain divergence seen in the same soak.
