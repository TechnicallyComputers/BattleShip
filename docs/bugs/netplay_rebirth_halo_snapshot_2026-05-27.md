# Netplay rebirth halo snapshot / stuck respawn platform — 2026-05-27

**Status:** Fix shipped (soak pending); prune/restore regression patched 2026-05-27  
**Scope:** [`port/net/sys/netrollbacksnapshot.c`](../../port/net/sys/netrollbacksnapshot.c), [`port/net/sys/netsync.c`](../../port/net/sys/netsync.c)

## Symptom

After cross-ISA quantize enabled, a stock loss can leave the **rebirth halo** (hex respawn platform) stuck on the fighter: spawn at stage center clipped into floor, then the platform mesh follows the character indefinitely.

## Root cause

1. Rebirth halo uses `proc_update == gcPlayAnimAll` ([`efManagerRebirthHaloMakeEffect`](../../decomp/src/ef/efmanager.c)), same proc as Ness PK wave.
2. [`syNetRbSnapEffectRespawnKindFromLive`](../../port/net/sys/netrollbacksnapshot.c) classified only Ness + `is_effect_attach` as `NESS_PK_WAVE`; rebirth halo got **`RESPAWN_NONE`** — no respawn on snapshot load if the GObj was missing.
3. Fighter-attached effects listed in the snapshot are not ejected during reconcile; stale halo rows kept the GObj alive after rebirth ended.

## Fix

| Change | Purpose |
|--------|---------|
| `SYNETRB_EFFECT_RESPAWN_REBIRTH_HALO` | `efManagerRebirthHaloMakeEffect` on load |
| Rebirth scope helpers + joint coupling fingerprint | Classify halo before Ness PK; tighten Ness PK to SpecialHi hold/start |
| `syNetRbSnapEnsureRebirthHaloEffectsFromSlot` | Mirror Fox reflector ensure during load |
| `syNetRbSnapPruneStaleRebirthHalos` | Eject halos when fighter left rebirth states |
| `syNetSyncHashActiveEffectsForRollback` | Fold `halo_despawn_wait` / rebirth status into `eff` hash |

## Regression (2026-05-27 soak): no respawn platform after second KO

**Symptom:** After the initial halo snapshot fix, a second stock loss could skip the rebirth platform entirely (fighter never gets the platform-down animation).

**Cause:** `syNetRbSnapPruneStaleRebirthHalos` only exempted fighters in `RebirthDown`..`RebirthWait`. During Catch/death overlap, `halo_despawn_wait` can still be active while status is Catch (166). Periodic synctest emergency restore re-applied the snapshot through `ApplySlotToLive`, prune ejected the halo, and the next sim tick spawned stock without the platform GObj.

**Follow-up fix:**

| Change | Purpose |
|--------|---------|
| `syNetRbSnapFighterRebirthHaloLifecycleActive` | Protect halos while `halo_despawn_wait` / `halo_lower_wait` > 0, not only rebirth statuses |
| Slot-aware prune + blob pending checks | Skip prune when snapshot lists `REBIRTH_HALO` for the fighter |
| Relaxed `FinalizeFighterEffectAttachFlags` | Keep `is_effect_attach` when blob/slot still expect a rebirth halo |
| `syNetSyncHashActiveEffectsForRollback` | Detect rebirth halo by joint coupling + lifecycle; omit float DObj position from hash (cross-ISA symmetry) |

## Soak / bisect

Use [`scripts/netplay-ko-lifecycle-soak.env.example`](../../scripts/netplay-ko-lifecycle-soak.env.example) on **both** peers.

Trim paired logs:

```bash
./scripts/netplay-trim-logs.py --label host /path/host.log --label guest /path/guest.log \
  -o rebirth-ko-trimmed.log --tick-min 100 --tick-max 250 --diff-ticks
```

**Pass criteria:**

- Halo visible during `RebirthDown`–`RebirthWait`; gone after transition to Fall.
- `effect_count` returns to 0 after rebirth window; no permanent `effect_count=1` while fighting.
- Optional rollback: `effect_respawn kind=REBIRTH_HALO` in log; host/guest `eff=` match post-respawn.

**Bisect:** `SSB64_NETPLAY_SIM_F32_QUANTIZE=0` on one peer only if floor clip persists after halo lifecycle fix (separate quantize-on-rebirth-Y issue).

## Center spawn / floor clip (2026-05-27)

**Symptom:** Rebirth halo lifecycles correctly, but the fighter body spawns at stage center clipped into the floor; jumping restores normal collision.

**Cause:** `ftManagerInitFighter` floor projection can snap `map_bound_top` onto the stage surface when `floor_dist > -300`. `ftCommonRebirthDownSetStatus` then captures that floor height into `rebirth.pos`, so `ftCommonRebirthCommonProcMap` never lifts the fighter onto the descending platform. Snapshot load also omitted ghost/rebirth flags and root/joint pose resync.

**Fix:**

| Change | Purpose |
|--------|---------|
| `ftCommonRebirthDownSetStatus` (`#ifdef PORT`) | Restore rebirth apex X/Y/Z after `InitFighter` before `rebirth.pos` capture |
| `syNetRbSnapRefreshRebirthFighterPose` | On rollback load, recompute root Y from rebirth state, restore ghost coll floor, repair stale low `rebirth.pos.y` |
| `syNetRbSnapQuantizeFighterRebirthStatusVars` | Quantize `rebirth.pos` / `halo_offset` in fighter blob for cross-ISA symmetry |

## Soak results

| Pairing | Result | Notes |
|---------|--------|-------|
| Linux host + Android guest | **Manual required** | KO / respawn soak |
| Linux ↔ Linux | **Manual required** | Regression |

## Wrong-fighter center snap after peer KO (2026-05-28)

**Symptom:** After Mario loses a stock, Fox (unaffected fighter) repeatedly snaps to stage center on snapshot load / synctest; `fighter_field_diff` shows Fox `top_joint_y live=0x00000000` while blob has correct stage Y.

**Cause:** `syNetRbSnapFighterRebirthHaloLifecycleActive` treated any non-zero `status_vars.common.rebirth.halo_*_wait` as rebirth-active. `status_vars` is a per-fkind union — on Fox in `Wait`, those bytes are Fox state, not rebirth timers. `syNetRbSnapRefreshRebirthFighterPose` then overwrote Fox root/joint pose with spawn-platform offsets (often 0). Same loose timer read in `syNetRbSnapQuantizeFighterRebirthStatusVars` corrupted non-rebirth blobs.

**Fix:**

| Change | Purpose |
|--------|---------|
| Lifecycle = `InRebirthScope` only (`is_rebirth` or `RebirthDown`..`RebirthWait`) | Stop aliasing Fox/Mario union bytes as rebirth timers |
| Gate quantize + pose refresh on `InRebirthScope` | Never run rebirth repair on normal fighters |
| Rebirth halo coupling = joint fingerprint only | No lifecycle fallback in classify/hash |
| Blob halo pending = rebirth status range only | No timer read from blob `status_vars` |
| Mirror in `netsync.c` | Cross-ISA `eff` hash symmetry |

**Build verification (2026-05-27):** `cmake --build build --target ssb64 -j 4` — success.

**Log trimmer smoke test:** `scripts/netplay-trim-logs.py` on `ssb64-debug.log` + `client-auto.log` with `--tick-min 130 --tick-max 150 --dedupe-effect-save --diff-ticks` produced summary + 638 kept lines (effect_count transition at tick 134).

## Faithful respawn snapshot restore (2026-05-28)

**Symptom:** After first rebirth, fighters snap to stage center/floor on snapshot load; second KO can leave Mario stuck in `DeadDown` with no visible respawn while 2P controls remain fine (no sync drift).

**Cause:** The fighter blob captured joint translates and rebirth `status_vars`, but **not** the fighter GObj root `translate`/`rotate` or respawn control bitfields (`is_invisible`, `is_ghost`, `is_rebirth`, `is_shadow_hide`, `is_menu_ignore`, `is_playertag_hide`, `is_limit_map_bounds`, `is_ignore_dead`). None of these are in `fhash_light`, so stale live values persisted across load without tripping sync. Load-time `syNetRbSnapRefreshRebirthFighterPose` synthesized pose from a parabolic formula and zeroed TopN joints when the formula matched the blob — producing `top_joint_y live=0` while blob had correct stage Y.

**Fix:**

| Change | Purpose |
|--------|---------|
| `gobj_translate` / `gobj_rotate` + control-bit `u8` fields in `SYNetRbSnapFighterBlob` | Faithful capture/restore of rebirth pose and visibility state |
| Remove `syNetRbSnapRefreshRebirthFighterPose` / pose synthesis on load | Trust blob; vanilla `ftCommonRebirthCommonProcMap` continues from restored state |
| `syNetRbSnapQuantizeFighterRebirthStatusVars` gated on `InRebirthScope` only | Avoid float-quantizing `dead.wait` via rebirth union aliasing |
| Extended `fighter_field_diff` for dead/rebirth statuses | Log `gobj_translate`, control bits, `stock_count`, `hitlag_tics`, `dead_wait` |
| Optional `SSB64_NETPLAY_HASH_GOBJ_TRANSLATE=1` | Bisect gobj translate round-trip in `figh` full hash (default off) |

**KO soak pass criteria (both peers, cross-ISA):**

- Respawned fighter visible after `RebirthWait` (`is_invisible`/`is_ghost` match blob on load).
- No center/floor snap: `fighter_field_diff` shows `gobj_translate_y` live==blob at first `RebirthDown` synctest.
- Repeated KOs (Mario 2nd death, Fox death) all respawn with halo (`effect_count=2` during rebirth window).
- Trim: `./scripts/netplay-trim-logs.py ... --tick-min 1320 --tick-max 3200`

## One-time Y snap during rebirth descent (2026-05-28)

**Symptom:** After quantize-aware rebirth canonicalization fixed main respawn, a **single** visible Y jump backward then forward could still occur during platform descent when `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`.

**Cause:** Periodic synctest captures live at tick T, loads probe tick T−1, runs presentation sync via `FinalizeLoad`, then **always** restores live emergency state. Rebirth `halo_lower_wait` differs by one tick between T and T−1, so the emergency restore briefly rewinds descent pose.

**Fix:**

| Change | Purpose |
|--------|---------|
| `syNetRbSnapshotSynctestShouldSkip` + `reason=rebirth` | Defer synctest while any fighter is in rebirth scope (`is_rebirth` or `RebirthDown`..`RebirthWait`); `nextProbe = completed_tick + 1` like other skips |

**Verify:** During rebirth window, log shows `SYNCTEST_SKIP tick=… reason=rebirth` and no visible snap. `SSB64_NETPLAY_ROLLBACK_SYNCTEST=0` should also eliminate the snap (bisect only).

## Rebirth gate logging + elimination cleanup (2026-05-28)

**Symptom:** Inconsistent respawn (e.g. Mario rebirth OK, Fox second KO never enters rebirth scope); eliminated fighters can still pull the camera off-map when snapshot load leaves `DeadDown` + `stock_count=-1` without completing vanilla `Sleep`.

**Fix:**

| Change | Purpose |
|--------|---------|
| `SSB64_NETPLAY_REBIRTH_GATE_DIAG=1` | Log `dead_init`, `dead_wait_zero`, `check_rebirth` branch, `rebirth_down` at vanilla choke points |
| `InRebirthScope` = rebirth **status range only** (drop `is_rebirth` alone) | Prevent quantize of rebirth union floats while fighter is in `DeadDown` (corrupts `dead.wait`) |
| `syNetRbSnapApplyFighterNetplayPost` | Sync `gSCManagerBattleState` stock; sanitize `is_rebirth`; force `ftCommonSleepSetStatus` when eliminated but still dead; Ghost presentation for `Sleep`/eliminated; rebind dead/rebirth procs after apply |
| `syNetplayRebirthApplyEliminationPresentation` | Mirror vanilla Sleep flags + `nFTCameraModeGhost` — **no position snap** |

**Verify:** Every stock KO with stocks remaining logs `check_rebirth branch=rebirth` then `rebirth_down`. Last-stock KO logs `branch=sleep`. Camera ignores eliminated fighter (Ghost). Trim with `REBIRTH_GATE` + `--tick-min`/`--tick-max` around KO ticks.

## Dead countdown synctest skip (2026-05-28)

**Symptom:** Third/fourth KO leaves fighter stuck in `DeadDown` (`status=0`); `REBIRTH_GATE` shows `dead_init` only — never `dead_wait_zero` / `check_rebirth`. Both peers agree on `dead_wait` values; failure is deterministic.

**Cause:** Vanilla `ftCommonDeadCommonProcUpdate` only calls `ftCommonDeadCheckRebirth` when `dead.wait == 0`. Periodic synctest emergency restore during the 45-tick dead countdown can resim past the zero gate (e.g. `dead_wait=3` → synctest at T−1 → next tick `dead_wait=-1`), so the gate never fires.

**Fix:**

| Change | Purpose |
|--------|---------|
| `syNetRbSnapshotSynctestShouldSkip` + `reason=dead` | Defer synctest while any fighter is in `DeadDown`..`DeadUpFall` (same range as dead proc rebind) |
| `syNetplayRebirthCatchUpDeadGateIfDue` in `syNetRbSnapApplyFighterNetplayPost` | After snapshot apply + proc rebind, if `dead.wait <= 0`, call `ftCommonDeadCheckRebirth` (safety net for missed zero after restore/resim) |

**Verify:** No `dead_wait` jump from positive to negative across synctest during dead window. KOs 3+ log `dead_wait_zero` → `check_rebirth` → `rebirth_down` or `branch=sleep`. Trim ticks 2705–2720 and 3305–3320 with `--diff-ticks`.

## Dual / simultaneous multi-rebirth (4p-ready) — 2026-05-28

**Symptom:** When two (or more) fighters lose stocks close together, rollback load during the rebirth window can drift on `figh` + `eff`: `gobj_translate_y live=0` vs blob spawn Y, halo respawn disagreement, latent drift resurfacing later in unrelated states (e.g. Firefox).

**Cause:**

1. `syNetRbSnapshotFinalizeLoad` presentation + joint reapply clobbered rebirth root pose before `LOAD_HASH` verify; rebirth halos lacked the second ensure/rebind pass that Fox reflector already had in finalize.
2. `netsync.c` `InRebirthScope` still treated `is_rebirth` alone as in-scope while snapshot used status range only — `eff` hash skew during multi-KO overlap.
3. No rebirth lifecycle catch-up after snapshot apply (missed `RebirthDown→Stand→Wait→Fall` gates after restore/resim, same class as dead `wait==0` bug).

Vanilla already assigns distinct `halo_number` (0–3) and X offsets via `dFTCommonRebirthOffsetsX[4]` when multiple fighters enter rebirth; snapshot slots are indexed by `fp->player` (`GMCOMMON_PLAYERS_MAX`).

**Fix:**

| Change | Purpose |
|--------|---------|
| `syNetRbSnapRestoreRebirthFightersAfterFinalize` | Slot-indexed 0..3: re-apply `gobj_translate` + TopN joint + `syNetplayCanonicalizeRebirthFighterMapPose` after presentation finalize |
| Second halo ensure/prune + per-slot effect rebind in finalize | Mirror Fox reflector finalize path for all fighters in rebirth scope |
| `syNetplayRebirthCatchUpLifecycleIfDue` | Per-fighter catch-up for stand/wait/fall rebirth timer gates after snapshot apply |
| `syNetSyncFighterInRebirthScope` = status range only | Match snapshot / quantize gates; drop standalone `is_rebirth` |
| `multi_rebirth_summary` / `halo_number` field diff | Diagnose 2–4 simultaneous rebirth windows |
| Rebirth halo ensure diag (`rebirth_halo_ensure`) | Per-player `halo_number`, blob vs synth respawn path |

**Verify (dual-KO soak, both peers, cross-ISA):**

- `effect_count` in 2..4 during overlapping rebirth; `multi_rebirth_summary rebirth_count>=2`.
- No `LOAD_HASH_DRIFT` with `figh`/`eff` split during rebirth defer window (`SYNCTEST_SKIP reason=rebirth`).
- `fighter_field_diff`: `gobj_translate_y` live==blob for each rebirth player at first load drift tick.
- Trim ticks **2000–2180** and **2740–2890** from dual-KO session logs.
- **4p lab (future):** `SSB64_NETPLAY_REMOTE_SLOTS=1,2,3` on host; same per-slot pass criteria (no matchmaking work in this fix).

## Second rebirth floats up from below (overlapping rebirth) — 2026-06-01

**Symptom:** When two fighters lose stocks close together, the first respawns correctly (descends from map top); the second floats **up** from below the platform instead.

**Cause:** Vanilla `ftCommonRebirthCommonProcMap` inverts descent when `rebirth.pos.y <= halo_offset.y` (apex at/below platform). Snapshot load/finalize could leave live `rebirth.pos.y = 0` while the blob retained `map_bound_top` (`fighter_field_diff rebirth_pos_y live=0 blob=0x46147000`). `syNetRbSnapRestoreRebirthFightersAfterFinalize` only re-applied gobj/joint pose, not rebirth `status_vars`.

**Fix:**

| Change | Purpose |
|--------|---------|
| `syNetplayRestoreRebirthStatusVars` | Copy rebirth union from blob on apply + after finalize |
| `syNetplayRepairRebirthApexIfInverted` | Force `rebirth.pos.y = map_bound_top` when apex ≤ platform (VS session) |
| `syNetplayCanonicalizeRebirthFighterMapPose` | Run apex repair on VS session even when F32 quantize is off; re-derive root Y |
| `syNetRbSnapRestoreRebirthFightersAfterFinalize` | Restore full rebirth `status_vars` before pose canonicalize |
| `syNetRbSnapApplyFighter` post-catch-up | Re-restore rebirth vars from blob after lifecycle catch-up |

**Verify:** Dual-KO soak ticks **1690–1800**; both fighters descend from top; no `rebirth_pos_y live=0` in `fighter_field_diff` during rebirth loads.
