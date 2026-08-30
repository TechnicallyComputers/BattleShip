# Sync fork chain: cosmetic asymmetry crossing into the game RNG partition

**Soak:** session 1626853358 (2026-08-31 report). Both peers UNSTABLE, FC diverges at
1029 (rng), 1133 (figh), 1143 (figh + input skew), session stopped at 1146.
**Verdict:** the scan tool's `REPLAY_DETERMINISM / genuine cross-ISA determinism failure`
bucket is a mislabel for this case — it is not libm/trig. It is asymmetric effect-shell
RNG consumption surfacing exactly the way the RNG partition was designed to surface it.

## The chain, tick by tick

| tick | event |
|---|---|
| 391 | transient status fork p1 (host 20 / guest 10), GGPO noise, recovered |
| **464** | **cosmetic seed forks** — both Kirbys in SpecialAirHi (258), `effect_attach_restore` churn at 459/464. Per-shell cosmetic rolls diverge because shell existence differs across peers (the known blade-tracking gap) |
| 464–1020 | game rng identical on both peers every tick; cseed permanently diverged (tolerated — cosmetic partition is per-peer by design) |
| 1010+ | p1 Kirby dies; rebirth with halo (`is_effect_attach=1 halo_effect_present=1`) — the halo shares the same attach flag the cutter restore paths churn |
| **1021** | **game seed forks.** figh/world/item/eff still identical for 8 more ticks — the signature of a *presentation-only* consumer rolling the game seed on one peer only |
| 1029 | FC catches it: everything matches except `rng`; inputs identical. `rng_hash_walk` shows both peers reaching the same `seed_after=0xADC2703A` at 1028 — the *state* re-agreed; the fold difference is the 1021 fork carried forward |
| 1133 | the shifted stream feeds a real gameplay roll → figh forks (p1, both peers agree status=259 SpecialAirHiFall) |
| 1143 | downstream input/pairing skew; session stops at 1146 |

## Why this is the designed tripwire, not a mystery

`decomp/src/sys/utils.c` (cosmetic RNG family): forward sim deliberately routes cosmetic
draws through the **shared game seed** "so any asymmetric cosmetic consumption surfaces
immediately in the `rng` partition." Tick 1021 is that mechanism firing. The asymmetric
consumer family is already known: Kirby cutter/halo effect shells whose existence differs
across peers because they are not snapshot-tracked.

The codebase also has the precedent fix for a *specific* consumer:
`syUtilsRandFloatForcedCosmetic` — added for ShockSmall sparks "whose spawn count can
differ across peers after rollback" (docs/bugs/netplay_shocksmall_cosmetic_rng_fc_diverge_2026-07-08.md).
Once the 1021 consumer is named, the same treatment applies — *if* its draw is genuinely
presentation-only. The deeper fix remains the blade/shell respawn-class migration, which
removes the shell-existence asymmetry that feeds all of these.

## Blocker removed: RNG sites now resolve to names

The step trace and hash-walk logged sites as raw truncated return addresses in a PIE
binary (`site=0x7BE0DC38` vs `site=0x96004080`) — not comparable across peers, not
rebasable from the log. This investigation stalled exactly there. Both now also print
`site_name=<symbol>+0x<off>` via in-process dladdr (nearest exported symbol; a static
caller resolves to a neighboring global, and name+offset is still a stable per-path
fingerprint).

## Next capture

On both peers:

```
SSB64_NETPLAY_RNG_STEP_TRACE=1
SSB64_NETPLAY_RNG_STEP_SITE=1
SSB64_NETPLAY_RNG_TRACE_TICK_MIN=0
SSB64_NETPLAY_RNG_TRACE_TICK_MAX=99999
```

Then, at the first `rng` FC diverge, diff the `rng_step` lines for the last agreeing tick
and the forking tick between peers: the extra/missing step's `site_name=` is the
asymmetric consumer. Expect it in the rebirth-halo or cutter effect path.
