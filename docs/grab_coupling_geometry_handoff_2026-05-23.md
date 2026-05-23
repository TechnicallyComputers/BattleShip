# Grab Coupling Geometry — Investigation Handoff (2026-05-23)

**Audience:** Main decomp / gameplay dev (upstream `ssb-decomp-re` or equivalent)  
**Port context:** BattleShip PC port + rollback netplay (`port-patches` on `JRickey/ssb-decomp-re`)

---

## The bug

During a **standard grab hold** (`CatchWait` on grabber, `CaptureWait` / `CapturePulled` on victim), the victim can appear **upside-down** with feet still roughly on the grabber’s floor line and the upper body **clipped into the ground**. The case that first surfaced in soak was **Ness → Donkey Kong** (large victim exaggerates the error), but once the fix stack landed the hold pose corrected for **other grabber/victim pairs** too (Fox, Mario, DK cargo, etc.) — not a Ness-specific script issue.

**Where we saw it**

| Context | Repro notes |
|---------|-------------|
| Rollback netplay + synctest | Very reliable with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`; also persisted on forward sim after bad snapshot rounds |
| Offline / local VS | Reported as affecting “core game” behavior as well — same visual (inverted victim, floor-snapped Y) after slope movement or other transform-cache stress |
| Delay-based netplay | Often looked fine; rollback load/resim ordering exposed it |

**What it is not**

- Not fixed by changing Ness or DK motion scripts.
- Not the ROM “make all grabs horizontal” slope-contour experiment (that path was **reverted**; see [Reverted work](#reverted-work-do-not-reapply) below).
- Not aerial grab attach (Y-from-hand for air captures was intentionally left unchanged pending N64 verification).

---

## How vanilla builds the grab hold pose

Victim position/rotation during hold is **not** a single joint attachment. Two independent code paths are composed every physics tick:

| Axis / component | Source | Function |
|------------------|--------|----------|
| **X, Z, rotation** | Grabber **heavy-item hand joint** world matrix (`joint_itemheavy_id`) | `ftCommonCapturePulledRotateScale` in `decomp/src/ft/ftcommon/ftcommoncapturepulled.c` |
| **Y** | Project victim onto grabber’s **floor line** (not hand height) | `ftCommonCaptureWaitProcMap` / `ftCommonCapturePulledProcMap` in `ftcommoncapturewait.c` / `ftcommoncapturepulled.c` |

Thrown states and **DK back cargo** (`Shouldered` victim + grabber `ThrowF*`) reuse the same hand-joint rotation path via `ftCommonThrownProcPhysics` → `ftCommonCapturePulledRotateScale`.

**Failure mode:** If the grabber’s cached joint/world transforms are **stale** when `ftCommonCapturePulledRotateScale` runs, X/Z/rotation reflect an old hand pose (often inverted relative to the current anim) while Y still snaps to the grabber’s floor → classic “upside-down, feet on ground, torso in floor” look. Any character pair can show it; large victims (DK) make it obvious.

---

## Root causes (three layers)

### 1. Stale `FTParts` transform cache on the grabber (engine — affects offline too)

The port (and any lazy transform implementation) can leave `FTParts->transform_update_mode` / `unk_dobjtrans_*` caches populated from an **earlier** frame or status while the grabber’s DObj anim has moved on. `ftCommonCapturePulledRotateScale` reads the hand joint matrix **without** forcing a refresh first.

**Contributing engine issue:** Stale **slope-contour FULL** root pitch (`rotate.x`) after `SetSlopeContour` clears or after leaving a FULL body-tilt state without invalidating part transforms. That poisons hand-matrix reads for subsequent grabs until something else rebuilds the skeleton.

### 2. Rollback snapshot load ordering (netplay-only)

On snapshot apply, grab geometry refresh initially ran in **core apply** before:

1. `syNetRbSnapshotSyncFighterPresentation()`
2. `ReapplyFighterJointAnimFromSlot()`

So hand-joint reads used pre-restore transforms; presentation then rebuilt figatree/joint AObj without re-deriving victim pose. Same class as Yoshi egg finalize-order bug: [`docs/bugs/netrollback_weapon_load_finalize_order_2026-05-20.md`](bugs/netrollback_weapon_load_finalize_order_2026-05-20.md).

### 3. Forward sim only refreshed on load finalize (netplay-only)

Even with correct finalize order, `syNetRbSnapshotRefreshGrabCouplingGeometry()` ran only at **load finalize**, not after each live `gcRunAll` tick during rollback forward sim. A single bad coupled pose could be **saved into the snapshot** and replicated on both peers.

---

## What we shipped (patch inventory)

### A. Engine / decomp (`#ifdef PORT` where noted)

| Change | File | Purpose |
|--------|------|---------|
| **`ftParamInvalidateFighterTransformFromRoot`** — walk joints from TopN to root, clear `transform_update_mode` and `unk_dobjtrans_*` | `decomp/src/ft/ftparam.c`, `ftparam.h` | Force full transform rebuild after root rotate / slope changes |
| **`ftMainApplySlopeContourFlags`** — on transition **off** FULL contour, zero root pitch + invalidate | `decomp/src/ft/ftmain.c`, `ftmain.h` | Match `SetSlopeContour(0)` clearing stale body tilt |
| **Per-frame stale FULL guard** — if FULL flag off but `rotate.x ≠ 0`, clear pitch + invalidate | `ftmain.c` (`ftMainProcPhysicsMap`) | Catch scripts that clear contour without zeroing root |
| **Invalidate after slope proc** | `decomp/src/mp/mpcommon.c` (`mpCommonUpdateFighterSlopeContour`) | Attack coll / grab reach see current transforms |
| **Invalidate grabber before hand read** | `ftcommoncapturepulled.c` (`ftCommonCapturePulledRotateScale`, `#ifdef PORT`) | Core fix for stale hand matrix during hold |
| **NULL guards** on `capture_gobj` / `capture_fp` in capture wait/pulled ProcMap | `ftcommoncapturewait.c`, `ftcommoncapturepulled.c` (`#ifdef PORT`) | Avoid SIGSEGV when rollback breaks coupling mid-grab |
| **Throw release NULL guards** on `capture_gobj` / `throw_desc` | `ftcommonthrown2.c` (`#ifdef PORT`) | Related grab/throw synctest crash family |

**Decomp commits (fork `port-patches`):**

- `f71ef4b6c` — `ftParamInvalidateFighterTransformFromRoot`, `ftMainApplySlopeContourFlags`, stale FULL guard, slope proc invalidate
- `f8ae7c9b5` — grab: invalidate grabber in `ftCommonCapturePulledRotateScale` + capture ProcMap NULL guards (same commit bundles Ness PK Thunder / Pikachu rollback work)

### B. Netplay / rollback (`port/net/sys/`)

| Change | File | Purpose |
|--------|------|---------|
| **`syNetRbSnapshotRefreshGrabCouplingGeometry()`** — invalidate grabber; rerun victim `CapturePulled` physics + wait/pulled/thrown ProcMap | `netrollbacksnapshot.c` | Re-derive coupled pose from current grabber skeleton |
| **Finalize order:** call refresh **after** presentation sync + `ReapplyFighterJointAnimFromSlot()` | `syNetRbSnapshotFinalizeLoadFromSlot()` | Fix stale hand read on load (phase 2) |
| **Per-tick refresh** when grab coupling active, before snapshot save | `netrollback.c` (`syNetRollbackAfterBattleUpdate`) | Stop persisting bad hold pose during forward sim (phase 3) |
| **`syNetRbSnapRebindFighterGrabCoupling()`** — pointer-only `catch_gobj` ↔ `capture_gobj` repair | `netrollbacksnapshot.c` | Coupling IDs survive apply; geometry refresh is separate |
| **`throw_desc_ptr` in fighter blob** | `netrollbacksnapshot.c` | Round-trip motion `SetThrow` desc for throw-phase verify |
| **Removed** synctest skip `reason=grab_coupling` | `netrollback.c` | Probes run through hold/throw when round-trip is correct |

**Port-side commits (outer repo):** `f6cc5e6` and related rollback grab handling on `BattleShip` main tree.

### C. Related netplay docs (detail)

| Doc | Topic |
|-----|--------|
| [`docs/bugs/netplay_grab_geometry_stale_joint_2026-05-22.md`](bugs/netplay_grab_geometry_stale_joint_2026-05-22.md) | Stale joint + phase 3 per-tick refresh |
| [`docs/bugs/netplay_grab_synctest_roundtrip_2026-05-22.md`](bugs/netplay_grab_synctest_roundtrip_2026-05-22.md) | Finalize order + `throw_desc_ptr` |
| [`docs/bugs/netplay_grab_synctest_throw_segv_2026-05-20.md`](bugs/netplay_grab_synctest_throw_segv_2026-05-20.md) | Coupling rebind + throw NULL guards |
| [`docs/bugs/fighter_slope_contour_attack_coll_2026-05-20.md`](bugs/fighter_slope_contour_attack_coll_2026-05-20.md) | Slope stale-tilt engine fixes + ROM revert notes |

---

## Reverted work (do not reapply)

Early investigation assumed “all standing grabs should be upright / horizontal on N64” and patched **relocData** motion scripts (Yoshi grab `0x0D34` FULL→(3), Kirby/Pikachu/Purin contour injections, catch-search upright gate). **Hardware reference disproved that** — Yoshi standing grab uses **`SetSlopeContour(4)` (FULL)** by design.

All ROM script edits and `ftMainIsCatchSearchCollActive` upright forcing were **reverted** in `f71ef4b6c`. Engine stale-tilt fixes above were **kept**. Do not reintroduce bulk class-B ROM changes without per-move N64 calibration ([`docs/slope_contour_audit_2026-05-20.md`](slope_contour_audit_2026-05-20.md)).

---

## Recommended upstream fix

Goal: **vanilla-correct hold pose** whether or not rollback exists.

### Minimum (likely sufficient for matching decomp + modern ports)

1. **Before** `ftCommonCapturePulledRotateScale` reads the grabber hand joint, ensure grabber world transforms are current — equivalent to calling `ftParamInvalidateFighterTransformFromRoot(capture_gobj)` then running the normal parts update path the game uses before collision/display (on the port: `ftParamsUpdateFighterPartsTransformAll` on the grabber if needed after invalidate).

2. Keep **slope-contour FULL clear** behavior tied to transform invalidation (`ftMainApplySlopeContourFlags` pattern) so root pitch does not leak into unrelated states.

3. **Do not** split Y onto the hand joint without N64 proof — floor projection for Y is intentional vanilla behavior.

### If the bug reproduces on stock N64 hardware

Then the issue is likely **ordering** in vanilla (something updates transforms after victim physics on some frames). Compare frame order: grabber `proc_physics` / `proc_slope` / joint anim vs victim `CaptureWait` physics. A one-line invalidate in `ftCommonCapturePulledRotateScale` may be the faithful fix if vanilla effectively refreshes there implicitly.

### Netplay / save-state layers

Any system that **restores fighter state mid-match** must, after joint anim/presentation is current:

1. Rebind `catch_gobj` ↔ `capture_gobj`
2. Call the equivalent of `syNetRbSnapshotRefreshGrabCouplingGeometry()`
3. Snapshot `throw_desc` (or re-run the motion event that sets it) before throw-phase simulation

Reference implementation: `syNetRbSnapshotRefreshGrabCouplingGeometry()` in [`port/net/sys/netrollbacksnapshot.c`](../port/net/sys/netrollbacksnapshot.c) (~line 1669).

### Matching note

`ftParamInvalidateFighterTransformFromRoot` is labeled `// 0x800EB528` in the port tree but was introduced as **new helper code** during port work — confirm against baserom before claiming a match. If no single vanilla function exists, inline the cache clears at each call site for IDO matching, or match a nearby existing invalidate routine if one is identified in disasm.

---

## Verification checklist

**Offline / local**

- [ ] Peach’s Castle flat: Ness → DK, Fox → DK, Mario → Link — victim upright in hold, no floor clip
- [ ] Same pairs after **run on slope → grab on flat** (stale FULL tilt stress)
- [ ] DK forward throw → cargo (`Shouldered`) — victim on back, not inverted
- [ ] Yoshi standing grab on slope — body/tongue **slope-aligned** (vanilla FULL), not forced upright

**Netplay rollback**

- [ ] Grab hold through 120+ tick window with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`: `SYNCTEST_OK`, upright victim
- [ ] Rollback induced during hold: no inverted victim after resim
- [ ] Throw release: no `LOAD_HASH_DRIFT anim`, no SIGSEGV (Fox throw path)
- [ ] Log does **not** need `SYNCTEST_SKIP reason=grab_coupling` (skip removed 2026-05-22)

**Build**

```bash
cmake --build build --target ssb64 -j 4
```

---

## Key code references

**Victim pose assembly**

```11:86:decomp/src/ft/ftcommon/ftcommoncapturepulled.c
void ftCommonCapturePulledRotateScale(GObj *fighter_gobj, Vec3f *this_pos, Vec3f *rotate)
{
    // ... reads capture_fp->joints[joint_itemheavy_id] world matrix for XZ/rot ...
    // PORT: ftParamInvalidateFighterTransformFromRoot(this_fp->capture_gobj) before read
}
```

**Netplay geometry refresh**

```1660:1713:port/net/sys/netrollbacksnapshot.c
void syNetRbSnapshotRefreshGrabCouplingGeometry(void)
{
    // invalidate grabber; rerun CaptureWait/Pulled/Thrown physics+map per victim
}
```

**Per-tick forward sim hook**

```4545:4549:port/net/sys/netrollback.c
	if (syNetRbSnapshotAnyFighterGrabCouplingActive() != FALSE)
	{
		syNetRbSnapshotRefreshGrabCouplingGeometry();
	}
```

---

## Summary for main dev

| Layer | Problem | Fix |
|-------|---------|-----|
| Engine | Stale grabber joint matrices + stale slope root tilt | Invalidate transforms when contour clears and **before** hand-joint read in `ftCommonCapturePulledRotateScale`; stale FULL guard in `ftMainProcPhysicsMap` |
| Netplay | Refresh before presentation/joint reapply; no per-tick refresh | Finalize-order move + `syNetRbSnapshotRefreshGrabCouplingGeometry()` after each sim tick when coupled |
| ROM scripts | Incorrect “all grabs upright” patches | **Reverted** — keep vanilla Yoshi FULL `(4)` |

The Ness→DK case was a **canary** for generic grab coupling geometry, not character-specific data. Porting the engine invalidate + correct refresh ordering should be the upstream takeaway; netplay needs the explicit re-derive pass after any snapshot load or mid-match state restore.
