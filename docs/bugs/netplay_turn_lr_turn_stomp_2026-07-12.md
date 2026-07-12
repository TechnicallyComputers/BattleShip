# Netplay Turn `lr_turn` stomp blocks Turn→Dash (dash-dance)

**Date:** 2026-07-12  
**Soak:** `1378616925` (post Turn anim-end carve-out)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Training lab dash-dances; live VS does not, while peers stay status-matched. `SSB64_TURN_DASH_WITNESS=1`:

| | Training | Netvs (Linux p0) |
|--|----------|------------------|
| `flag1` @ `anim_frame=6` | yes | yes |
| `allow=1` | yes | yes |
| `lr_turn` | always ±1 | **always 0** |
| `did_dash=1` | 33/33 | **0/25** |
| `18→15` | — | **0** |

Dash-out gate is `stick_x * lr_turn >= 56`. With `lr_turn==0` the product is never smash.

Turn entry already showed `lr_dash=±1` (InvertLR) with `lr_turn=0` — write of `lr_dash` survived, `lr_turn` (union **+16**) did not.

## Root cause

`turn.lr_turn` at overlay +16 aliases `attack4.lr` and `fallspecial.is_allow_interrupt`. Under VS (`RollbackSemanticsActive`), that slot is cleared to 0 after / instead of a lasting `TurnSetStatus` write. Training lab does not run rollback semantics and keeps `lr_turn=±1`.

Anim-end harden carve-out for Turn (same day) restored SetFlag1/`allow` but did not fix dash-out.

## Fix

1. **`ftCommonTurnSetStatus`**: capture facing before `SetStatus`/`PlayAnim`; set `lr_turn = (lr_dash != 0) ? lr_dash : -lr_facing` (InvertLR arg is already `-facing`).
2. **`syNetplayHardenTurnLrTurn`**: when `RollbackSemanticsActive` and status is Turn and `lr_turn==0`, repair from `lr_dash`, else from ±`fp->lr` (pre/post allow). Called from Turn update/interrupt and after SetStatus init.

Offline / non-NETMENU: harden stubbed out; SetStatus facing capture is behavior-preserving when `fp->lr` is stable across SetStatus.

## Verify

- Re-soak with `SSB64_TURN_DASH_WITNESS=1`.
- Expect netvs `lr_turn=±1` on interrupt, `did_dash=1` on InvertLR smash turns, `18→15` completions.
- Training unchanged; FC trails still match (both peers harden the same).

## Related

- `docs/bugs/netplay_turn_dash_allow_anim_harden_2026-07-12.md` — SetFlag1 / harden carve (necessary but not sufficient)
- `docs/bugs/netplay_fallspecial_pass_allow_stomp_2026-06-09.md` — same union class (+4 / +16 stomps)
- `docs/refactor/ftstatusvars_overlay_map_2026-06-02.md`
