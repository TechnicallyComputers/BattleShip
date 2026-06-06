# Netplay DK Jungle barrel cannon crash — hollow DObj + union scrub

**Slug:** `netplay_dk_jungle_tarucann_crash_2026-05-30`
**Status:** FIX SHIPPED (soak pending)
**Severity:** Critical — SIGSEGV when entering or firing from the barrel in netplay VS.

## Symptom

DK Jungle netplay VS (quantization off):

1. **Phase 1 repro:** Instant SIGSEGV when Link entered the floating barrel (`ftCommonTaruCannProcPhysics`, `fault_addr=0xC8` = NULL `GObj::obj`).
2. **Phase 2 repro:** Enter succeeded after Phase 1 fix; crash moved to **firing** from the cannon (button press or auto-fire countdown).

## Root cause

Two independent issues:

### A. Status-vars union scrub (Phase 1)

`syNetRbSnapClearCoupledGObjPointersInStatusPassive` unconditionally nulled `guard.effect_gobj` at union offset `0x08`, which aliases `tarucann.tarucann_gobj` and `twister.tornado_gobj` while those statuses are active.

### B. Hollow barrel DObj after rollback / particle reset (Phase 2)

Snapshot loads and `syNetRbSnapResetParticlesForRollback()` leave the barrel **GObj shell** alive but set `gobj->obj == NULL` (same family as Yoster clouds). Phase 1 guarded `ftCommonTaruCannProcPhysics` only; the **fire** path still calls:

- `grJungleTaruCannAddAnimOffset` → `DObjGetStruct(gobj)->child`
- `grJungleTaruCannGetRotate` → global `jungle.tarucann_gobj` DObj rotate

No `nGRKindJungle` entry existed in `syNetRbSnapRepairStageAfterParticleReset`. Ground snapshot also omitted barrel root translate / rotate.

## Fix

### Phase 1 (shipped earlier)

- Status-gated coupled-pointer scrub (mirror capture-id gates).
- `#ifdef PORT` NULL guard in `ftCommonTaruCannProcPhysics`.

### Phase 2 (shipped earlier)

| Area | Change |
|------|--------|
| `grjungle.c` | Init-time `sGRJungleTaruCannGobj` cache + `grJungleGetTaruCannGobj()` |
| `grjungle.c` | `grJungleReestablishTaruCannDobjTree` + `grJungleRepairTaruCannPresentation` |
| `grjungle.c` | PORT NULL/repair guards on anim, rotate, get-position, collision check |
| `netrollbacksnapshot.c` | Extend `SYNetRbSnapGroundJungle` with `tarucann_translate` + `tarucann_rotate_z` |
| `netrollbacksnapshot.c` | Restore via cache + repair; `syNetRbSnapEnsureJungleTaruCannAfterParticleReset` |
| `netrollbacksnapshot.c` | `syNetRbSnapResyncFighterTaruCannGobjs` after restore |

### Phase 3 (post-fire rollback desync)

| Area | Change |
|------|--------|
| `grjungle.c` | Cached barrel pose (`sGRJungleTaruCannCachedTranslate/RotateZ`); refresh each ProcUpdate |
| `grjungle.c` | `GetRotate`/`GetPosition` read-only (no repair side effects during sim) |
| `grjungle.c` | Hollow-tree repair uses cached pose, not `(NULL, 0)` |
| `grjungle.c` | `grJungleEnsureTaruCannCoupling` + `grJungleRestoreTaruCannAnimState` |
| `ftcommontarucann.c` | ProcPhysics rebinds barrel GObj and forces coupling repair before translate copy |
| `netrollbacksnapshot.c` | Jungle blob v2: root/child `mobj->anim_wait` bits + valid mask |
| `netrollbacksnapshot.c` | Restore re-anchors TaruCann rider translate from barrel root after load |
| Env | `SSB64_NETPLAY_JUNGLE_TARUCANN_DIAG=1` logs restore pose, `jungle_tarucann_occupancy` (live vs snap rider count/mask, shoot_wait/release_wait), per-rider waits, and `jungle_tarucann_orphan_shoot_suppressed` when shoot anim plays with no rider |

### Phase 4 (fire anim without launch after rollback)

Long soak (~5300 ticks, zero frame-commit diverges) showed one **cannon fire glitch**: A press played the barrel shoot animation but did not eject the fighter; a second press worked.

**Cause:** Barrel shoot visual (`grJungleTaruCannAddAnimShoot`) is decoupled from gameplay launch (`shoot_wait` countdown → `ftCommonTaruCannShootFighter`). Periodic synctest / soft-continue loads restored fighter `shoot_wait=0` while the live barrel still showed the shoot anim. Phase 3 anim capture checked `mobj->anim_wait`, but on PORT anim state lives on **`dobj->anim_wait`** (same as fighter snapshots via `syNetRbSnapCaptureDObjAnim`).

| Area | Change |
|------|--------|
| `grjungle.c` | `grJungleApplyTaruCannDObjAnimWait` writes `dobj->anim_wait` on PORT (+ syncs mobj if present) |
| `netrollbacksnapshot.c` | Jungle capture sets dobj valid mask from root/child presence; PORT reads `dobj->anim_wait` |
| `ftcommontarucann.c` | `ftCommonTaruCannResyncBarrelVisualAfterRollback`: shoot anim iff `shoot_wait > 0`, else fill |
| `netrollbacksnapshot.c` | Call resync from `syNetRbSnapResyncFighterTaruCannGobjs` after every jungle restore |
| Env | Diag logs rider `shoot_wait` / `release_wait` when `SSB64_NETPLAY_JUNGLE_TARUCANN_DIAG=1` |

### Phase 5 (shoot reconcile + slide/yakumono sync)

Long soak after Phase 4: stable (no crashes, `state_diverge=0`) but **first cannon press still misfired** for Link and DK when rollback overlapped fire — shoot anim without eject. Silent **map hash drift** during Move-phase cannon use (barrel slide presentation).

**Cause:** Phase 4 resync was one-way (`shoot_wait` → visual only). Restore with `shoot_wait=0` forced fill anim even when (a) child DObj still had shoot joint active, or (b) snapshotted tick input had an A/B edge not yet reflected in fighter `status_vars`. Guard scrub could still NULL `tarucann_gobj` via `guard.effect_gobj` alias when `is_shield` was set. Barrel gobj translate was restored but yakumono collision kinematics were not refreshed (`mpCollisionPlayYakumonoAnim`). Move-phase root slide joint was not re-armed after restore when tree stayed intact.

| Area | Change |
|------|--------|
| `ftcommontarucann.c` | `ftCommonTaruCannReconcileShootStateAfterRollback(gobj, snap_tick)`: shoot anim if `shoot_wait>0`; else immediate launch if child shoot joint active; else re-arm from input history edge on `snap_tick`; else fill |
| `grjungle.c` | `grJungleTaruCannIsChildShootAnimActive`, `grJungleTaruCannReapplyMoveSlideAnimIfNeeded`, `grJungleSyncTaruCannYakumonoCollision` |
| `netrollbacksnapshot.c` | Pass `snap_tick` into jungle restore/resync; guard scrub skips TaruCann/Twister; slide + yakumono sync after every jungle restore |

### Phase 5b (restore must not run sim)

Phase 5 soak regressed: barrel **X pinned** (`tx=3484.12` from tick ~509), **`PEER_BASELINE_MAP_DRIFT`** + **`state_diverge=2`** after Link cannon fire (~tick 839–960).

**Cause:** Phase 5 called **`mpCollisionPlayYakumonoAnim` during restore** (advances anim + `gMPCollisionUpdateTic`, rewrote translate). **`grJungleRepairTaruCannPresentation` overwrote root translate on every intact-tree restore** (pinned Move-phase slide). **`ftCommonTaruCannShootFighter` during reconcile** mutated fighter state off the sim tick (FC diverge with agreeing inputs).

| Area | Change |
|------|--------|
| `grjungle.c` | Intact tree: skip snapshot translate overwrite (Rotate phase still restores `rotate.z`); hollow rebuild only gets full pose |
| `grjungle.c` | Replace `mpCollisionPlayYakumonoAnim` with `grJungleRestoreTaruCannYakumonoKinematics` (zero yakumono speeds only, no anim/tic advance) |
| `grjungle.c` | Slide re-arm: parse default joint without `gcPlayDObjAnimJoint` on restore |
| `ftcommontarucann.c` | Orphan shoot anim → re-arm `shoot_wait=1`, launch on next `ProcUpdate` (never `ShootFighter` in reconcile) |
| `ftcommontarucann.c` | Reconcile: in-progress `shoot_wait` syncs anim only; `release_wait >= 180` catch-up on restore; manual tap via input history; orphan shoot during countdown → fill (no `shoot_wait` arm) so auto timer reaches 180 |
| `netrollbacksnapshot.c` | Jungle ground v3: rider count/mask, shoot_wait/release_wait, shoot_anim_active in map payload; light hash folds TaruCann waits; scrub stale tarucann overlay when not riding; resync suppresses orphan shoot anim when `live_riders=0` |

### Phase 5c (barrel anim snapshot partition — slide frozen)

Phase 5b soak: no crash / desync, firing consistent, but barrel **frozen at frame-0 base pose the entire match** (`tx=717.71 ty=-1597.5` constant across all 196 restores; only `rot` advances — `rot` is manual in `grJungleTaruCannUpdateRotate`, slide is anim-driven). Barrel sat under the stage and never moved.

**Cause (root):** the **visual barrel is its own ground GObj set up with `dobjs == NULL`** (`grJungleMakeTaruCann`), so it is **not** a `gMPCollisionYakumonoDObjs` entry — `syNetRbSnapApplyMap` never rewinds it. The jungle ground blob only snapshotted `anim_wait` (not `anim_frame` / figatree `event32` cursor / AObj chain), so rollback could not rewind the slide anim. The hand-rolled restore then made it worse: `grJungleTaruCannReapplyMoveSlideAnimIfNeeded` fired on **every** Move-phase restore (live cursor `!= default_joint` base once playing) and `gcAddDObjAnimJoint` re-seated the joint to its base pose without `gcPlayDObjAnimJoint`; `grJungleRestoreTaruCannAnimState`'s `END→NULL` `anim_wait` transform could stall the joint. Net: the barrel was repeatedly snapped to frame-0 base and never advanced. (Phase 5 only *appeared* to move because `mpCollisionPlayYakumonoAnim`-during-restore advanced the joint via its `else` branch — masking the missing snapshot.)

**Fix:** snapshot the barrel's **full DObj anim runtime** (root + child) the same way fighters/yakumonos do — new `SYNetRbSnapBarrelBlob` (`SYNetRbSnapDObjAnimBlob` × 2 + translate/rotate) in the slot (local ring memory, not the 128-byte ground payload, not sent on the wire). Capture in the snapshot pass; apply **after** the particle-reset stage rebuild (`syNetRbSnapRepairStageAfterParticleReset` jungle case) so it lands on the live/rebuilt tree and is authoritative. Forward resim then replays the slide deterministically on both peers.

| Area | Change |
|------|--------|
| `netrollbacksnapshot.c` | New `SYNetRbSnapBarrelBlob` + `slot->barrel`; `syNetRbSnapCaptureBarrel` (after `CaptureGround`); `syNetRbSnapApplyBarrel` via `syNetRbSnapApplyDObjAnim` (root+child), invoked from the jungle `RepairStageAfterParticleReset` case |
| `netrollbacksnapshot.c` | `syNetRbSnapRestoreJungleGround` no longer touches barrel anim (removed `RestoreTaruCannAnimState` / `ReapplyMoveSlideAnimIfNeeded` / `RestoreTaruCannYakumonoKinematics` calls); keeps pose/status scalars, hollow-tree rebuild, GObj recoupling |
| `grjungle.c` / `grjungle.h` | Removed now-dead `grJungleRestoreTaruCannAnimState`, `grJungleApplyTaruCannDObjAnimWait`, `grJungleTaruCannReapplyMoveSlideAnimIfNeeded`, `grJungleRestoreTaruCannYakumonoKinematics` |

Diag: `SSB64_NETPLAY_JUNGLE_TARUCANN_DIAG=1` now also emits `jungle_barrel_anim_restore … tx … ty … frame … wait` on each restore.

## Test plan

1. DK Jungle netplay VS, quantization **off**: enter barrel, press fire, auto-fire after 180f — no SIGSEGV on either peer.
2. Repeat with rollbacks active (`LOAD_HASH_DRIFT soft-continue` sessions): enter + fire after drift events.
3. **Phase 3:** fire from cannon, trigger rollback — Mario launch trajectory and peer `map` hash should match; no `PEER_BASELINE_MAP_DRIFT` on resim load.
4. **Phase 4:** after `LOAD_HASH_DRIFT soft-continue`, press fire in barrel — first press should always eject (no orphan shoot anim with `shoot_wait=0`); diag `mask=0x03` when barrel DObj tree intact.
5. **Phase 5/5b:** Link + DK fire during rollback — first press ejects; barrel **slides** in Move phase (tx not pinned); no `PEER_BASELINE_MAP_DRIFT` / FC diverge after fire.
5c. **Phase 5c:** barrel **moves/slides from spawn** (not frozen under stage); `jungle_barrel_anim_restore` shows `tx`/`frame` advancing across restores; slide stays cross-peer aligned through rollback (no map drift / FC diverge).
6. Hyrule tornado + Yoster clouds: smoke test unchanged.
7. Optional: quantization on — map drift may remain a separate issue.

## Related

- [`netplay_yoster_cloud_evaporate_2026-05-30.md`](netplay_yoster_cloud_evaporate_2026-05-30.md) — hollow GObj / init-time cache pattern
- [`netrollback_coupled_pointer_stability_2026-05-25.md`](netrollback_coupled_pointer_stability_2026-05-25.md) — coupled-pointer scrub design
- [`netplay_dk_jungle_effect_pop_desync_2026-05-25.md`](netplay_dk_jungle_effect_pop_desync_2026-05-25.md) — cosmetic RNG / effect drift (separate from this crash)
