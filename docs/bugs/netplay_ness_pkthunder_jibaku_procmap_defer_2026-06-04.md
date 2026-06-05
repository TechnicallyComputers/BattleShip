# Netplay Ness PK Thunder jibaku ProcMap defer teleport (2026-06-04)

## Symptom

Near a ledge, air jibaku felt like a horizontal **teleport outside the platform**: body
launched down into the wall below, then snapped far left on the next frame. Raw
`joint_translate` (TopN / ji=0) showed:

- Tick 928: X = -6052.998 (Hold, falling)
- Tick 929: X = -5875.661 (~+177, first jibaku physics frame)
- Tick 931: X = -6871.000 (~-1167 snap after defer expired)

Gate `jibaku_trigger fighter=` logged **-6052.998** on tick 929 (pre-physics), so
trimmed triage looked like “X did not change” while the sim had already moved.

## Root cause

1. **Blanket ProcMap defer** — `ftNessSpecialAirHiJibakuProcMap` returned early for
   the full 2-tick weapon-teardown defer (`defer_teardown=1`, `cull_at_tick=931`).
   Physics still integrated launch velocity into geometry with **no wall/ledge
   response**, then the first post-defer map pass corrected ~1k units in one tick.

2. **Launch coupling** — `dist_x`/`dist_y` were computed from root DObj before
   refresh; physics/map use **TopN**. Up to 250u fighter↔head collide box allowed
   jibaku while PK head had orbited far during Hold.

3. **Diagnostics** — `jibaku_trigger` logged before `ftMainPlayAnimEventsAll` and
   before physics; `joint_translate` stripped by default trim excludes.

## Fix (shipped)

| Layer | Change |
|-------|--------|
| **ProcMap** | Remove blanket defer `return`; skip only **ground/cliff** paths during defer; keep wall/ceiling + `CollideWallPhysics`. Log `air_jibaku_procmap_defer`. |
| **Cull** | `PrepareJibakuCoupling` at `CheckCollidePKThunder` (pre-jibaku orphan cull). |
| **Launch** | Vanilla root DObj `dist` (hold-entry anchor refine reverted — caused vertical ground plunge). |
| **Diag** | `hold_tick`; `jibaku_collide` (dist_root/dist_topn at self-hit); `jibaku_launch_dist`; `jibaku_coupling` with `dist=`; trim flags `vertical_plunge`. |

## Files

- `decomp/src/ft/ftchar/ftness/ftnessspecialhi.c`
- `port/net/sys/netplay_ness_pkthunder_gate.c`
- `port/net/sys/netplay_ness_pkthunder_gate.h`

## Verification

1. Reproduce ledge Hold → jibaku soak; raw logs should show gradual wall response on
   ticks 929–930 (no single ~1k X snap at defer expiry).
2. `jibaku_coupling site=post_anim` should match end-of-tick TopN within ~200u of
   launch velocity, not a flat pre-physics `fighter=` row.
3. `weapons=1` (or 0 after cull) at `jibaku_trigger` when possible; no SIGABRT from
   orphan teardown (weapon defer paths unchanged).
