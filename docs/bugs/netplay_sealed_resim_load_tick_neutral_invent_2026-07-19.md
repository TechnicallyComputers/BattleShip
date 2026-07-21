# Sealed resim invents stick-neutral on load_tick (outside seal span)

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Session:** soak1 `2104045952` seed `205597418` (Android client ↔ Linux host)

## Symptom

| Signal | Detail |
|--------|--------|
| Kill | `PEER_SNAPSHOT_DIVERGE` @607 figh (`replay_determinism`); map/world/rng/anim agree |
| Physics | `PHYSICS_FORK_ONSET` @608 `topn_tx` CLIFF sticky (1 ULP); SoftLipPhase matched |
| Actor | P1 Kirby `status=234` cliff aerial; `ja_vel=0` |
| Stick | Forward Android ~1 tick ahead of Linux; `RESIM_STICK_FORK` @605–608 |
| Seal | Linux `SEALED_RESIM_LEDGER_SKIP` @607 **sealed=(0,0)** vs **ledger=(43,66)** |

`STATUS_FORK` @393 KneeBend vs Wait recovered (jump-button GGPO). Not the kill.

## Smoking gun

Episode seal span is `[mismatch, target)` — here mismatch=608, target=610 → sealed rows for **608–609 only**.

Correction loads snapshot at **607** and resims. For tick 607:

1. `syNetInputResolveFrame` saw `EpisodeInputsSealed` but `GetSealedFrame(607)` missed (outside span).
2. It **invented stick-neutral** `(0,0)` and returned.
3. `syNetInputPublishFrame` skipped ledger overwrite because `EpisodeInputsSealed` (any tick), so resim applied `(0,0)` while ledger/forward still had `(43,66)`.
4. `SEALED_RESIM_LEDGER_SKIP` logged the disagreement; deepen `PREFIX_FILL` could then poison History with the neutral invent.

SoftLip CLIFF / 1 ULP TopN are consequences of resimming the cliff aerial with a dead stick.

## Root cause

Sealed-episode resim treated “inputs sealed” as “every resolve during the episode uses the seal table or else invent neutral.” Load ticks **before** `mismatch_tick` are not in the seal contract; they must keep confirmed History / ledger.

## Fix (`port/net/sys/netinput.c`)

1. **ResolveFrame sealed miss:** fall through to remote confirmed History, then authority ledger, then (local) wire-locked / published History — do not invent neutral solely because the episode is sealed.
2. **PublishFrame ledger gate:** skip ledger only when `EpisodeTickInSealedSpan(tick)` — out-of-span load ticks may still take ledger.

Offline / non-rollback unchanged.

## Tooling

`scripts/netplay-scan-drift.py`:

- `SEALED_RESIM_LOAD_NEUTRAL` — sealed `(0,0)` vs nonzero ledger (hard signal)
- Compound note prefers this + `RESIM_STICK_FORK` over SoftLipX CLIFF

## Follow-up (2026-07-20)

Soak1 `1876984747` still logged guest `SEALED_RESIM_LOAD_NEUTRAL` during dual dash-dance
(sealed row was `(0,0)` in-span vs nonzero ledger). Remote seal copy now prefers ledger /
confirmed wire before Resolve hold-last; sealed-miss resolve uses hold-last + smash-flip
instead of inventing hard `(0,0)`. See
[`netplay_hold_last_dash_dance_smash_flip_peer_2026-07-20.md`](netplay_hold_last_dash_dance_smash_flip_peer_2026-07-20.md).

## Verify

**Agent:** `cmake --build <netmenu-build> --target ssb64 -j 4`.

**Human re-soak** (same cliff SoftLipPhase / STICK_SAMPLE env):

- No `SEALED_RESIM_LOAD_NEUTRAL` / sealed=(0,0) vs nonzero ledger on load_tick
- In-span `SEALED_RESIM_LEDGER_SKIP` only when both peers agree on seal
- No `PEER_SNAPSHOT` from this path at SoftLipPhase-matched CLIFF lip

## Related

- [`netplay_seal_ledger_resim_stick_fork_2026-07-19.md`](netplay_seal_ledger_resim_stick_fork_2026-07-19.md) — in-span seal vs ledger; this soak is the load-tick follow-up
- [`netplay_kneebend_jump_exit_witness_2026-07-19.md`](netplay_kneebend_jump_exit_witness_2026-07-19.md) — @393 recovered STATUS_FORK (noise)
