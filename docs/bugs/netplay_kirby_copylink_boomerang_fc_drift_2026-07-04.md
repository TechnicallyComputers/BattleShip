# Netplay Kirby CopyLink boomerang + pass-platform FC @600 (`figh` only)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Session:** soak2 `908190465` (Linux guest + Android host, Link P0 / Kirby P1)

## Symptoms

- After first-round FC ordering fix (`1570738194`): `FRAME_COMMIT_STATE_DIVERGE validation=600 snap_tick=599` with **`figh` only** — `fc_rng_div=0`, world/item/eff match, inputs MATCH.
- Both fighters: same `status` / `motion` / `anim_hash` cross-peer at snap 599, but **`topn_tx` / `topn_ty` diverge** (~224 units on P0 Link, smaller on P1 Kirby).
- P0 Link: status **41** (`DamageN2`), motion 35.
- P1 Kirby: status **192** / motion **167** — CopyLink boomerang window (after `CopyLinkSpecialN` @480).
- Live weapon hash at 599: `wpn=0x0A644835` (CopyLink boomerang active).
- Recovery: `FRAME_COMMIT_INPUT_AGREE_REANCHOR last_agreed=480` (`wpn`-only `LOAD_HASH_DRIFT` on re-anchor, accepted).

## Root causes

### 1. Pass-platform hardening scope too narrow

First-round fix only re-anchored Squat/SquatWait/Pass/GuardPass on `MAP_VERTEX_COLL_PASS`. Soak `908190465` had Link and Kirby in **Wait/Turn/Dash** on pass floors (`floor_flags=0x4000`) for ticks 481–586 — outside that scope — so stale `pos_prev` vs TopN could still fork translate before FC @600.

**Fix:** Broaden pass-platform coll harden / load refresh / capture mirror to **any grounded fighter on `MAP_VERTEX_COLL_PASS`**, not status-specific.

### 2. Weapon forward-sim cross-ISA drift (CopyLink boomerang)

Boomerang integrates sin/cos physics each tick (`wplinkboomerang.c`). Snapshot quantize runs on capture/load, but forward sim had no per-tick weapon canonicalize (same class as Castle bumper item drift). Kirby CopyLink boomerang window can pull fighter translate via attachment/collision while weapon vel/translate drift cross-ISA.

**Fix:** `syNetplayQuantizeWeaponPhysics` + `syNetplayCanonicalizeWeaponSimState` + `syNetplayCanonicalizeActiveWeaponsForNetplay`, wired at end of `syNetplayCanonicalizeActiveFightersForNetplay()` (forward + resim paths that already call fighter canonicalize).

## Diagnostics

`SSB64_NETPLAY_KIRBY_COPYLINK_TRACE=1` — per-tick log from `syNetplayTraceKirbyCopyLinkBoomerangTick` (Kirby CopyLink boomerang scope): TopN bits, boomerang translate/vel. Combine with `SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=1` on both peers to bisect first divergent tick.

## Files

| File | Change |
|------|--------|
| `port/net/sys/netplay_sim_quantize.c` | Broaden pass scope; weapon canonicalize; Kirby trace |
| `port/net/sys/netplay_sim_quantize.h` | Exports |
| `port/net/sys/netrollbacksnapshot.c` | Broaden load refresh + capture mirror |
| `decomp/src/sc/sccommon/scvsbattle.c` | Pre-sim harden comment |
| `port/net/sys/netpeer.c` | Kirby trace hook in sim tick trace |
| `docs/bugs/netplay_frame_commit_pass_platform_fork_2026-07-04.md` | Note scope broadening |

## Verification

Rebuild netmenu + offline; re-run soak2 Link vs Kirby CopyLink cross-ISA. Expect FC @600 pass (no `topn_tx`/`topn_ty` fork at snap 599).
