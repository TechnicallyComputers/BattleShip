# Netplay — FC late mint skipped on light-resim grid boundaries (ONSET_UNRECOVERABLE kill)

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host — session `190673804` / seed `4356509`; reconfirm
`session=1420331424` / seed `2087355410` (soak1 00:27, Dream Land Ness ditto,
`fc_validation_ticks=120`, `rb_snap=128`)  
**Follow-on to:** [FC onset older than ring hard fork](netplay_fc_onset_older_than_ring_hard_fork_2026-07-27.md),
[FC late mint / pairing grid](netplay_frame_commit_pairing_grid_2026-07-26.md)

## Symptom

```
FRAME_COMMIT_STATE_DIVERGE validation=721
  inp_local == inp_peer
  figh ONLY
FRAME_COMMIT_INPUT_AGREE_ONSET … onset=459
FRAME_COMMIT_INPUT_AGREE_ONSET_UNRECOVERABLE … onset_load=458 min_load≈594
  ring_cap=128 — fork older than ring; fail closed
PEER_SNAPSHOT_DIVERGE load_tick=720 class=replay_determinism
FRAME_COMMIT_DIAG sent≈2–3 … recovery_started=0
```

Zero `FRAME_COMMIT_LATE_MINT_*` lines even after binaries contained the late-mint strings.
Android log: `resim begin … load_tick=480` (exact FC grid) with no arm.

## Root cause

1. **Wrong call site (primary).** Light/heavy resim completes ticks via
   `scVSBattleFuncUpdateBattleSimOnly`. Live `scVSBattleFuncUpdate` early-returns while
   `syNetRollbackIsResimulating()` **before** `syNetPeerFrameCommitAfterCompletedSimStep`.
   Arming only inside AfterCompleted's resim branch was therefore **dead code** during real
   resim. Grid ticks (480 / 600 / …) advanced with no FC mint and no `LATE_MINT_ARM`.

2. **Ring vs delayed FC.** First post-storm live compare (e.g. validation 721) sees shared
   onset already older than `ring_cap` → July-27 `ONSET_UNRECOVERABLE` refuse short
   RING_CLAMP → hard kill with `recovery_started=0`.

3. **Late-mint wait was too timid (secondary).** Hardcoded age drop at **96** and "wait while
   deferred" with no force window could drop a pending mint still in a 128-deep ring.

## Fix

`PORT && SSB64_NETMENU`:

1. **`syNetPeerFrameCommitNoteResimCompletedSimStep`** — called from
   `scVSBattleFuncUpdateBattleSimOnly` after `AfterBattleUpdate` / before Advance (GetTick
   still names the completed boundary). Arms `LATE_MINT_ARM … resim skipped grid boundary`.

2. AfterCompleted resim-arm kept as belt-and-suspenders if any path still reaches it.

3. **Late depth = `ring_cap - 2`**; **`LATE_MINT_FORCE`** when deferred still blocks but age
   exceeds `max_late / 2`. Deferred-cover live path shares `syNetPeerFrameCommitArmLateMint`.

## Acceptance

- [ ] Re-soak: expect `LATE_MINT_ARM … resim skipped grid boundary` when resim crosses
      `completed % fc_interval == 0` (e.g. 480/600); and/or `LATE_MINT_FORCE` on live resume.
- [ ] `recovery_started≥1` on input-agree figh diverge while onset still in ring (no immediate
      `ONSET_UNRECOVERABLE` solely because mid-storm grids were eaten).
- [ ] When onset truly predates the entire ring with no missed mid-grid FC: still
      `ONSET_UNRECOVERABLE` fail-closed (July-27 contract unchanged).
- [ ] No return of FC pairing skew (`compared=0`) from late mint tick_anchor rules.

## Related

- [`netplay_fc_onset_older_than_ring_hard_fork_2026-07-27.md`](netplay_fc_onset_older_than_ring_hard_fork_2026-07-27.md)
- [`netplay_light_input_episodes_2026-07-27.md`](netplay_light_input_episodes_2026-07-27.md)
- Portable contract extract (not causal): [`docs/netplay_input_contract_portable.md`](../netplay_input_contract_portable.md)
