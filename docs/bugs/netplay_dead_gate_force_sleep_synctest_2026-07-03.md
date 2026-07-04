# Netplay: apply-time force-sleep breaks eliminated-ghost synctest round-trip

**Date:** 2026-07-03
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android cross-ISA soak
**Status:** FIX IMPLEMENTED (soak pending). `build-netmenu` + `build-offline` link clean.

## Symptom

Soak `1511272357` (session paired, RNG matched) ran clean until a single deterministic
synctest failure on **both** peers at the same tick:

```
[host]  SYNCTEST_FAIL x1 first=2825   load_hash_drift=1
[guest] SYNCTEST_FAIL x1 first=2825   load_hash_drift=1
drift ticks on BOTH peers: 2825   -> diverged=anim,figh
```

Field diagnostics isolated the drift to player 0 (Kirby, KO'd on his **last stock**,
`stock=-1`, `is_ghost=1`); Fox matched perfectly (`full_ok=1 anim_ok=1`):

```
fighter_field_diff tick=2825 player=0 field=status_id   live=0x4 (Sleep)  blob=0x0 (DeadDown)
fighter_field_diff tick=2825 player=0 field=camera_mode live=0x1 (Ghost)  blob=0x0
NetStatusVars: corrupt dead_gate tick=2826 player=0 union_wait=0 dead_gate_wait=45
```

## Evidence

- The authoritative per-tick capture (`fighter_slot_hash`) shows Kirby in `DeadDown`
  (status 0) through tick 2825, then `Sleep` (status 4) from 2826 — a **natural**
  forward-sim transition (one capture row per tick, i.e. no rollback occurred here).
- The frozen last-KO ghost is not being simulated: `fhash_light` is byte-identical
  across 2820–2824 and `ftCommonDeadGetWait()` (the netmenu `dead_gate_wait` mirror)
  is pinned at 45 (`FTCOMMON_DEAD_WAIT`, the value set on dead entry — the countdown
  proc never runs for the game-over ghost).
- The 2825 failure is a **synctest probe**, not a real rollback: it loads slot 2825
  (faithfully `DeadDown`, `camera_mode=0`) and the apply path then mutates it to
  `Sleep` + Ghost camera, so `load(slot) != slot`.

## Root cause

`syNetRbSnapApplyFighterNetplayPost()` (`port/net/sys/netrollbacksnapshot.c`) ran two
**load-time state overrides** on eliminated fighters:

1. `syNetplayRebirthShouldForceSleepSetStatus()` → `ftCommonSleepSetStatus()` for **any**
   `stock=-1` fighter in a dead status, unconditionally (no dead-gate check). This
   forced `DeadDown → Sleep` on load even for a tick the forward sim had captured as
   `DeadDown`.
2. `syNetplayRebirthApplyEliminationPresentation()` for `stock=-1 || status==Sleep`,
   which re-set `camera_mode = Ghost` and the hide flags.

Every field both paths touch — `status_id`, `camera_mode`, `is_invisible`, `is_ghost`,
`is_shadow_hide`, `is_menu_ignore`, `is_playertag_hide` — is **snapshot-captured and
restored** already (`blob->camera_mode`, `blob->is_*`, status via the status-vars
restore). So the overrides are redundant with faithful restore and only ever *create*
divergence: for the pre-`Sleep` `DeadDown` ghost (captured `camera_mode=0`, dead gate
still 45) they rewrote `status_id`→Sleep and `camera_mode`→Ghost, exactly the two
fields the field diff flagged.

The `corrupt dead_gate` / `dead_wait_union_mismatch` witness lines are a second symptom:
`ftCommonSleepSetStatus` enters via `ftMainSetStatus(..., PRESERVE_NONE)`, which zeroes
the union `dead.wait`, but the forced-sleep path never called `ftCommonDeadClearGateWait`,
so the netmenu mirror stayed at 45 (`union_wait=0 dead_gate_wait=45`). The apply-side
force-sleep and the Classic co-op bonus branch in `ftCommonDeadCheckRebirth` were the
only `DeadDown → Sleep` transitions that omitted the gate clear every sibling branch has.

This surfaced now because `netplay_dead_rebirth_synctest_unskip_2026-07-02.md` removed the
`reason=dead` synctest skip. That doc explicitly predicted the first failure would be
"a real `SYNCTEST_FAIL` in `figh` … around KO lifecycle state (`dead_gate_wait`,
`dead.wait`, …)" — this is it. It only triggers on a final-stock (`stock=-1`) elimination,
so earlier soaks that didn't hit a last-stock KO in a probed window passed.

## Fix

`port/net/sys/netrollbacksnapshot.c`, `syNetRbSnapApplyFighterNetplayPost()`:

- Gate the force-sleep on `ftCommonDeadGetWait(fp) <= 0` so it converges to `Sleep`
  only when the dead gate is actually due — matching vanilla `ftCommonDeadCommonProcUpdate`
  and the wait-gated `syNetplayRebirthCatchUpDeadGateIfDue` that already runs a few lines
  below. A faithfully-captured `DeadDown` ghost (gate > 0) is now left untouched, so the
  slot round-trips.
- Add `ftCommonDeadClearGateWait(fp)` to that forced-sleep path so the mirror can never
  outlive the union `dead.wait`.
- Narrow the elimination-presentation branch from `(stock == -1 || status == Sleep)` to
  `status == nFTCommonStatusSleep`. Presentation for genuinely-asleep fighters is
  idempotent with the captured state (proven by the 2826+ `Sleep` slots passing synctest);
  dead (non-`Sleep`) fighters keep their faithfully-restored `camera_mode`.

`decomp/src/ft/ftcommon/ftcommondead.c`, `ftCommonDeadCheckRebirth()`:

- Add `ftCommonDeadClearGateWait(fp)` to the `#ifdef PORT` Classic co-op bonus
  `stock == -1 → Sleep` branch, matching every other transition in the function.
  (`ftCommonDeadClearGateWait` is internally `PORT && SSB64_NETMENU`-gated, so this is a
  no-op for offline / non-netmenu PORT builds.)

No new mirror, no new accessor — honors the FTStatusVars Approach-C contract.

## Follow-up risk

The apply-time force-sleep also *masked* a possible latent issue: if a **real** rollback
ever resimmed across a game-over `DeadDown → Sleep` boundary whose transition is driven by
scene/game-end state rather than the dead countdown, resim must reproduce that transition
from captured state alone. No such rollback occurred in this soak (the match simply ended
~62 ticks later at 2888). If a future soak shows a real `LOAD_HASH_DRIFT` (not a probe)
at a final-KO boundary, the correct fix is to capture the game-end scene state, not to
re-mutate fighter status on load.

## Verify

- `cmake --build build-netmenu --target ssb64 -j 4` — links clean.
- `cmake --build build-offline --target ssb64 -j 4` — links clean (decomp change is
  offline-safe; force-sleep block is `SSB64_NETMENU`-only).
- Lint clean on both touched files.

## Soak checklist

- Last-stock KO on any stage: expect **zero** `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` through
  the eliminated ghost's `DeadDown` hold and the `DeadDown → Sleep` transition.
- Expect **zero** `corrupt dead_gate` / `dead_wait_union_mismatch` lines during the KO
  countdown and elimination.
