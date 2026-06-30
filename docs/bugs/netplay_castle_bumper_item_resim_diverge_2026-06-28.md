# Netplay: Peach's Castle Bumper item-only resim divergence (field-diff instrument)

**Date:** 2026-06-28 (root cause + fix 2026-06-29)
**Scope:** `port/net/sys/netsync.c` (diagnostic). **Fix:** `port/net/sys/netplay_sim_quantize.c` / `.h` (live item-physics canonicalize). Diagnosis touches `decomp/src/it/itground/itgbumper.c`, `port/net/sys/netrollbacksnapshot.c` (item blob capture/apply), `port/net/sys/netsync.c` (`syNetSyncFoldGBumperItemExtras`).
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending). The `item_fold_floats` instrument named the field on the 06-29 Castle soak: **it is the bumper's position (root DObj translate), not any of the five folded extras** — those are bit-identical cross-peer. Fix quantizes live item position + physics each accepted tick (fighter precedent).
**Class:** item-domain cross-peer **forward-sim** divergence (un-quantized movable item position accumulating cross-ISA f32 drift), surfacing as a late `frame_commit_item_diverge` → oversized recovery resim (perceived as a hard lock). Different object than the grab/throw coupling.

## 2026-06-29 update — field identified (position) and fixed

A fresh Castle soak (Link-vs-DK, `SSB64_NETPLAY_DISABLE_GRAB_COUPLING_SKIP=1`, `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1`) reproduced the divergence at **tick 719** and the `item_fold_floats` instrument fired on both peers. Side-by-side, **all five folded extras are bit-identical**:

```
both peers: sx_q=0x3F800000 sy_q=0x3F800000 pal_q=0x00000000 multi=0 hit_anim_length=64816 ...
```

The diverging field is the bumper **position X**, carried in the base item fold (`syNetSyncFoldItemState` hashes `dobj->translate.vec.f`):

```
android: pos=(-838.25000, 3525.0, 0)  fold=0x8296D2EE
linux:   pos=(-833.00000, 3525.0, 0)  fold=0x08FB82EE
```

A **~5.25-unit** gap — far larger than a `1/65536` grid cell, so this is **not** the scale-quantization hypothesis from the original audit below. It is accumulated cross-ISA float drift in the bumper's forward-sim position: the bumper is the one Castle item fighters can knock around (`atk_state=3`), it moved, and integrated to different X on arm64 vs x86_64. `rng` diverge is downstream (`ring_steps=0`, cosmetic seed), as the original audit predicted. The detection fired late (commit frontier `last_agreed=600`), forcing a `span=120` recovery resim (600→721) that grinds for several frames and is then torn down via `VS_SESSION_END` — the "hard lock a few seconds after the resim."

### Root cause

`netplay_sim_quantize.c` canonicalized **fighter** physics/transforms and the camera every accepted tick, but **no item path existed** — `ITStruct.physics` (`vel_ground`/`vel_air`) and the item root DObj translate ran un-quantized. Fighters and Link bombs stay in sync because they are quantized (or weren't knocked); the movable bumper was uniquely exposed.

### Fix

Added `syNetplayCanonicalizeItemSimState(GObj*)` + `syNetplayCanonicalizeActiveItemsForNetplay()` (and a static `syNetplayQuantizeItemPhysics`) to `netplay_sim_quantize.c`, called from `syNetplayCanonicalizeActiveFightersForNetplay()` (the accepted-sim-boundary canonicalize-all, alongside the existing camera pass). Each accepted tick, for every live item it snaps `ip->physics.vel_ground`/`vel_air` and the root DObj translate (the exact folded position) to the shared `1/65536` grid, so both peers integrate the next tick from bit-identical inputs — the same strategy that keeps fighters deterministic. Gated by `syNetplaySimQuantizeActive()` (netmenu + active VS/resim only; no-op offline and outside rollback). Built clean (`build-netmenu`); `build-offline` links unchanged. Snapshots inherit grid values automatically (live is on-grid at capture), so no separate blob change was needed.

### Soak procedure (to confirm)

Re-run the Castle pair, knock the bumper with a hit, and confirm the `item` committed hash stays converged from the hit onward (no `frame_commit_item_diverge`). Note the original detection reanchored to `last_agreed=600`, so the bumper began drifting well before the 719 trip — verify convergence across the whole post-hit window, not just the trip tick.

---

*Original 2026-06-28 audit (instrument bring-up) retained below for history. Its "scale straddling the quantization grid" suspicion was **disproven** by the 06-29 soak — the divergence is position, not scale.*

## Symptom

`soak2` cross-ISA pair (android host / linux guest, **current build** — post `netrollback_fighter_coupling_gobjid_ambiguity` 06-27 fix), Peach's Castle (`AUTOMATCH_STAGE_KIND=0`), DK (slot 0) vs Link (slot 1), `FORCE_MISMATCH=1 INJECT_TICK=520`:

- `netplay-trim-logs.py --sync-report` → `UNSTABLE`, per-peer `frame_commit_rng_diverge x1`.
- The user observed an in-game desync/"failing to resim" **with no grab move involved**.

Diffing the committed per-frame `NetSync: role=` digests on both peers:

- Ticks 468–490 (the "traced-back" origin region 475/481) commit **bit-identical**. The traced mismatch tick is a red herring.
- The first and only committed tick that differs is **601**, and it differs in **`item` only** — `figh`, `rng`, `world`, `anim`, `cam`, `p0..p3` all reconcile:

```
A: ... figh=0xFD89948C ... rng=0x93313F65 cam=0x8A8A5ED7 anim=0x6C1C9742 item=0x7FBF4AE8
L: ... figh=0xFD89948C ... rng=0x93313F65 cam=0x8A8A5ED7 anim=0x6C1C9742 item=0x7095EAE8
```

At detection (tick 599):

```
item_hash_walk reason=frame_commit_item_diverge slot_item=0x1390C522 live_item=0x17925522
  step=0 gobj_id=1013 kind=23 type=3 fold=0x53055BF6 hash=0x1390C522   (count=1)
rng_hash_walk  reason=frame_commit_rng_diverge ... ring_steps=0 count=0
```

`ring_steps=0`/`count=0` → the RNG ring logged no events; the `rng` divergence is **downstream** of the item, not an independent cause. The whole desync is one object.

## What the object is

Item `kind=23` = **`nITKindGBumper`** — the Peach's Castle **Bumper** stage hazard (`itGBumperCommonProcUpdate` / `itgbumper.c`). All items share `gobj->id == nGCCommonKindItem (1013)`; `gobj_id` cannot identify it. The Castle bumper is already a heavily special-cased rollback object (`syNetRbSnapFindLiveCastleBumperGObj`, `syNetRbSnapEjectStaleCastleGBumperShellsBeforeItemApply`, `syNetRbSnapEnsureCastleBumperAfterParticleReset`, recycled-id-1013 notes; prior crash fix `netplay_peach_castle_bumper_rollback_2026-05-30`).

## Audit result: nothing is *dropped*

The rollback hash for the bumper (`syNetSyncFoldGBumperItemExtras`) folds exactly five fields:

| Folded field | Captured/restored by snapshot? |
|--------------|--------------------------------|
| `ip->multi` | yes — `SYNetRbSnapItemBlob.multi` |
| `ip->item_vars.bumper.hit_anim_length` | yes — `item_vars[]` (full union memcpy) |
| `dobj->scale.vec.f.x` | yes — `present_anim_frame` (quantized on capture+apply) |
| `dobj->scale.vec.f.y` | yes — same |
| `dobj->mobj->palette_id` | yes — `present_anim_wait` (quantized) |

So the "missing-field" hypothesis is **disproven** — every folded field round-trips. This is a **value** divergence in one of the folded fields, not a coverage gap. Likely candidates:

- **`scale.x/y`** straddling a `1/65536` quantization-grid boundary. Scale is recomputed every `ProcUpdate` as `2.0F - ((10 - multi) * 0.1F)` (`0.1F` not exactly representable); cross-ISA last-bit rounding can land either side of a grid cell, surviving quantization as a different cell. (`sim_f32_quantize=vs_default` → quantization is ON, but grid quantization only *reduces* boundary divergence, it doesn't eliminate it.)
- **`palette_id`** — discrete `0`/`1.0F`; `1.0F` survives the grid exactly, so less likely, but possible if a transient mid-resim fighter divergence latched a one-sided `ProcHit` (`palette_id=1.0`, `multi=ITBUMPER_HIT_SCALE`, `hit_anim_length=ITBUMPER_HIT_ANIM_LENGTH`) that decays out of phase after the fighters reconverge.
- **`multi` / `hit_anim_length`** — integers, exactly captured; a divergence here means a one-sided `ProcHit` during the resim (collision-timing), not a snapshot-fidelity hole.

The existing per-field diagnostic could not distinguish these: `item_field_diff` prints `multi`/pos/vel, and the raw-vs-quantized float breakdown `item_fold_floats` was **`nITKindLinkBomb`-only** — the bumper's `scale.x/y`/`palette_id` were never broken out.

## Instrument shipped

Added a `nITKindGBumper` branch to `syNetSyncLogItemFieldDiffDiag` (`netsync.c`) that emits an `item_fold_floats` line with raw + quantized bits of exactly the folded floats (`scale.x`, `scale.y`, `palette_id`) plus the integer fold inputs (`multi`, `hit_anim_length`) and the carried-but-unfolded `unk_0x2`/`damage_all_delay` (union-aliasing surface):

```
SSB64 NetSync: item_fold_floats step=.. gobj_id=1013 kind=gbumper
  sx_raw=.. sy_raw=.. pal_raw=.. sx_q=.. sy_q=.. pal_q=.. multi=.. hit_anim_length=.. unk_0x2=.. damage_all_delay=..
```

Gated by the existing env `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1` (off by default; `soak2` did not have it set, which is why no per-field rows exist). Diagnostic-only; no sim/hash behavior change. Built clean (`build-netmenu`); `build-offline` links unchanged (uses `netsync_hash_stubs.c`, never compiles `netsync.c`).

## Soak procedure to pin the fix

Re-run the Peach's Castle cross-ISA pair with `SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1` on both peers, reproduce the bumper desync, then diff the `item_fold_floats` lines host-vs-guest at the divergence tick:

- **first `*_q` mismatch is the field whose hashed bits diverge** → that is the desync source.
- a `*_raw` mismatch with matching `*_q` = quantization absorbed it (not the source).

Then apply the field-specific fix:

- **scale.x/y diverge** → the cleanest fix is to stop folding the derived float scale and rely on the integer `multi` (scale is a pure deterministic function of `multi` recomputed each `ProcUpdate`; folding it adds cross-ISA fragility, not state coverage) — mirrors the `netrollback_sector_map_gobj_flags` "exclude a derived field from the fold" precedent. Validate it does not mask a real divergence first.
- **palette_id / multi / hit_anim_length diverge** → a one-sided `ProcHit` during the resim window; chase the collision-timing path (fighter↔bumper hitbox) rather than snapshot fidelity.

## Audit hook

An `item`-only committed divergence with `figh`/`rng`/`world`/`anim`/`cam` all reconciling, on Peach's Castle, single live item `kind=23` → the Castle Bumper. Before assuming a snapshot coverage gap, confirm the fold-field set is captured (it is) and use `item_fold_floats` (`SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1`) to name the diverging field; a derived float (scale) folded raw is the prime suspect. Unrelated to the grab/throw coupling (`netrollback_fighter_coupling_gobjid_ambiguity`) — no grab is involved.

## Related

- [`netplay_peach_castle_bumper_rollback`](netplay_peach_castle_bumper_rollback_2026-05-30.md) — same object, prior `grCastleBumperProcUpdate` SIGSEGV (stale `bumper_gobj`).
- [`netrollback_item_gobjid_ambiguity_resim`](netrollback_item_gobjid_ambiguity_resim_2026-06-26.md) — shared item id 1013 family.
- [`netrollback_sector_map_gobj_flags`](netrollback_sector_map_gobj_flags_2026-06-27.md) — "exclude a derived field from the rollback fold" precedent.
- [`netrollback_fighter_coupling_gobjid_ambiguity`](netrollback_fighter_coupling_gobjid_ambiguity_2026-06-27.md) — the grab/throw coupling fix this divergence is **not**.
