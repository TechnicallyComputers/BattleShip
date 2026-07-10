# Emergency-restore Link-bomb sparkle window: u32 tick overflow wedges the game thread

**Date:** 2026-06-27
**Scope:** `port/net/sys/netrollbacksnapshot.c` (`PORT && SSB64_NETMENU`)
**Status:** FIX IMPLEMENTED (soak pending)
**Lineage:** the "`RestoreLiveEmergency` wedging the game thread" follow-up from
[netrollback_sector_map_gobj_flags](netrollback_sector_map_gobj_flags_2026-06-27.md).
Supersedes an earlier **misdiagnosis** that blamed the Sector Z Arwing tree walk
(see "Correction" below).

## Symptoms

With synctest enabled (`SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`) the cross-ISA soak
hangs at the intro→GO boundary on **every stage tried** (Sector Z `gkind=1` *and*
Peach's Castle `gkind=0`). The map/`anim`/`item` partitions round-trip; the only
drift is `cam`, correctly tolerated:

```
LOAD_HASH_DRIFT tick=389 … map=…/= … cam=0x0B4813E7/0x4C7E2A25 anim=…/= …
LOAD_HASH_DRIFT presentational-only — continuing resim tick=389 …
WATCHDOG HANG since_frame=3002ms … active_tid=5(game) … scene=22(VSBattle)
```

Both peers wedge in the **same** spot — the watchdog refires with an identical
stack every ~2 s before aborting, i.e. a true infinite loop. The release binary
that built the AppImage (`build-bundle-linux-netplay-us/BattleShip`, unstripped,
`syNetRbSnapshotRestoreLiveEmergency` @ `0x5a15b0` — matches the AppImage's load
offset exactly) resolves the frames to:

```
syNetRbSnapApplySlotToLive                          (netrollbacksnapshot.c:30993)
  → syNetRbSnapReconcileLinkBombWindowEffectsCore   (:14738)
    → syNetRbSnapshotSlotInLinkBombEffectRepairScope (:14643  loop)
      → syNetRbSnapSlotTickHasExplodeSparkleReplay  (:29570  PC sampled here)
```

## Root cause

`syNetRbSnapshotSlotInLinkBombEffectRepairScope` scans a backward sparkle-replay
window by tick:

```c
/* SYNETRB_LINK_BOMB_SPARKLE_REPLAY_WINDOW == 48 */
sparkle_start = (probe_tick < WINDOW) ? 0 : probe_tick - WINDOW;
for (sparkle_tick = sparkle_start; sparkle_tick <= probe_tick; sparkle_tick++)
    if (syNetRbSnapSlotTickHasExplodeSparkleReplay(sparkle_tick)) return TRUE;
```

`syNetRbSnapReconcileLinkBombWindowEffectsCore` (and the finalize-path callers)
pass `slot->tick` as `probe_tick`. During `syNetRbSnapshotRestoreLiveEmergency`
the slot is the **emergency sentinel**, whose `tick == 0xFFFFFFFF`
(`syNetRbSnapshotCaptureLiveEmergency` → `syNetRbSnapFillSlotFromLive(&slot,
0xFFFFFFFFU)`). So:

- `probe_tick = 0xFFFFFFFF`
- `sparkle_start = 0xFFFFFFFF - 48 = 0xFFFFFFCF`
- `sparkle_tick <= 0xFFFFFFFF` is **true for every `u32`** — when `sparkle_tick`
  reaches `0xFFFFFFFF`, `++` wraps to `0`, `0 <= 0xFFFFFFFF` is still true, and the
  scan spins forever (each `syNetRbSnapSlotTickHasExplodeSparkleReplay` does a ring
  lookup that returns `FALSE`, so it never breaks).

The loop is reached whenever the emergency slot has no Link-bomb item, no
fighter-in-bomb-scope, and no quake blob (the common case at the intro boundary) —
which is why it reproduces on **any** stage. It is a generic Link-bomb-window
reconcile reached from `syNetRbSnapApplySlotToLive` line 30993; nothing about it is
stage-specific.

## Fix

In `syNetRbSnapshotSlotInLinkBombEffectRepairScope`:

1. **Count-bounded scan** (core fix, covers all six callers): iterate an offset
   `0..span` where `span = probe_tick - sparkle_start <= WINDOW`, so the loop
   counter never compares against `UINT32_MAX` and cannot wrap regardless of
   `probe_tick`.

```c
sparkle_span = probe_tick - sparkle_start;      /* always <= WINDOW (48) */
for (sparkle_off = 0U; sparkle_off <= sparkle_span; sparkle_off++)
    if (syNetRbSnapSlotTickHasExplodeSparkleReplay(sparkle_start + sparkle_off)) return TRUE;
```

2. **Sentinel-tick substitution** (semantic correctness): the emergency slot
   represents live@now, so scan the live tick window rather than the meaningless
   `0xFFFFFFCF..` range.

```c
if (probe_tick == 0xFFFFFFFFU) probe_tick = syNetInputGetTick();
```

Either change alone stops the hang; both together keep the repair-scope decision
correct for the emergency restore.

## Correction — the Sector Z Arwing misdiagnosis

The first pass at this bug (same `RestoreLiveEmergency` hang, seen on Sector Z)
concluded the loop was the unguarded Sector Arwing DObj tree walk
(`grSectorReestablishArwingVisualTree` / `grSectorArwingApplyAnimTransforms`),
because the `sector_arwing_restore` end-diag never printed and the static frames
*looked* adjacent to `syNetRbSnapApplyArwing`. That was wrong: I inferred the
frames from raw offsets without symbolizing (the sandbox blocked extracting the
ELF from the AppImage). The Peach's Castle repro — where `syNetRbSnapApplyArwing`
early-returns on `gkind != nGRKindSector` yet the hang persists with the identical
stack — disproved it, and `addr2line` against the matching release binary named the
real functions.

Consequences for the tree, now corrected:

- The **`ApplyArwing`-skip-on-emergency-sentinel** branch added to
  `syNetRbSnapEnsureSectorArwingAfterParticleReset` was **reverted** (it fixed
  nothing and added an untested branch).
- The **`GR_SECTOR_ARWING_TREE_WALK_MAX_NODES` (512) iteration caps** on
  `grSectorArwingCountTreeDObjs` / `grSectorArwingApplyAnimTransformsWalk` were
  **kept** purely as a cheap defensive watchdog — those walks genuinely had no
  cycle guard — but they are *not* what fixed this hang.

Audit hook: a true infinite-loop watchdog hang (identical stack refiring) reached
only via the emergency restore = the `0xFFFFFFFF` sentinel tick flowing into
tick-window arithmetic. Symbolize against the **exact** build (here
`build-bundle-linux-netplay-us` matched the AppImage); never infer frame identity
from raw offsets.

## Follow-ups (not in this fix)

- Other consumers of `slot->tick` in tick-window math should be audited for the
  same sentinel hazard (`grep` for `slot->tick` near loops / subtractions).
- The `intro_wait` synctest blind spot (from the `map_gobj_flags` doc) still leaves
  the countdown unverified.

## Test plan

1. Cross-ISA soak with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` on Peach's Castle
   (`AUTOMATCH_STAGE_KIND=0`) and Sector Z: no `WATCHDOG HANG` in
   `syNetRbSnapshotRestoreLiveEmergency`; match continues past the GO boundary.
2. Confirm `SYNCTEST=0` soak still passes (no regression).
3. Link-bomb gameplay (Link bomb spam on Peach's Castle, explode-sparkle/quake
   windows) behaves as before during normal rollback.
