# Netplay Link bomb item throw resim kinematics — 2026-07-05

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending — Follow-on 12 LBParticle enforce)

## Symptom

soak2 session `913743101` (Link P0 / Donkey P1, `FORCE_MISMATCH@520`): drift scan **PASS**
(no hash drift, synctest OK, resim completes), but **visible** gameplay failure — Link's bomb
**drops at his feet and falls through the floor** instead of arcing after resim lands mid
`LightThrowF` (`status=106`, `motion=92`) at load anchor tick **519**.

Both Android host and Linux guest show identical wrong replay (`MpLanding branch=diff` from tick
523, constant `tr_x=0xC38CEE13`, large negative `fdist`). Deterministic wrong fidelity, not
cross-peer desync.

### Follow-on (post throw-window fix)

Re-soak after throw-window repair: bomb **arcs correctly** on the post-resim throw (ticks
545–570), but **clips through the ground after landing** (571–604+). `MpLanding branch=diff`,
`fdist=0xC364A38A` (~−228.6) stays constant while translate moves. Root cause: hand-pose /
throw repair updated DObj translate and attack coll but **did not re-anchor item MPColl**
(`floor_dist`, `pos_prev`, `update_tic`). Blob apply at resim load tick 519 left stale coll on
the second bomb (`item_count=2`, fighter in `SpecialLw` not throw scope).

## Root cause

Item throw window load/replay gap in `port/net/sys/netrollbacksnapshot.c`:

1. **Hold blob translate clobber** — `syNetRbSnapReapplyLinkBombStatusAfterBlob` forced
   `blob->translate` onto the live bomb after figatree/presentation sync moved Link's hand joints,
   leaving the held bomb at a stale world pose when `ftCommonItemThrowProcUpdate` released it on
   resim ticks 520–522.

2. **Missing hand re-attach** — blob apply restored Hold procs/coupling but not the hold display
   wrapper (`item_gobj->obj`); without `itMainSetFighterHold` or a hand-joint pose refresh, item
   coll/translate drifted from the throw release point.

3. **Finalize blob regress** — `syNetRbSnapReconcileItemsToSlotBlobs` could re-apply a pre-release
   Hold slot blob over a live item already thrown during resim forward replay.

4. **Link bomb proc infer** — `syNetRbSnapInferLinkBombStatusFromBlob` consulted live `is_hold`
   after apply instead of the authoritative blob hold flag; stale `is_thrown` + `drop_update_wait`
   could bind `Dropped` procs in edge cases.

5. **Stale item MPColl after translate repair** — blob `syNetRbSnapApplyMPColl` restores
   `floor_dist` / `update_tic` from ring capture while live translate is refreshed by hand-pose
   or throw kinematics repair. Landing uses `update_tic != gMPCollisionUpdateTic` → Diff branch;
   stale `floor_dist` yields false air / clip-through after landing.

## Fix

`port/net/sys/netrollbacksnapshot.c` (`PORT && SSB64_NETMENU` where noted):

- `syNetRbSnapItemBlobReconcileWouldRegressThrow` — skip slot blob reapply when live item is
  already released but blob still says Hold.
- `syNetRbSnapInferLinkBombStatusFromBlob` — prefer blob `is_hold` flag for Hold proc selection.
- `syNetRbSnapReapplyLinkBombStatusAfterBlob` — in item throw scope, skip blob translate/vel
  overwrite; refresh hand pose via `syNetRbSnapRefreshHeldItemHandPoseFromFighter`.
- `syNetRbSnapRepairItemThrowWindowLive` — after load finalize and each resim forward tick while
  any fighter is in `[LightThrowStart..End]` / heavy throw: rebind hold coupling, re-Hold Link
  bomb procs, re-attach hold display or sync translate from hand joint, refresh thrown bomb coll.
- `syNetRbSnapRefreshItemMPCollFromTranslate` — re-anchor item `pos_prev` / `pos_diff` /
  `update_tic` from live DObj translate and run `mpCommonRunDefaultCollision` for non-held items;
  wired into hand-pose refresh, thrown bomb repair, blob reapply (Thrown/Fall/Wait/Dropped), and
  `syNetRbSnapRepairLinkBombAirborneMPCollLive` (airborne Link bombs with stale integration).
  **Load vs resim split:** integration re-anchor and floor probe run only during forward
  resim replay. Load verify must not call `mpCommonRunDefaultCollision` (translate drift @1469)
  or `itMainRefreshAttackColl` on Explode bombs (attack_state drift @1589/2549). Explode coll
  refresh: `syNetRbSnapRepairLinkBombExplodeAttackCollLive` during forward resim only; sparkles
  remain `ReplayExplodeSparklesFromRing` at slot apply.
- Hooks: `FinalizeLoadCouplingFromSlot` (post item reconcile), `ResyncLiveFightersFromSlotForSim`,
  `RefreshIntroPresentationAfterForwardResimTick`.

## Re-soak pass criteria

Same soak2 Link bomb + `FORCE_MISMATCH@520` scenario on both peers:

- Resim completes with matched hashes (unchanged).
- Bomb **arcs normally** after throw release (no feet-drop / floor fall-through).
- Bomb **rests on stage after landing** (no post-landing clip-through; ticks 571–604).
- No new `LOAD_HASH_DRIFT` / `SYNCTEST_FAIL` on ticks 516–525.

**Diag env:** `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1`, `SSB64_NETPLAY_LANDING_BRANCH_DIAG=1`.

### Follow-on (item hash drift @ tick 989, session 688269894)

**Symptom:** `LOAD_HASH_DRIFT` + `SYNCTEST_FAIL` at resim anchor tick **989** — item-only
(`slot=0x2FE9FB9B` vs `live=0x306F36A3`), both peers identical. Two Link bombs: Wait on
ground + Explode mid-window. All logged scalars (pos, vel, link_status, atk_state, lifetime)
matched live vs blob; per-item folds at verify were Wait `0xCF6401C6` + Explode `0xBEC90546`
→ aggregate `0x306F36A3`.

**Root cause:** `syNetRbSnapFillSlotFromLive` captured item blobs early (`CaptureItems`) but
computed `hash_item` after weapons/effects/backfill. That pipeline can mutate hashed fields
(`attack_records` victim ids/flags, `is_allow_pickup`, owner/damage gobj ids) without refreshing
blobs — slot hash reflected late live state while ring blobs stayed stale.

**Fix:** Re-run `syNetRbSnapCaptureItems(slot)` immediately before
`syNetSyncHashActiveItemsForRollback()` in `FillSlotFromLive` (mirrors map re-capture before
`hash_map`). Extended `item_blob_field_diff` to log attack_records and other hash-fold scalars
when `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1`.

### Follow-on 2 (dangling attack_record victim pointers, session 1854235190 ticks 869/989)

**Symptom:** Item-only `LOAD_HASH_DRIFT` + `SYNCTEST_FAIL` persisted after the re-capture fix.
New attack_record diagnostics named the field: `attack_rec[0] victim_id=0/1011` (live/blob) on
records with `atk_state=0` — a stale record on an **inactive** attack coll pointing at GObj kind
1011 (`nGCCommonKindEffect`).

**Root cause:** The decomp only clears `attack_records[].victim_gobj` on `timer_rehit` expiry
while the attack is active (`itProcessUpdateAttackRecords`), so a record can outlive its victim
GObj. Rollback effect churn recycles the freed GObj slot as an effect (shared id 1011) every
verify. Three readers disagreed on the dangling pointer:

- **Hash fold** (`syNetSyncFoldAttackRecordSlots`) read `victim_gobj->id` raw → folded the
  recycled/freed id (1011) into the slot hash at capture.
- **Blob capture** (`syNetRbSnapGobjId`) returned the recycled id if the memory was re-linked,
  or 0 if unlinked at capture instant.
- **Blob apply** (`syNetRbSnapResolveLiveGobj`) rejects GObjs without a ft/it/wp payload →
  restored `NULL` → verify folded 0.

Slot hash folds 1011, post-restore live folds 0 → deterministic drift on both peers. Tick 989
was the unlinked variant (blob stored 0, fold still read freed memory). The raw `->id` read was
also a use-after-free.

**Fix:** Sanitize victim identity to the restore-path criterion — currently linked in the
fighter/item/weapon GObj lists AND carrying a typed payload; otherwise 0 / NULL. Never
dereference an unlinked pointer (membership by pointer comparison only):

- `syNetSyncAttackRecordVictimIdForFold` (netsync.c) — used by the shared
  `syNetSyncFoldAttackRecordSlots` (fighter + item + weapon folds).
- `syNetRbSnapAttackVictimGobjIdForCapture` (netrollbacksnapshot.c) — used by fighter
  `CaptureAttackColl`, item/weapon blob meta capture, and the `item_blob_field_diff` logger
  (which previously dereferenced the dangling pointer itself).

Capture fold, blob id, and restore now agree on every dangling-victim state.

### Follow-on 3 (explode owner + LightThrowF verify pose, session 109552986 ticks 749/1349)

**Symptom:** After victim-pointer fix, 8× `SYNCTEST_OK` but item-only `LOAD_HASH_DRIFT` +
`SYNCTEST_FAIL` at ticks **749** and **1349** (both peers identical).

- **749:** Four Link bombs (Wait/Fall/Thrown/Explode). Explode step: all scalars match except
  `owner_id=1000/0` (live=Link fighter kind id, blob=0). Fighters/world/rng/anim match.
- **1349:** Link in `LightThrowF` (`status=110`). Held bomb (`link_status=2`, `is_hold=1`):
  `pos live=(657,320,-93)` vs `blob=(-69,341)` (hand vs stale ground translate);
  `atk_state live=2 / blob=0`.

**Root cause:**

1. **Explode owner rebind** — `syNetRbSnapResolveItemOwnerFromBlob` always resolved fighter-owned
   items (Link bomb) by `blob->player` even when `is_hold=0` and `owner_gobj_id=0`. Same for
   `syNetRbSnapReconcileFighterOwnedItemOwners`, which forced owner on every fighter-owned item
   regardless of hold state. Blob/hash captured `owner=0`; verify restore left `owner=1000`.

2. **Verify hand pose** — `PrepareLoadedSlotForVerify` runs a final `ReapplyJointAnimAtTick`
   after item reconcile, invalidating held-bomb translate. Forward resim uses
   `syNetRbSnapRepairItemThrowWindowLive`, but verify had no equivalent pass. Hand refresh during
   reconcile called `itMainRefreshAttackColl`, mutating hashed `attack_state` (live=2, blob=0).

**Fix:**

- `syNetRbSnapResolveItemOwnerFromBlob` — player-slot owner restore only when blob hold flag set;
  non-held with `owner_gobj_id=0` → NULL.
- `syNetRbSnapReconcileFighterOwnedItemOwners` — skip when `is_hold==FALSE`.
- Explode branch in `syNetRbSnapReapplyLinkBombStatusAfterBlob` — clear `owner_gobj` when blob
  says 0.
- `syNetRbSnapRefreshHeldItemHandPoseTranslateOnly` + `syNetRbSnapRepairItemThrowWindowForVerify`
  — pose-only hand sync (no attack coll refresh) after final joint anim on verify path; reconcile
  hold path uses translate-only when `s_syNetRbSnapRepairStageVerifyOnly`.

**Re-soak pass criteria:** session 109552986 seed — `SYNCTEST_OK` at ticks 749 and 1349; bomb arc
and landing fidelity unchanged.

### Follow-on 4 (capture-side throw-window hand pose, session 1853178978 tick 1829)

**Symptom:** Ticks 749–1709 all `SYNCTEST_OK` after Follow-on 3, then item-only drift at **1829**.
Link in `LightThrowF` (`status=106`, `motion=92`), 4 bombs. Held bomb step 3 (`instance_id=9`,
`link_status=2`, `is_hold=1`): all scalars match except translate — `live=(2057.72,405.97)` vs
`blob=(-69.79,341.64)`. Fighter/world/rng/anim match; both peers identical.

**Root cause:** Follow-on 3 added verify-side `RepairItemThrowWindowForVerify` (hand pose after joint
anim) but `FillSlotFromLive` still captured item blobs without refreshing held-bomb translate from
hand joints. Slot hash and blobs baked in stale translate `(-69,341)` while verify repair moved live
to the current hand joint `(2057,406)` (Link at `topn_x≈1997`). Same held-bomb translate family as
ticks 519/1349; manifests once Link has moved far from the stale anchor during a long throw window.

**Fix:** Call `syNetRbSnapRepairItemThrowWindowLive(FALSE)` in `FillSlotFromLive` immediately before
the second `CaptureItems` / `hash_item` when any fighter is in item throw scope.

### Follow-on 5 (capture Live repair regressed bomb physics, session 1186579825)

**Symptom:** 14/14 `SYNCTEST_OK`, zero hash drift — but visible bomb misbehavior returned: bombs drop
at Link's feet instead of arcing, fall through the floor (`MpLanding branch=diff` tunnels at gut
752–782, 915–985, …), and despawn off-stage. Both peers identical (deterministic wrong fidelity).

**Root cause:** Follow-on 4 used `syNetRbSnapRepairItemThrowWindowLive(FALSE)` in `FillSlotFromLive`
every tick while Link is in throw scope. That runs on **all** live items (held + thrown), calls
`syNetRbSnapReanchorItemMPCollIntegration` (re-stamps `update_tic` every capture tick → permanent
Diff landing branch + stale `floor_dist`), and `syNetRbSnapRepairReleasedLinkBombThrowKinematics`
(can mis-bind Thrown→Fall procs). Hash-aligned wrong physics.

**Fix:** Use `syNetRbSnapRepairItemThrowWindowForVerify()` in `FillSlotFromLive` instead — held-bomb
hand pose + translate only; no attack-coll refresh, no thrown-bomb proc repair, no MPColl re-anchor
on in-flight items.

### Follow-on 6 (read-only capture/hash projection, session 1186579825 fidelity)

**Symptom:** Synctest/hash pass but Link bombs non-vanilla: feet-drop, floor tunnel (`MpLanding
branch=diff`), despawn. Any `RepairItemThrowWindow*` in `FillSlotFromLive` mutates live sim every
tick during throw windows.

**Fix:** Separate hash/capture from live gameplay mutation:

- `syNetRbSnapTryComputeHeldItemHandPose` — read-only hand joint → `Vec3f` (quantize only).
- `CaptureItems` — blob `translate` from projection when held + throw scope (no live writes).
- `syNetRbSnapGetItemTranslateForHashFold` — netsync item hash fold uses same projection.
- **Removed** all `RepairItemThrowWindow*` from `FillSlotFromLive`.
- **Resim forward:** `RepairItemThrowWindowForVerify` (pose-only) instead of `Live(TRUE)`;
  `RepairLinkBombAirborneMPCollLive` / explode coll **once** on first resim tick (`probe=FALSE`).
- **Load finalize:** one-shot `RepairLinkBombAirborneMPCollLive(FALSE)` after item reconcile.

### Follow-on 7 (post-canonicalize MPColl sync, session 1312113353)

**Symptom:** 6/6 `SYNCTEST_OK` through 989 in session 1312113353, but bombs still tunnel. First
implementation (unconditional per-tick reanchor + forward explode coll sanitize) regressed in session
715611001: `SYNCTEST_FAIL` + item `LOAD_HASH_DRIFT` @1589 (LightThrowF, held+Wait bombs), tunnel
spam worsened (766 `branch=diff` lines), later `SIGSEGV` @1968 after desync cascade.

**Root cause (gameplay):** Item translate quantize without MPColl integration sync; stale
`floor_dist`/`pos_prev` from hold phase poisons throw release.

**Root cause (F7 regression):** Unconditional per-tick `ReanchorItemMPCollIntegration` on every
airborne bomb re-stamped `update_tic` without clearing stale `floor_dist` (tunnel spam worsened).
Forward per-tick `RepairLinkBombExplodeAttackCollLive` risked hashed `attack_coll` churn. Verify
hash at throw-window ticks needed pose refresh after reconcile/canonicalize.

**Fix (revised):**

- Resync only when quantize nudged translate **or** `pos_prev` desynced (non-held airborne Link
  bombs only); clear stale floor coll fields before re-anchor; **skip during load verify**.
- **Removed** forward per-tick explode coll sanitize (resim first-tick only, unchanged).
- `syNetRbSnapshotRepairItemThrowWindowForVerify` before item hash compare in verify path.

### Follow-on 8 (throw-release MPColl, session 707731396)

**Symptom:** 4/4 `SYNCTEST_OK`, 0 drift — frame-commit diverge @868 (`item`+`rng`, inputs
MATCH). Seed tick **829**: Link Squat vs Wait; by 867 Fall bomb `vel_air.y` forks (10.8 vs 4.8).
982 `branch=diff` lines.

**Fix:**

- Hold-state ring (`instance_id`+`gobj_id`): on hold→thrown transition after canonicalize, one-shot
  `syNetRbSnapRepairReleasedLinkBombMPCollOnly` (itMapSetAir + clear stale floor coll + MPColl
  re-anchor; no attack_coll refresh).
- `syNetRbSnapshotAfterSimLinkBombForwardRepair` — forward sim + resim replay only (not verify).
- Airborne resync also triggers when Thrown/Fall `vel_air` quantize nudges bits.

### Follow-on 9 (item sort vs hash fold translate, session 563454390 tick 1349)

**Symptom:** `SYNCTEST_FAIL` @1349 item-only (both peers identical). Link `LightThrowF`
(`status=106`): held bomb + Wait on field. All per-item blob fields match; aggregate item hash
differs (`slot_item=0x4DC3AEE2` vs `live_item=0xD0E3A888`).

**Root cause:** Follow-on 6 projected held-bomb translate via
`syNetRbSnapGetItemTranslateForHashFold` in hash fold, but
`syNetRbCompareItemGobjsForRollbackHash` still sorted by raw `dobj->translate`. Capture kept
stale held x → held sorted before Wait; verify `RepairItemThrowWindowForVerify` fixed dobj → Wait
sorted before Hold. Same per-item folds, swapped XOR order → different aggregate hash.

**Fix:** Sort comparator uses the same hash-fold translate source
(`syNetRbSnapGetItemTranslateForHashFold`, fallback raw dobj) so capture and verify enumerate
items in identical order.

### Follow-on 10 (throw-window MPColl fidelity, session 1497838271)

**Symptom:** 6/6 `SYNCTEST_OK`, 0 drift — residual bomb misbehavior. 268 `branch=diff`
lines (down from 821); 11 `gated=1` spurious landing frames @504/515/519/… First airborne
frame @478: stale hold `fdist=0xC3437D60`. Hash layer fixed; MPColl landing path still wrong vs
vanilla.

**Root cause:** F8 hold-track repair ran **after** sim (too late for release-tick collision).
Canonicalize resync skipped stale `floor_dist` when translate/pos_prev already matched.
Held-phase floor coll poisoned first Thrown map proc; persistent `pos_prev`/Diff-path landing
geometry caused `vv0=1 gated=1` feet-drop class symptoms. Both peers identical (not desync).

**Fix (Follow-on 10):**

- `syNetRbSnapshotHardenLinkBombAtThrowRelease` — end of `itMainSetFighterRelease` for Link
  bomb: clear stale floor coll + re-anchor before first thrown map proc same tick.
- `syNetRbSnapshotPreSimLinkBombAirborneMPCollHardening` — before `gcRunAll` (forward + resim):
  Thrown/Fall airborne bombs, no floor probe.
- Post-sim `syNetRbSnapshotAfterSimLinkBombForwardRepair` — same hardening each tick (replaces
  hold-track one-shot).
- Canonicalize resync also triggers on nonzero airborne `floor_dist` or stale `MAP_FLAG_FLOOR`.

### Follow-on 11 (unconditional per-tick MPColl hardening, session 998566450)

**Symptom:** 18/18 `SYNCTEST_OK`, 0 drift — residual bomb misbehavior (feet-drop, whiffed
explosion VFX). 1271 `MpLanding` lines each peer; 45 `gated=1` spurious landing accepts at
identical ticks. In-flight bombs show `fdist=0` frozen entire arc while `branch=diff` every tick
(vanilla in-flight bombs update `floor_dist` each tick).

**Root cause:** `syNetRbSnapLinkBombAirborneMPCollNeedsHardening` returned TRUE for every
Thrown/Fall airborne bomb, so `PreSim`/`AfterSim` ran full hardening twice per tick (clear floor
coll + reanchor `pos_prev` to translate + stamp `update_tic`). That destroyed the normal
in-flight integration delta (`pos_prev` must trail `translate` until map proc runs). Throw-release
hook also called full hardening, wiping `mpCommonRunItemCollisionDefault` floor seed at release.

**Fix:**

- `syNetRbSnapLinkBombAirborneFloorCollIntegrationStale` — gate on hold-poison floor coll only:
  `MAP_FLAG_FLOOR` while ga=air, or nonzero `floor_dist` with `update_tic != gMPCollisionUpdateTic`.
- `syNetRbSnapRefreshLinkBombAirborneFloorCollStale` — clear stale floor fields + quantize only
  (no `pos_prev` reanchor); used by `PreSim`/`AfterSim`.
- `syNetRbSnapshotHardenLinkBombAtThrowRelease` — quantize; clear floor coll only when stale;
  reanchor `pos_prev` only when hold left it desynced from release translate.
- Canonicalize resync unchanged (full harden when quantize nudges translate/vel or post-quantize
  `pos_prev` stale).

### Follow-on 12 (LBParticle sparkle enforce storm SIGSEGV, session 1500361127 tick 990)

**Symptom:** 5/5 `SYNCTEST_OK` then deterministic `SIGSEGV` (`pc=0x0`) on both peers at tick 990
during synctest emergency restore after Link bomb spam (6th bomb buggy VFX). Six `id=1011`
effect GObjs ejected via `gcEjectGObj` immediately after emergency apply. `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`
produced no `slot_effect_enforce` summary because the storm ran on `effect_count=0` without
per-GObj logging on enforce/orphan paths.

**Root cause:** Link bomb sparkle LBParticle shells are cosmetic (never in effect blobs) but were
not hidden when `anim_frame==0`. `RestoreLiveEmergency` ran `syNetRbSnapEnforceSlotAuthoritativeEffectSet`
on the emergency sentinel (`effect_count=0`), mass-ejecting live particle shells → corrupted
effect link → null proc call.

**Fix:**

- `syNetRbSnapLiveEffectIsLbParticleCosmeticShell` — hide LBParticle-coupled shells from rollback
  capture/enforce.
- Skip `syNetRbSnapEnforceSlotAuthoritativeEffectSet` on emergency restore when `effect_count==0`.
- `syNetRbSnapLogEffectEjectDiag` — per-GObj eject lines under `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG`
  on hidden-cosmetic, orphan-shell, enforce, and verify-non-canonical paths.

### Follow-on 13 (release-tick hold floor_dist poison, post-F12 soak)

**Symptom:** 16/16 `SYNCTEST_OK`, 0 crash — residual bomb misbehavior after several throws.
1197 `MpLanding` lines; 42 `gated=1` spurious landing accepts at identical ticks. First thrown
frame still shows hold-poison `fdist=0xC3437D60` (gut 838) despite F11 stale gating.

**Root cause:** F11 gated throw-release and per-tick refresh on
`syNetRbSnapLinkBombAirborneFloorCollIntegrationStale`, which required nonzero `floor_dist` **and**
`update_tic != gMPCollisionUpdateTic`. `mpCommonRunItemCollisionDefault` at throw release seeds
poisoned `floor_dist` with `update_tic` already stamped to `gMPCollisionUpdateTic`, so the gate
never fired. `syNetRbSnapLinkBombThrownFallAirborne` also required `ga == Air`, but
`itLinkBombThrownSetStatus` does not call `itMapSetAir` — held bombs stay `ga=Ground` through
release, skipping all PreSim/AfterSim refresh paths.

**Fix:**

- `syNetRbSnapLinkBombThrownFallAirborne` — Thrown/Fall proc identity only (drop `ga == Air` gate).
- `syNetRbSnapLinkBombAirborneFloorCollIntegrationStale` — any nonzero airborne `floor_dist` or
  `MAP_FLAG_FLOOR` (no update_tic lag requirement).
- `syNetRbSnapClearLinkBombAirborneFloorCollStale` — also reset `floor_line_id`/flags/angle.
- `syNetRbSnapshotHardenLinkBombAtThrowRelease` — always `itMapSetAir` + unconditional floor coll
  clear after release collision; pos_prev reanchor still gated on desync only.

### Follow-on 14 (verbatim MPColl contract — structural refactor)

**Symptom:** F13 throw-release clear helps most arcs but hold-poison at spawn+1, resim reload
re-poisons via blob apply + `RepairLinkBombAirborneMPCollLive` reanchor, and feet-drop persists
after bomb spam (`gated=1` with `fdist=0`).

**Root cause:** `SYNetRbSnapMPCollBlob` already stores full `MPCollData`, but capture wrote
`blob.translate` (projected hand pose) while `blob.coll.pos_prev` came from stale live hold
coupling; apply ran MPColl before translate, then canonicalize/repair hooks mutated coll again.
Item hash excludes MPColl — synctest clean while sim fidelity wrong.

**Fix (verbatim contract, `SSB64_NETPLAY_ITEM_MPCOLL_VERBATIM=1`; legacy `LINK_BOMB_*`):**

- Capture: held+throw scope sync `blob.coll.pos_prev = blob.translate`, zero `pos_diff`.
- Capture: thrown/airborne hold-poison stripped in blob only (`floor_dist`/`MAP_FLAG_FLOOR` clear).
- Apply: write translate then `syNetRbSnapApplyMPColl`; no post-apply item MPColl repair.
- Skip F8–F13 forward/load MPColl hooks when verbatim active; trust blob round-trip.
- Witness: `SSB64_NETPLAY_ITEM_MPCOLL_WITNESS=1` logs capture/apply integrity (all items).

### Follow-on 15 (all-item verbatim + blob throw-poison strip)

**Symptom:** Follow-on 14 pilot fixed hold blob coupling but verbatim disabled F13 throw-release
clear, so `C3437D60` hold-poison still serialized on Thrown frame 1; `gated=1` unchanged.

**Fix:** Generalize verbatim to all items (`SSB64_NETPLAY_ITEM_MPCOLL_VERBATIM=1`). At capture,
when `!is_hold && (is_thrown || ga==Air)` and blob coll still carries hold-phase floor poison,
clear stale floor fields in the blob only (no live mutation). Legacy `LINK_BOMB_*` env aliases kept.

### Follow-on 16 (pickup hash double-fold, soak2 tick 508)

**Symptom:** Paired-soak linux session: `item_count=2` at tick 508–509 during ground→held
pickup; `item_hash_walk` folds the same logical Link bomb twice (ground `hold=0` + coupled held
`hold=1`). Both log as `gobj_id=1013` because all items share `nGCCommonKindItem`. Tick 510
`FRAME_COMMIT_ITEM_PERSISTENT_DIVERGE` → reanchor to tick 470.

**Root cause:** Pickup can leave two live item GObjs for one bomb (ground ghost + fighter-coupled
held). Hash/capture enumeration walked both because `gobj_id` is not a per-instance key.

**Fix (Follow-on 16, revised):** Hash-only dedupe via `syNetRbDedupeActiveItemsForRollbackHash`
(called from `syNetSyncHashActiveItemsForRollback` / hash trace / field-diff — **not**
`syNetRbEnumerateActiveItemsSorted`):

(1) same nonzero `instance_id` → keep `is_hold`. Pass-2 (same-player held coupling without
`instance_id` match) removed in F17b — it ejected legitimate landed bombs during bomb spam.

**F16 regression (soak2 session 741017016):** Dedupe inside enumerate made capture/apply cardinality
match hash (1 blob) while live still had 2 GObjs → synctest verify ejected legitimate bombs
(`ejected=1` @509). Forward-sim `syNetRbSnapEjectLinkBombPickupGroundGhostsLive` at capture start
cleans pickup ghosts before ring save. LBParticle explode shells with `anim_frame > 0` no longer
verify-ejected when lbParticle back-pointer is stale (`obj_kind` is `GObjObjKind` DObj=1, not
`gobj->id`).

### Follow-on 17 (verbatim MPColl gameplay: damage / floor / sparkle obj_kind, soak2 seed 372284816)

**Symptom:** Post-F16 paired soak: 18/18 `SYNCTEST_OK`, bombs show explode VFX but (1) no damage
to fighters in range, (2) bomb mesh persists after detonation, (3) landed bombs fall through floor.

**Root cause:** With `SSB64_NETPLAY_ITEM_MPCOLL_VERBATIM=1`, forward-sim MPColl repair hooks were
fully disabled and `syNetRbSnapRepairLinkBombExplodeAttackCollLive` ran only once at resim intro.
Sparkle `hidden=0` ejects used `obj_kind == nGCCommonKindEffect` (1011) instead of
`nGCCommonAppendDObj` (1). `MpLanding gated=1` feet-drop cleared floor_line_id on landed bombs.

**Fix:**
- `syNetRbSnapshotAfterSimLinkBombForwardRepair`: verbatim path clears airborne hold-poison +
  reanchors; `syNetRbSnapRepairLinkBombGroundMPCollLive` re-probes floor when `floor_line_id < 0`;
  per-tick `syNetRbSnapRepairLinkBombExplodeAttackCollLive` for live Explode hitboxes.
- LBParticle cosmetic hidden: match `obj_kind == nGCCommonAppendDObj` on effect/special-effect links
  (`anim_frame > 0`), not `nGCCommonKindEffect`.

### Follow-on 17b (landed bomb vanish, soak2 seed 171179615)

**Symptom:** Post-F17 soak: bombs land on floor correctly but vanish shortly after. Not a
synctest/load blob mismatch (`SYNCTEST_OK`, ring capture shows ground bomb through tick 582).

**Root cause:** `syNetRbSnapEjectLinkBombPickupGroundGhostsLive` pass-2 ejected any ground idle
Link bomb (`!hold !thrown ga=Ground`) when the same player held a **different** bomb
(`instance_id` mismatch @583: ground `instance=2` ejected while held bomb unrelated). Legitimate
landed throws were classified as pickup ghosts during bomb spam.

**Fix:** Forward eject and hash dedupe now use pass-1 only — same nonzero `instance_id`, keep
`is_hold`. Removed same-player held-coupling pass-2 that ignored instance identity.

### Follow-on 18 (feet-drop throw with ground bombs present, soak2 seed 1485978527)

**Symptom:** Post-F17b soak: bombs stay on ground and behave normally, but the 3rd bomb throw
lands near Link's feet (zero horizontal arc) while throws 1/2/4 arc correctly. 3/3 `SYNCTEST_OK`,
0 eject events — not desync or vanish.

**Root cause:** With `item_count=3` (two ground bombs + held bomb), throw release at gut 633 kept
stale hold-poison translate `C38CEE13` (-281.9) instead of the normal release jump to
`C39E510F` (-316.6). `MpLanding branch=diff` used `fdist=0xC3CE3900` (vs `0xC3437D60` on good
throws), so the bomb dropped vertically (`peak_dy=0`, X frozen) and landed ~210 units from Link
instead of off-stage. Follow-on 14 disabled `syNetRbSnapshotHardenLinkBombAtThrowRelease` under
verbatim MPColl; without release-tick floor coll clear + `pos_prev` reanchor after
`mpCommonRunItemCollisionDefault`, hold-poison geometry persisted when multiple floor probes
competed.

**Fix:** Re-enable `syNetRbSnapshotHardenLinkBombAtThrowRelease` under verbatim MPColl (F13
release-tick clear still applies to live forward sim; blob round-trip unchanged).

### Follow-on 19 (re-pickup throw vanish, soak2 post-F18)

**Symptom:** Link neutral throws stable after F18, but a fighter picking up a landed bomb and
throwing it can look like the bomb vanishes right after release. Linux + Android soak logs
identical (deterministic).

**Soak findings (two distinct paths):**

1. **Non-Link forward throw (fkind=5 @ gut 1419):** Thrown bomb arcs normally from Y≈317 for
   25+ frames. Same tick `itMainDestroyItem` removes a **different** idle ground bomb at x≈−497
   (old fuse/lifetime — `upt=1413`, unrelated instance). Not the thrown bomb vanishing in sim.

2. **Re-pickup flat throw (Link @ gut 2527–2542):** Single bomb picked at gut 2507
   (`LightGet`), held through low-Y throw windup (guts 2518–2526), released at Y≈121 with a
   flat horizontal arc to x≈756, then map-bound `itMainDestroyItem` at gut 2542 (~15 frames).
   Peers agree — reads as instant vanish.

3. **Non-throw release during pickup (fkind=5 @ gut 2109):** After `LightGet` hold, status
   transition drops held bomb airborne at x≈1065 with `thrown=0` (not a LightThrowF release) —
   off-stage within one frame.

**Root cause (partial):** Held Link-bomb hand pose refresh + hold coupling ran only during
LightThrow/HeavyThrow scope, not during `LightGet`/`HeavyGet`/`LiftWait`/`LiftTurn`. Stale
pickup translate/MPColl can poison release geometry for re-picked bombs.

**Fix (Follow-on 19):** Add `syNetRbSnapFighterInItemPickupHoldScope` +
`syNetRbSnapFighterInItemHoldOrThrowScope` (`LightGet`..`LiftTurn` ∪ throw range). Use in live
throw-window repair, verify pose refresh, hash-fold hand pose, blob capture MPColl normalize, and
Hold blob apply hand-pose rebind.

### Follow-on 20 (explode SFX doubled/loud on fighter hit, soak2 post-F19)

**Symptom:** Gameplay/determinism stable, but bomb explode on a fighter sounds much louder —
like ExplodeL stacking (double or multi-hit).

**Root cause:** Follow-on 17 per-tick `syNetRbSnapRepairLinkBombExplodeAttackCollLive` called
`itMainRefreshAttackColl` after every forward-sim tick. That clears `attack_records` and sets
`attack_state = New`, re-arming the explode hitbox each frame. Overlapping fighters re-trigger
`ftMainUpdateDamageStatItem` → `func_800269C0(fgm_id)` (`nSYAudioFGMExplodeL`) on top of the
immediate explode sound in `itLinkBombExplodeInitVars`. Vanilla already plays ExplodeL twice on a
direct connect (init + hit); netplay repair stacked up to ~6 extra hit SFX per explode window.

**Fix:** Keep per-tick explode coll sync for F17 damage/floor parity, but call
`itProcessUpdateAttackPositions` only (re-enable with `attack_state = New` when Off). Do not clear
hit records or force New every tick.

### Follow-on 21 (re-pickup throw decoupling + phantom arc, soak2 post-F20)

**Symptom:** Synctest pass and explode audio fixed (F20), but late soak shows bad bomb physics when
picking up a landed bomb and throwing it back, plus a phantom bomb drop after Link's throw ends.

**Soak findings (tick 3880–3915, Linux + Android identical):**

1. **Decoupled re-pickup throw (@3882–3888):** Link goes Dash (status 15) → LightThrowF (52)
   without ever setting `hold=1` on the ground bomb at x≈−890 (`tr_x=0xC45E9365`). Witness/blob
   stay frozen at ground idle (`hold=0 thrown=0`, `upt=3882` stuck) while live `MpLanding`
   integrates a phantom arc at `tr_x=0xC48C8BAF`. Bomb `itMainDestroyItem` @3888; Link still in
   LightThrowF with continuing phantom arc through @3913.

2. **Phantom feet-drop (@3915):** `MpLanding gated=1 fdist=0xC1F4D1E8` on the decoupled arc after
   the item GObj is gone — reads as a new bomb dropping at Link's feet. Link then enters status 68
   (motion 59) with `item_count=0` (anim only, no sim item).

**Root cause:** `syNetRbSnapRepairItemThrowWindowLive` was never invoked on live forward sim (only
verify pose refresh). Re-pickup that skips LightGet in status_trail never runs
`itMainSetFighterHold`, so throw release / MPColl hardening gates (`is_hold`, `is_thrown`) never
fire. Verbatim `RepairReleasedLinkBombThrowKinematics` returned before reanchor.

**Fix (Follow-on 21):**

- `syNetRbSnapRepairLinkBombDecoupledThrowCouplingLive` — when fighter is in item throw scope and
  a ground idle Link bomb is nearby (or `fp->item_gobj` already points at it) but `is_hold=0`, call
  `itMainSetFighterHold` + Hold proc rebind. Runs PreSim (before release) and AfterSim (hand pose).
- Re-enable `syNetRbSnapRepairItemThrowWindowLive(FALSE)` from AfterSim forward repair.
- Verbatim throw kinematics: always `syNetRbSnapReanchorItemMPCollIntegration` even when verbatim on
  (skip floor probe only).
- Verbatim capture: normalize MPColl `pos_prev` for decoupled throw-scope ground bombs.

### Follow-on 22 (bomb-bomb collision + explode VFX with item_count≥2, soak2 post-spin-fix)

**Symptom:** Synctest pass (18/18 @2429, session `2074984261`), but colliding thrown bombs bounce
and persist instead of exploding/disappearing, and explode sparkle/quake is inconsistent when another
Link bomb is still on stage (`item_count=2` windows common during dual-Link SpecialN).

**Soak findings (tick 722–726 dual throw, Linux + Android identical):**

1. **Dual airborne bombs (@723–725):** Two thrown bombs at distinct positions (Y≈329 vs Y≈99) —
   no bomb–bomb proc_hit in log; one bomb hits a fighter @726 and bounces to Fall (`thrown=0`)
   while the other continues. Synctest still passes — deterministic bounce, not desync.

2. **Explode cosmetic gap:** `SkipLinkBombExplodeCosmeticReplayOnLoad` returns TRUE whenever the
   slot has any pre-explode Link bomb blob. That suppresses ring sparkle replay **and** blocks
   `EnsureQuakeEffectsFromSlot` when no quake effect blob was captured separately. Explode blob
   apply (`ReapplyLinkBombStatusAfterBlob`) intentionally skipped sparkle replay (hash hygiene),
   so one bomb exploding while another idle bomb exists → hidden item GObj with no sparkle/quake.

3. **F21 hold state leak:** Decoupled throw coupling called `itMainSetFighterHold` + Hold proc
   rebind without clearing `is_thrown` or setting `hitstatus=None`. Held bombs could retain
   `hitstatus=Normal` + stale thrown flag, confusing item–item hurtbox checks when bombs re-enter
   flight.

**Fix (Follow-on 22):**

- F21 decoupled hold: clear `is_thrown`, call `itLinkBombCommonSetHitStatusNone` after Hold rebind.
- Throw kinematics repair + blob apply: enforce `itLinkBombCommonSetHitStatusNormal` on
  Thrown/Fall/Wait so item–item collision always sees hittable hurtboxes after resim.
- Explode blob apply: replay sparkle via `syNetRbSnapReplayCosmeticExplodeSparkle` (presentational
  only — not hashed). `SkipLinkBombExplodeCosmeticReplayOnLoad` remains ring-history-only.
- `ShouldEnsureQuakeEffectsFromSlot`: return TRUE when slot contains an explode item blob (Link bomb
  or Marumine), even if another pre-explode bomb keeps SkipLinkBomb TRUE.

### Follow-on 23 (held-fuse explode audio double + missing sparkle, soak2 post-F22)

**Symptom:** Link holding a bomb when the fuse expires, two fighters in the blast — ExplodeL
sounds doubled/loud. Explosion sparkle/quake sometimes missing while damage still applies.

**Vanilla audio (not a desync):** `itLinkBombExplodeInitVars` plays ExplodeL once on init;
`ftMainUpdateDamageStatItem` plays `attack_coll.fgm_id` (also ExplodeL) per overlapping fighter
hit. Two victims → init + 2 hit SFX (3 total) is expected offline.

**Soak findings (session `2074984261`, Linux + Android identical):**

1. **No tick with both fighters in damage (status 70) simultaneously** — bot patterns rarely
   stack both peers in explode radius on the same fuse tick. Closest explode destroys:
   - @825: P1 damage (70), P0 throwing — one victim.
   - @1762: P0 damage (70), P1 throwing, ground bomb `y=0x42E20000` — one victim.
   Held-fuse explode (`hold=1` → lifetime 0 → `ExplodeInitVars`) not captured cleanly; long
   `hold=1` windows (@433–465) end in throw (@466), not in-hand detonation.

2. **F20 residual re-arm:** `syNetRbSnapRepairLinkBombExplodeAttackCollLive` still set
   `attack_state = New` whenever Off after AfterSim. Once a fighter connects, vanilla sets Off;
   repair re-arms → same victim retriggers `ftMainUpdateDamageStatItem` → extra ExplodeL on
   still-overlapping bodies. Reads as “doubled” with two fighters (each may get init-tick +
   repair-tick hit SFX).

3. **Missing animation conditions (all 6 bomb destroys in soak):**
   - `effect_count=0` on every explode-adjacent tick — quake blobs not present in slot saves
     (`eff=0x811C9DC5` empty hash throughout); sparkle is LBParticle (never snapshotted).
   - @737: `item_count=2` with decoupled `hold=1` ghost (`upt=520` stale) + ground bomb —
     `SkipLinkBombExplodeCosmeticReplayOnLoad` would suppress ring sparkle replay (F22 blob-apply
     sparkle fixes authoritative explode blobs only).
   - `SSB64_NETRB_SNAPSHOT_PARTICLE_DIAG` not enabled — no `particle_replay` / skip lines in log.

**Fix (Follow-on 23):** Remove `attack_state = New when Off` from explode coll repair; position-only
sync via `itProcessUpdateAttackPositions` (F20 path, no record clear).

### Follow-on 24 (explode sparkle/quake with item_count≥2 after quake exclusion, 2026-07-10)

**Symptom:** One Link bomb exploding while another pre-explode bomb is still on stage — damage OK,
sparkle/quake missing or inconsistent. F22 blob-apply sparkle helped only when an explode blob was
present; ring replay stayed gated by `SkipLinkBombExplodeCosmeticReplayOnLoad` whenever *any*
pre-explode bomb existed. Quakes are also `syNetRbSnapEffectHiddenFromRollback`
(`netplay_quake_cosmetic_rollback_exclusion`), so `ShouldEnsureQuakeEffectsFromSlot` opening the
gate for explode items could not restore a boom shake (`effect_count=0`).

**Fix (Follow-on 24):**

- `SkipLinkBombExplodeCosmeticReplayOnLoad`: return FALSE when the load slot has an explode item/
  weapon cosmetic source (Link bomb / Marumine / Samus bomb / Yoshi egg), even if another
  pre-explode Link bomb is live. Pre-explode-only + SpecialN pull still skip (soak1 @520 ghost).
- Explode blob apply (Link bomb + Marumine): mint `efManagerQuakeMakeEffect(1)` with sparkle
  (`syNetRbSnapReplayCosmeticExplodeQuake`), matching vanilla explode init.
- `ReplayExplodeSparklesFromRing`: seed dedupe positions from the load slot's current explode
  blobs (avoid double-mint vs blob apply); mint quake with hist Link/Marumine sparkle replays.
