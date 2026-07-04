# Netplay: knockback collision re-anchor forks pos_prev/pos_diff fold on load

**Date:** 2026-07-03
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android cross-ISA soak
**Status:** FIX IMPLEMENTED (soak pending). `build-netmenu` + `build-offline` link clean.

## Symptom

Soak `1974820229` (session paired, RNG matched, `sigsegv=0`) ran to `max_sim_tick=4172`
but reported two deterministic synctest failures on **both** peers with the diverged
partition `figh`:

```
[FAIL] tick 3509: diverged=figh  [synctest probe]
[FAIL] tick 3629: diverged=figh  [synctest probe]
drift ticks on BOTH peers: 3509, 3629
```

Field diagnostics isolated both to player 1 (Kirby) while the opponent (Fox, player 0)
was in `RebirthWait` and matched. Kirby was in an **airborne knockback** motion status:

```
# tick 3509 — Kirby status 51 = DamageFlyHi
fighter_field_diff player=1 light_ok=0 full_ok=0 anim_ok=1 status=51 motion=44
  field=fold_coll_pos_prev_x live=0xC325CED2 blob=0xC311F2AF
  field=fold_coll_pos_diff_x live=0x00000000 blob=0xC19EE118
  field=fold_coll_pos_diff_y live=0x00000000 blob=0x4311F6D0
  field=coll_pos_prev_y      live=0x453F7564 blob=0x453655F7

# tick 3629 — Kirby status 57 = DamageFall
  field=fold_coll_pos_diff_x live=0x00000000 blob=0x41DC0000
  field=fold_coll_pos_diff_y live=0x00000000 blob=0xC2400000
```

The signature: on the **load** side the fighter's collision delta `pos_diff` is `0`, but
the captured **blob** has a nonzero `pos_diff` (real per-tick movement), and `pos_prev`
differs. Only the two `coll` collision-tracking fields diverge — velocities, TopN/root
translate, animation, status/motion all match (`anim_ok=1`).

## Evidence

- The stored slot fold hash (`fighter_slot_hash fhash_light=0x411045B0`) equals the blob
  fold. The re-derived live post-load fold (`0x604A5C90`) differs — i.e. loading the slot
  does not reproduce the slot's own captured fold: `load(slot) != slot` on `coll` fields.
- `syNetRbSnapApplyMPColl` **does** restore `dst->pos_prev = src->pos_prev; dst->pos_diff
  = src->pos_diff` from the blob — so the divergence is a post-apply mutation, not a
  missing restore.
- Only airborne-knockback / grounded-down-tech fighters ever fail this way, and only when
  a synctest probe happens to land while they are mid-flight (moving), which is why just 2
  of ~29 probes tripped it — idle fighters have `pos_diff==0` in the blob too, so no drift.

## Root cause

`pos_prev`/`pos_diff` are **per-tick collision scratch**: vanilla `ftMainProcPhysicsMap`
sets `pos_prev = *TopN` at the top of every tick and recomputes `pos_diff = *TopN -
pos_prev` after the move (`decomp/src/ft/ftmain.c`). They are consumed within the same
tick by collision and are not cross-tick gameplay state.

On the load/verify path, a netmenu-only re-anchor pass overwrites them **after** the blob
apply:

- `syNetRbSnapRefreshAirborneDamageKnockbackCollAfterLoad` hard-sets `pos_prev = *TopN` and
  zeros `pos_diff` for `fp->ga == nMPKineticsAir` fighters in `DamageFlyHi..DamageFlyRoll`
  / `DamageFall` (`syNetRbSnapStatusInAirborneDamageKnockbackScope`).
- `syNetRbSnapRefreshGroundedDownTechCollAfterLoad` does the same for
  `DownBounceD..DownWaitU` when the integration is stale.

Both run last, via `syNetRbSnapRefreshKnockdownCollFromSlot`, at the tail of the verify
prepare and the resim resync paths. The re-anchor is **load-bearing**: it prevents a
stale-`pos_prev` false `DownBounce` on the first resim tick. But because the ring fold
folds `coll.pos_prev`/`coll.pos_diff`, this deterministic re-anchor forks the live load
hash against the raw captured scratch — a **false** `LOAD_HASH_DRIFT[figh]`.

## Fix

`port/net/sys/netrollbacksnapshot.c`, `syNetRbSnapCaptureFighter()`:

Mirror the load-side re-anchor into the captured blob (netmenu-only). Right after
`syNetRbSnapCaptureMPColl`, for fighters in the same airborne-knockback / grounded-
down-tech scope, overwrite the blob's `coll.pos_prev = *TopN` and zero `coll.pos_diff`.
`*TopN` at capture equals `*TopN` after load (the joint translate round-trips faithfully
and is already hashed), so the slot now round-trips:

- Airborne knockback: load always re-anchors to `(*TopN, 0)`; blob now matches.
- Grounded down-tech: when stale the load re-anchors to `(*TopN, 0)` and the normalized
  blob matches; when not stale the blob already equals `(*TopN, 0)` (idempotent) and the
  load's stale check sees the normalized blob and skips the re-anchor.

Both peers normalize identically (position-derived, deterministic), so the cross-peer
`fighter_slot_hash` comparison is unaffected, and a real rollback restore is unchanged
(the load re-anchor produced these exact values already).

The re-anchor passes remain the sole owner of the load-side behavior; the capture scope is
kept in sync by comment with the two `Refresh*CollAfterLoad` predicates.

## Follow-up risk

If the airborne re-anchor ever grounds a fighter (`mpCommonSetFighterGround` when the
knockback position touches a floor), it would mutate more than `pos_prev`/`pos_diff`
(`ga`, floor binding). That path cannot fire for a genuinely airborne capture — the
forward sim would already have grounded the fighter at that position, moving it out of the
airborne scope — and the field diffs confirmed only `pos_prev`/`pos_diff` ever diverged.
If a future soak shows `ga`/floor divergence in this scope, extend the capture mirror to
reproduce the full re-anchor.

## Verify

- `cmake --build build-netmenu --target ssb64 -j 4` — links clean.
- `cmake --build build-offline --target ssb64 -j 4` — links clean (capture mirror is
  `SSB64_NETMENU`-only).
- Lint clean on the touched file.

## Soak checklist

- Any airborne knockback (`DamageFly*`/`DamageFall`) or grounded down-tech
  (`DownBounce`/`DownWait`) fighter under a synctest probe: expect **zero**
  `LOAD_HASH_DRIFT[figh]` with only `coll.pos_prev`/`coll.pos_diff` diverging.
