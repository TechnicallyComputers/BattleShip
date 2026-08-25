# Netplay — frontier floor undoes the load rescue, deadlocking into a terminal hold

**Date:** 2026-08-25
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED — re-soak pending
**Soak:** Android guest ↔ Linux host, hang at sim 1041 during a grab
**Related:** [walkback floor](netplay_light_frontier_load_fail_walkback_floor_2026-08-21.md) (the rescue), [hold escape](netplay_battle_sim_hold_no_escape_2026-08-21.md) (what should have ended the match)

## Symptom

The match froze for ~6 s until the player killed the app. A load-fail hold armed
mid-grab (p0 `167 CatchPull`, p1 `171 CapturePulled`):

```
LOAD_HASH_DRIFT tick=1039 figh=0x57D294BB/0x602D4A72 ...
LOAD_HASH_DRIFT soft-continue blocked tick=1039 reason=fighter_mismatch
LOAD_HASH_DRIFT resim fidelity — deferring session stop tick=1039 (caller may walk back)
RESIM_LOAD_FIDELITY_RETRY failed=1039 deeper=1038 mismatch=1039 attempt=1 (below episode floor — rescue)
FRONTIER_BEGIN_FLOOR mismatch=1039->1040 load=1038->1039 frontier=1040 target=1041
FRONTIER_BEGIN_FLOOR load failed load_tick=1039
BATTLE_SIM_HOLD armed sim=1041 load_tick=1039 reason=resim_load_fail fail_count=3
```

## Root cause: two floors fighting

1. Tick 1039 fails to load (`LOAD_HASH_DRIFT`, `reason=fighter_mismatch`).
2. The fidelity walkback **rescues it** — walks to 1038 and loads successfully.
   (This is the 2026-08-21 fix working as intended.)
3. `FRONTIER_BEGIN_FLOOR` then clamps `load` back **up** to 1039, because the
   shared correction frontier is 1040 and it refuses ordinary GGPO episodes that
   landed behind it.
4. 1039 fails again — same mismatch as step 1 — and the hold arms.

The rescue says *go deeper*; the frontier says *never below the frontier*. Neither
yields, so a load that had a working anchor ends as a dead match.

## Fix

On the **already-failed** path only, the frontier floor now yields: if the clamped
`floor_load` fails to load and the pre-clamp (rescue) anchor is strictly deeper,
reload that anchor, restore its `mismatch_tick`, and proceed —
`FRONTIER_BEGIN_FLOOR_YIELD`. The hold arms only if the deeper anchor also fails.

When the floor load succeeds, behaviour is unchanged.

### This is a real relaxation, deliberately taken

Opening an episode behind the shared frontier is precisely what this clamp exists
to prevent. Accepting it here is a trade, not a free win:

- **Cost:** the episode replays from behind the frontier, which can produce a
  state the peer does not share.
- **Why it is still better:** that mismatch is *detectable and healable* by the
  frame-commit / FC layer, whereas `BATTLE_SIM_HOLD` is terminal — the match is
  over either way, and this path at least has a chance of continuing.
- **Scope:** only reached after a load has already failed, so healthy episodes
  never take it.

If re-soak shows the yield producing durable divergence rather than a healed
mismatch, the alternative is to fail fast instead — detect the
clamp-undoes-rescue cycle and end the episode immediately rather than burning
three attempts into a hold.

## Verification

- netmenu + offline Debug builds compile and link clean.
- Control flow checked: `syNetInputSetTick()` runs exactly once on each success
  path (floor load OK, or yield to the deeper anchor) and never on the failure
  path.
- Re-soak: expect `FRONTIER_BEGIN_FLOOR_YIELD` where the old log showed
  `FRONTIER_BEGIN_FLOOR load failed` + `BATTLE_SIM_HOLD armed`.
