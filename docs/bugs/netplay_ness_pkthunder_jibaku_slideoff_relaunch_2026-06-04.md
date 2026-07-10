# Netplay — Ness jibaku re-launches at full speed when sliding off a ledge (2026-06-04)

**Date:** 2026-06-04
**Status:** Fix shipped (soak pending)
**Area:** `decomp/src/ft/ftchar/ftness/ftnessspecialhi.c` (`ftNessSpecialHiJibakuSwitchStatusAir`), `port/net/sys/netplay_sim_quantize.c`

## Symptom

In netplay, Ness in **grounded** PK Thunder Blast (jibaku) that slides off a stage ledge
"flies off really far" — far past the offline trajectory. Offline play at the same geometry
is normal. Sim hashes can stay matched (it's a real, deterministic velocity, just wrong).

## Root cause

When grounded jibaku (`nFTNessStatusSpecialHiJibaku`) leaves the floor with no wall present,
`ftNessSpecialHiJibakuProcMap` calls `ftNessSpecialHiJibakuSwitchStatusAir` to convert to air
jibaku (`nFTNessStatusSpecialAirHiJibaku`).

**Vanilla** does not modify velocity magnitude here — it sets the status, recomputes
`pkjibaku_angle` from the *current* (decelerated, ground→air transferred) velocity, and resets
jumps. Ness coasts off the ledge with whatever speed remained.

The netmenu path, however, called `syNetplayCanonicalizeNessPKJibakuLaunchState`. For the
air-jibaku status that function **recomputes velocity from scratch**:

```c
fp->physics.vel_air.x = syNetplayQuantizeF32(__cosf(angle) * FTNESS_PKJIBAKU_VEL * lr);
fp->physics.vel_air.y = syNetplayQuantizeF32(__sinf(angle) * FTNESS_PKJIBAKU_VEL);
```

`FTNESS_PKJIBAKU_VEL = 200.0`. So the slide-off conversion **re-launched** Ness at the full
200 launch magnitude instead of preserving his decelerated velocity. Oscillating at a ledge
(slide off → `procmap_pass_cliff` ground snap → slide off again) reloaded 200 on each
ground→air transition, sustaining/extending the launch. Gated by `PORT && SSB64_NETMENU` +
`syNetplaySimQuantizeActive()`, so offline never saw it.

`LaunchState` is only appropriate where the decomp has *just* set `vel_air`/`vel_ground` to
`cos/sin * VEL` immediately before (`ftNessSpecialAirHiJibakuSetStatus`,
`ftNessSpecialHiJibakuSetStatus`) — there it re-derives the identical value and quantizes it.
`ftNessSpecialHiJibakuSwitchStatusAir` is a momentum-preserving *conversion*, not a launch.

## Fix

In `ftNessSpecialHiJibakuSwitchStatusAir`, call `syNetplayCanonicalizeNessPKJibakuSimState`
(quantizes the existing `fp->physics`, `pkjibaku_angle`, `pkthunder_pos`, pitch joint) instead
of `...LaunchState` (recomputes velocity). Preserves vanilla momentum while staying
deterministic across peers.

## Verification

1. Netmenu build links/compiles (`ssb64` target).
2. Cross-ISA soak: grounded jibaku sliding off a ledge follows the offline trajectory; no
   full-speed re-launch / extended range; no `figh` divergence at the slide-off frame.
3. Actual launch sites (`SetStatus`) unchanged — initial jibaku distance unaffected.

## Related

- [netplay_ness_pkthunder_jibaku_ledge_snap_2026-06-03.md](netplay_ness_pkthunder_jibaku_ledge_snap_2026-06-03.md)
- [netplay_ness_pkthunder_jibaku_edge_fc_desync_2026-06-01.md](netplay_ness_pkthunder_jibaku_edge_fc_desync_2026-06-01.md)
- [netplay_cross_isa_libm_trig_2026-06-04.md](netplay_cross_isa_libm_trig_2026-06-04.md) — deterministic trig that made removing edge workarounds viable
