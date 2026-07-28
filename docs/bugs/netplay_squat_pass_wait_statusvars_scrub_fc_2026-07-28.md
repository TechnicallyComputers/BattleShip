# Netplay — Squat `pass_wait` scrub → Pass vs SquatWait FC (inputs MATCH)

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host, Dream Land Ness ditto, seed `1327280249`  
**Related:** [turn lr_dash scrub](netplay_turn_lr_dash_statusvars_scrub_synctest_2026-07-26.md), [jumpaerial scrub](netplay_jumpaerial_statusvars_scrub_synctest_2026-07-19.md), [exclusive frontier poison](netplay_light_exclusive_frontier_poison_load_2026-07-28.md), [pass platform fork](netplay_frame_commit_pass_platform_fork_2026-07-04.md)

## Symptom

- `FRAME_COMMIT_FIGHTER_SLOT_DIVERGE` with `inputs_agree=1` (digest MATCH).
- P0 (or P1) **Pass(33)** vs **SquatWait(29)** (also motion 27 vs 23, `ga` 1 vs 0, `tap_stick_y` `0xFE` vs small).
- SoftLipPhase / `floor_edge_skip` ambient on Dream Land pass floors — not the durable writer.
- FC@961 and FC@1144: peers **matched** through end-of-Squat (`fhash_light`/`fhash_full` equal), then forked on the next forward tick after a **light** resim storm on the other player.
- FC recovery / deepen often re-converged to Pass later; session ended on `VS_SESSION_END`, not PEER map kill.

## Root cause

`ftCommonPassCheckInterruptSquat` arms `status_vars.common.squat.is_allow_pass` + `pass_wait = FTCOMMON_SQUAT_PASS_WAIT` (3). `ftCommonSquatCheckGotoPass` decrements `pass_wait` each interrupt; at 0 → `nFTCommonStatusPass` (burns `tap_stick_y` to MAX → FC field `0xFE`).

Ring save runs `syNetRbSnapScrubInactiveStatusVarsInBlob`. Squat family had **no** early-return, so `memset` of `attackair` / `dead` / `rebirth` **aliased and zeroed** the live squat overlay on every save.

| Peer | Path | Effect |
|------|------|--------|
| First-pass (often Android remote for P0) | Live squat keeps armed `pass_wait` | GotoPass → **Pass** |
| Light resim host (Linux P1 corrections) | Load scrubbed Squat blob → `is_allow_pass=0` | Re-arm late or miss window → AnimEnd **SquatWait** |

`fhash_light` / `fhash_full` did **not** fold squat allow/wait, so end-of-tick hashes still matched while the Pass arm was already poison in the ring.

Same scrub class as Turn `lr_dash`, JumpAerial `ja_*`, Damage `hitstun_tics`.

## Fix

`PORT && SSB64_NETMENU`:

1. **`syNetRbSnapScrubInactiveStatusVarsInBlob`** — early-return for `nFTCommonStatusSquat`…`SquatRv` (preserve captured squat overlay).
2. **`syNetSyncHashFighterStructLight`** — fold `is_allow_pass`, `pass_wait`, `unk_0x8` via `ftStatusVarsSquat()` so `FIGHTER_LIGHT_ONSET` can see overlay skew before status fork.

## Superseded by C2a (2026-07-28)

Approach **C2a — tagged exact snap** replaces the scrub denylist wholesale:

- `SYNetRbSnapFighterBlob` gains `s16 status_vars_overlay` (ownership-table tag from `syNetplayStatusVarsExpectedOverlay`).
- Capture copies the live union **verbatim** and sets the tag; the netmenu build no longer runs the overlay `memset`s at all (scrub stays only in the offline/PORT build).
- `syNetRbSnapHashFighterBlobLight` now mirrors the live squat folds (`is_allow_pass` / `pass_wait` / `unk_0x8`), so blob-vs-live light lockstep holds on Squat too.

The scrub early-returns (this one plus Landing / Turn / JumpAerial / KneeBend / Damage / character arms) are retired together; this document stays for the diagnosis history. Structural replacement lives in `docs/refactor/ftstatusvars_overlay_map_2026-06-02.md`.

## Verify on re-soak

- Stick-down on Dream Land soft platforms through light GGPO storms: no Pass vs SquatWait FC with `inputs_agree=1`.
- After light Finish mid-Squat, ring blob / live should keep non-zero `pass_wait` when armed (witness via fhash change if overlay skews).
- SoftLipX `floor_edge_skip` may still log; should not correlate with durable figh FC of this shape.
