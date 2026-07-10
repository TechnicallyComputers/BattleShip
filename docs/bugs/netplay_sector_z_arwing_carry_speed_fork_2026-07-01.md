# Netplay — Fox `figh`/`cam` fork on Sector Z, one tick after resim settles (Firefox air-hold near Arwing deck)

**Date:** 2026-07-01
**Status:** Root cause narrowed, diagnostic added (`SSB64_NETPLAY_SECTOR_ARWING_CARRY_DIAG`), fix pending confirmation
**Area:** `decomp/src/gr/grcommon/grsector.c`, `port/net/sys/netsync.c`

## Symptom

Soak1 session `1389484428`, stage=1 (Sector Z): `FRAME_COMMIT_STATE_DIVERGE [figh]` at tick 600
with **matching inputs** on both peers (genuine cross-ISA determinism failure, not misprediction).
`netplay-trim-logs.py` reported "first sim_state mismatch tick=523" — historically this exact tick
number has been a red herring (see
[netplay_sim_state_trace_pre_quantize_diag](netplay_sim_state_trace_pre_quantize_diag_2026-07-01.md)),
so this needed to be re-verified, not assumed real.

## Investigation

Diffed `sim_state_tick` `figh`/`cam` across every common tick on both peers:

```
518 0x8F09A1EA 0x8F09A1EA MATCH
519 0x4467BA98 0x4467BA98 MATCH
522 0x93EF80FF 0x93EF80FF MATCH
523 0x833628A2 0x4B7F8F83 DIFF   <- first real divergence, not an artifact
524 0x5D1CACD9 0x874F46DF DIFF
```

`world`/`item`/`wpn`/`rng`/`anim`/`eff`/`cseed`/`mph` all matched at every tick in this window —
divergence is isolated to `figh` (fighter hash) and `cam` (camera hash) only. `map_hash_save`
(`hash_map`/`kin`/`ground_fold`) also matched exactly through tick 524, ruling out the ground/deck
DObj tree itself as the diverging object.

Cross-referenced against the resim log: `resim begin epoch=0 mismatch_tick=520 load_tick=519
target_tick=522` ... `resim complete ... load_tick=519 mismatch_tick=520 rollbacks=1` ...
`POST_RESIM_LIVE sim=522 target=522`. **Tick 523 is the first tick simulated live after this resim
settles.** Player 1 (Fox) status is `233` (`nFTFoxStatusSpecialAirHiHold` — charging Firefox
airborne) on both peers at 523, and the `status_trail` sequence for Fox is byte-identical across the
whole match on both peers (232→234→233→...→18 at tick 539), so the eventual "Firefox ended early"
observation the user reported is a deterministic *gameplay* outcome (both peers agree), not a
desync — separate from the network bug tracked here.

`fighter_cargo_diag`/`fighter_cargo_diag2` (`SSB64_NETPLAY_FIGHTER_CARGO_DIAG`) was only enabled on
the android peer this soak, so the exact diverging field inside Fox's `FTStruct` could not be
directly diffed. However: Fox's `coll_data.floor_line_id` is `1` at tick 523 even though `ga=0`
(airborne) — Fox was standing on Sector Z's Arwing-deck line 1 immediately before this window and
`floor_line_id` is not reset on leaving the ground. This activates the netplay-only conditional fold
in `syNetSyncHashFighterStructLight` (`port/net/sys/netsync.c`):

```c
if ((gSCManagerBattleState->gkind == nGRKindSector) && is_arwing_line_active && is_arwing_z_near &&
    (fp->coll_data.floor_line_id == 1))
{
    h = fold(vel_speed.x/y/z, floor_flags);   // folded into fhash_light -> figh
}
```

`vel_speed` for a fighter on line 1 is carried from the deck's own frame-to-frame motion via
`gMPCollisionSpeeds[1]`, set in `grSectorArwingReconcileDeckYakumonoFromFlightTree()`
(`decomp/src/gr/grcommon/grsector.c`) — including the netplay-only branch added for the
"grounded fighters slide hundreds of units during resim" bug: when the flight-tree-derived deck
position and the live yakumono translate differ by more than 45 units (a **discrete threshold**,
not a rounding operation), it snaps translate to the target and hard-zeroes
`gMPCollisionSpeeds[1]`; otherwise it leaves the position/speed alone for the vanilla per-frame
derivation to handle. `syNetplayQuantizeMPCollData` does quantize `vel_speed` every tick, but
quantization only unifies values that are already *epsilon-close* — it cannot reconcile a case
where one peer's `dx²+dy²` crosses the 45² threshold (via tiny cross-ISA FP differences in the
flight-tree position feeding `pos`) one tick before or after the other peer's, since that produces
a **hard 0.0 vs. a genuine nonzero carry value**, not a rounding-distance difference. If Fox was
carried by (or had just left) that platform at the exact tick this snap fires asymmetrically, his
`coll_data.vel_speed`/resulting position would genuinely differ cross-ISA at the source, which then
persists forward (feeding `pos_diff` on subsequent ticks) even after the deck's own translate
re-converges — matching the observed pattern (`kin`/`ground_fold` match again quickly, `figh`
stays diverged).

This is the same lead flagged as unresolved in
[netplay_fox_appear_firefox_charge_soak2](netplay_fox_appear_firefox_charge_soak2_2026-07-01.md)
Round 5 ("focus the next pass on Fox `SpecialHiHold` on Sector Z with the Arwing deck active —
check `coll_data.vel_speed`/`floor_flags` and the deck-derived yakumono geometry feeding it"), now
reproduced with a corrected (post-quantize) trace and narrowed to a specific tick/mechanism instead
of a stage-wide guess.

## Diagnostics added (this pass)

- `decomp/src/gr/grcommon/grsector.c`: `SSB64_NETPLAY_SECTOR_ARWING_CARRY_DIAG=1` logs every
  evaluation of the 45²-threshold snap check (`arwing_carry_snap tick=... fired=0/1 dist_sq=...`
  plus old/new translate and speed when it fires). Netmenu-only (`PORT && SSB64_NETMENU`), no-op
  offline.
- Re-run `SSB64_NETPLAY_FIGHTER_CARGO_DIAG=1` (optionally with `_TICK_MIN=515`/`_TICK_MAX=530`) on
  **both** peers this time (it already exists; it was asymmetric last soak) to get Fox's raw
  `vspd_x/y/z`/`floor_flags`/`pos_*` bits on both sides for a direct diff at tick 522/523.

## Next step

Re-soak reproducing Fox charging/using Firefox while on or just after leaving the Sector Z Arwing
deck (near a resim), with both diagnostics enabled on **both** peers. If `arwing_carry_snap`
fires on one peer and not the other (or one tick apart) at/around tick 522–523, that confirms the
threshold-crossing race as the root cause; the fix is to make the snap decision itself
cross-ISA-safe (e.g. quantize the `dist_sq` comparison inputs before the threshold check, or widen
the netplay-only branch to always hard-set translate+speed from the flight-tree pos while
`is_arwing_line_active && is_arwing_z_near`, removing the discrete branch entirely) rather than
attempting to patch the resulting fighter-side drift after the fact.
