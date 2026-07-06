# Netplay Link bomb item throw resim kinematics — 2026-07-05

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending — Follow-on 11 MPColl gate)

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
