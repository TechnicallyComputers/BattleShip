# netplay: Fox Firefox X-position frame-commit drift after latch fix

**Status:** defensive hardening + diagnostic added (`PORT && SSB64_NETMENU`), soak pending.

## Symptom

Soak `71785175` still reported `FRAME_COMMIT_STATE_DIVERGE` at validation 600 after the
`FTPlayerInput` latch snapshot fix. The failure is `figh`-only with matching inputs; `world`,
`item`, `rng`, and `eff` all agree.

This is not the previous latch non-idempotency: both resim baselines round-trip exactly, the
tick-480 replay seed blob matches cross-peer, and the session runs to tick 2140 with 15 clean
synctest probes.

## Evidence

At snap tick 599, player 1 is Fox in Firefox travel (`status=234`, `motion=209`):

- `anim_hash` matches cross-peer (`0x8FD9EF0F`), so pose/animation are not the fork.
- `fhash_light` differs (`host=0x12D7437D`, `guest=0x14C2ED0D`).
- Cross-peer field diff is X-axis only:
  - `topn_tx` / `gobj_translate_x` / `j0_tx` / `joint0_tx`: `0xC22099D4` vs `0xC2393394`.
  - `coll_pos_prev_x`: `0xC11CA720` vs `0xC17F0E20`.
  - `tap_stick_x` / `hold_stick_x`: 76 vs 78, downstream from the replay timeline shift.
- Y position, velocities, `anim_vel`, `stick_prev`, status/motion, RNG, and item state match.

The X gap is roughly 6.15 world units after replaying 480 -> 599 from an identical load seed, far
larger than the `1/65536` quantize grid. That points to a discrete physics/collision branch or an
uncovered contraction/libm surface, not harmless sub-ULP drift.

## Current hardening

`CMakeLists.txt` netmenu contraction flags now also cover:

- `decomp/src/sys/matrix.c` defensively.
- Every `SSB64_LIBULTRA_PORT` `gu` helper TU, not just `sinf.c`/`cosf.c`.

The known hot path (`ft/`, `lb/`, `mp/`, `gm/`, `sys/vector.c`, `sys/utils.c`, `sys/interp.c`) was
already compiled with `-ffp-contract=off`; this broadens the remaining shared matrix/helper surface.

## Diagnostic added

With `SSB64_NETPLAY_FOX_FIREFOX_GATE_DIAG=1`, the existing Firefox diagnostic now emits:

```text
SSB64 Netplay: FOX_FIREFOX_TRACE ...
```

The trace logs raw float bits for `top`, `pos_prev`, `pos_diff`, `vel_air`, `vel_damage_air`,
collision masks/angles, Firefox timers, angle, and whether the tick is replaying. It fires at:

- Firefox air launch (`launch_air`)
- travel air physics (`physics_air`)
- travel map entry (`map_enter`)
- floor/end branch (`map_floor_end`)
- collision-adjust branch (`map_coll_adjust`)

If the next soak still fails, compare `FOX_FIREFOX_TRACE` lines across peers to identify the first
tick and field that diverges. If all trace fields match through launch/travel, move the probe one
layer earlier into the damage-knockback status (`status=68` in this soak).
