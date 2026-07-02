# Netplay: Castle bumper `hit_anim_length` free-run folded into item hash

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-07-02
**Scope:** `port/net/sys/netsync.c` (`syNetSyncFoldGBumperItemExtras`) — hash-only, no sim behavior change.
**Builds:** `build-netmenu` + `build-offline` link clean.
**Supersedes the "position" hypothesis** in `netplay_castle_bumper_item_resim_diverge_2026-06-28.md` as the *remaining* cause once the 06-29 item-position quantization landed.

## Symptom

Soak2 session `736861014` (host=`soak2-android.log`, guest=`soak2-linux.log`), Peach's Castle,
`FORCE_MISMATCH inject=520`:

- `FRAME_COMMIT_STATE_DIVERGE validation=600` with **matching inputs** (`inp_local==inp_peer`),
  diverging in **`item,rng`** only (`figh`, `world`, `eff` all agree). Flagged by
  `netplay-scan-drift.py` as "genuine cross-ISA determinism failure".
- `rng` is downstream/cosmetic (the item walk shows `ring_steps=0`).

The 06-29 fix (quantize live item position + physics each accepted tick) is holding — the
`item_fold_floats` instrument (`SSB64_NETPLAY_ITEM_HASH_FIELD_DIFF=1`) shows every folded **float**
bit-identical cross-peer:

```
host : kind=gbumper sx_q=0x3F800000 sy_q=0x3F800000 pal_q=0x00000000 multi=0 hit_anim_length=64935
guest: kind=gbumper sx_q=0x3F800000 sy_q=0x3F800000 pal_q=0x00000000 multi=0 hit_anim_length=64936
```

The only diverging folded field is the integer **`hit_anim_length`: 64935 vs 64936** — off by one.

## Root cause

`hit_anim_length` (`u16`) is a hit-flash timer. `itGBumperCommonProcUpdate` reads it exactly once:

```c
if ((ip->item_vars.bumper.hit_anim_length == 0) && (dobj->mobj->palette_id == 1.0F))
    dobj->mobj->palette_id = 0;   /* flash over: clear palette, do NOT decrement */
else
    ip->item_vars.bumper.hit_anim_length--;   /* decrement — including underflow past 0 */
```

`itGBumperCommonProcHit` sets `palette_id = 1.0F` and `hit_anim_length = ITBUMPER_HIT_ANIM_LENGTH`.
The flash counts down; when it reaches 0 with `palette_id == 1.0F`, palette clears to 0. The **next**
tick `hit_anim_length == 0` but `palette_id == 0`, so it takes the `else` branch and underflows
`0 -> 0xFFFF`, then **free-runs decrementing every frame forever**. The values ~64935 are that
free-run (0xFFFF minus a few hundred ticks since the flash ended).

Post-flash the counter is **dead state**: nothing reads it (the sole read requires
`palette_id == 1.0F`, which never recurs once cleared) and the bumper scale is driven by `multi`. But
`syNetSyncFoldGBumperItemExtras` folded it into the item hash unconditionally. So a single 1-tick
discrepancy in how many times the bumper's `ProcUpdate` ran across the `FORCE_MISMATCH` resim
(recovery reanchored `last_agreed=480 mismatch=481`) permanently forks the item hash via a value that
has no gameplay meaning — a self-inflicted determinism failure on top of otherwise-converged state
(`figh`/`world`/floats all identical, `multi==0`, `palette_id==0` on both peers).

## Fix

Fold `hit_anim_length` only while the hit flash is actually live — gated on `palette_id == 1.0F`, the
exact liveness test the sim itself uses, and `palette_id` is itself folded and agrees bit-identically
cross-peer. When the flash is not active, fold a stable sentinel (`0`) in its place so the fold
sequence length is unchanged. During a genuine mid-flash divergence the counter is still folded and a
real fork is still caught; once the flash ends the dead free-run can no longer drift the hash.

Hash-only change in `netsync.c` (netmenu-only TU; offline uses `netsync_hash_stubs.c`). No change to
`itgbumper.c` sim behavior, so offline / ROM parity is untouched and the underflow quirk itself is
preserved as vanilla.

## Not addressed / notes

- The underflow is a vanilla decomp quirk; per the OFFLINE-vs-NETMENU contract we do not patch the
  offline sim to "fix" it — we only stop hashing the dead value in the netplay path.
- If a future soak shows a divergence **during** an active flash (`palette_id==1.0F`,
  `hit_anim_length` differing), that is a genuine one-sided `ProcHit`/collision-timing fork and should
  be chased in the fighter↔bumper hitbox path, not here.

## Verify

- `build-netmenu` `ssb64`: links clean.
- `build-offline` `ssb64`: links clean (unaffected).
- Soak pending: re-run the Castle pair, confirm the committed `item` hash stays converged post-flash
  (no `frame_commit_item_diverge` at 600 with `multi==0`/`palette_id==0`).

## Related

- [`netplay_castle_bumper_item_resim_diverge`](netplay_castle_bumper_item_resim_diverge_2026-06-28.md)
  — same object; 06-29 fixed the **position** drift (quantize). This is the remaining folded-field
  fork.
- [`netplay_quake_cosmetic_rollback_exclusion`](netplay_quake_cosmetic_rollback_exclusion_2026-07-02.md)
  — the `eff` half of the same soak (excluded quakes from the rollback hash).
