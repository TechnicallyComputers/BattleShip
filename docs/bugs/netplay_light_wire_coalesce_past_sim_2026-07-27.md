# Netplay — light wire coalesce waits after live already past mismatch

**Date:** 2026-07-27
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)
**Soak:** Android guest ↔ Linux host, Dream Land, seed `1064408286`
**Follow-on to:** [FC join + light wire coalesce](netplay_fc_join_budget_light_wire_coalesce_2026-07-27.md)

## Symptom

After FC join / light coalesce landed, SoftLip still forked into FC:

- Light episode count dropped (~155 → ~14) and FC recovery joined (`LOAD_TICK_NEGOTIATE 538→535`).
- Live `figh` oscillated agree/diverge with each light heal, then **permanent diverge from ~489** through FC@601.
- Fail-closed `PEER_SNAPSHOT_DIVERGE class=replay_determinism` (no hang) — still a SoftLip state fork with inputs agreeing at FC.

## Root cause

`LIGHT_WIRE_COALESCE` deferred span-1 Begin up to 4 pumps while
`DeferredCorrectionBlocksLiveAdvance` held live-cap at `mismatch-1`. That geometry
only helps when **live has not yet reached the mismatch**.

On soak `1064408286`, **100%** of `try_begin_fail stage=light_wire_coalesce` lines had
`sim >= mismatch` (e.g. `mismatch=417 sim=420`). Deferred armed late (SNAP_AGREE /
wire after SoftLip prediction already advanced). Live-cap cannot rewind, so coalesce
waited under an already-wrong SoftLip tip — delaying Begin without any widen benefit.

## Fix

`PORT && SSB64_NETMENU`, `TryBeginDeferredInputMismatch` in `port/net/sys/netrollback.c`:

- Coalesce only when `syNetInputGetTick() < mismatch` (live-cap can still hold).
- When `sim >= mismatch`, skip coalesce and Begin immediately with the span-1 (or
  wire-aligned) target.

## Acceptance

- [ ] Re-soak: no `light_wire_coalesce` waits with `sim >= mismatch`.
- [ ] SoftLip snap storms shorter / fewer permanent forks into FC with `state_diverge=0`,
      or fail-closed without long hang.
- [ ] When deferred arms **before** live reaches mismatch, coalesce still waits with
      live-cap held (prior micro-chase mitigation preserved).
