# Grab connect lost on resim, then unrepeatable

**Status:** root cause narrowed to one tick and one function; gate not yet named.
**Symptom (player):** "one player grabbed the other, but a resim happens and instead of
replaying the grab and grabbing them, it glitches and the player cannot grab them again at
all, they hit them and then can grab again after though."

## What the soak shows

The `search_catch` instrument caught it cleanly. Same tick, opposite outcome, on **both**
peers:

```
tick=453 player=1 status=166 is_catchstatus=1 search=0  search_status=10 resim=0   <- connects
tick=453 player=1 status=166 is_catchstatus=1 search=-1 search_status=-1 resim=1   <- misses
```

A second resim replays 452-462 and misses every tick of it. There is **no**
`grab_setstatus` line with `resim=1` anywhere in the session, so the `166 -> 167` connect is
never replayed.

## Consequence chain

| tick | host (Linux) | guest (Android) |
|---|---|---|
| 448 | p1 `16 -> 166` (Catch) | same |
| 453 live | p1 `166 -> 167`, p0 `10 -> 171` — **connect** | same |
| 453 resim | search finds nothing | search finds nothing |
| 455-461 | p0 back to `10` at 460 | p0 `172` (CaptureWait) then `186` (ThrownCommon) |
| 463 | p1 `166 -> 10`, catch expires unresolved | FC forces p0 back to `10` |
| 482, 500, 526, 556, 574 | p1 re-enters 166 five times, **never connects** | same |

So the host's failed resim is authoritative, the guest's completed grab-and-throw is
discarded at frame commit, and every later grab attempt misses. That last row is the
"cannot grab them again at all" the player reported.

## Ruled out

- **Input divergence** — the guest's live pass and the host's live pass agree exactly on
  tick 453. The disagreement appears only between live and resim of the same tick.
- **Peer disagreement** — both logs show the identical live-connect / resim-miss pattern.
- **attack_colls truncation** — `FTStruct.attack_colls[4]` (fttypes.h:1605) and the snapshot
  blob both carry 4; capture and apply loops cover all of them.
- **Adaptive D** — D was static at 4 for the whole session; the run predates no delay
  change at all.

## What is not yet known

`ftMainSearchFighterCatch` rejects a candidate at seven places. Any one of them could be
the difference, and the existing instrument only reports the final `search_gobj`:

1. `is_ghost` / `fkind == Boss` / team rules
2. `other_fp->capture_immune_mask & this_fp->catch_mask`
3. `special_hitstatus` / `star_hitstatus` / `hitstatus != Normal`
4. `attack_coll->attack_state == Off` — kills the search silently, no other symptom
5. air/ground mismatch (`other_fp->ga` vs `is_hit_air` / `is_hit_ground`)
6. the attack-record `catch_mask` veto
7. `gmCollisionCheckFighterAttackDamageCollide()` — the geometric accept

Gate 7 is the leading suspect: it is a position overlap test, and the frame-commit diff at
tick 461 shows player 0's `fold_topn_tx` differing live vs blob by roughly 16 units
(`0xC3691022` = -233.06 against `0xC3789EC0` = -249.62), far beyond grab range. Gate 4 is
the other candidate, because it produces exactly this symptom with no trace of its own.

## Instrumentation added (decomp `7b0f3fa00`)

- `syNetplayGuardGrabDiagLogCatchGates` — one line per candidate covering gates 1-5, read
  only, no control-flow coupling.
- `syNetplayGuardGrabDiagLogCatchCollide` — gate 7 with raw f32 bits for the hitbox
  position/size and the target's TopN translation, so a one-ulp restore error is visible.

Both are `PORT && SSB64_NETMENU` behind `SSB64_NETPLAY_GUARD_GRAB_DIAG=1`.

**Next soak:** land a grab, then compare the `catch_gates` / `catch_collide` lines at the
connect tick between `resim=0` and `resim=1`. The first field that differs is the bug.
