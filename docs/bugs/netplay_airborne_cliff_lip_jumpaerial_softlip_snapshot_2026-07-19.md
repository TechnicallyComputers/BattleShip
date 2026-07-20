# JumpAerial CLIFF soft-lip — snapshot sticky latch (2026-07-19)

**Soak:** soak1 session `1328818035` seed `513280960` — Android client ↔ Linux host, Dream Land, Ness/Ness  
**Bucket:** `REPLAY_DETERMINISM`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

| Field | Detail |
|-------|--------|
| FC | `@1039` figh-only, `inputs=MATCH`, peer field `topn_tx` only |
| Status at FC | P1 `SpecialAirHiStart` (232) / motion 207 — rides already-forked TopN |
| PEER | `@1037` JumpAerial (25); figh (+ cam); world/item/rng/map MATCH |
| First X fork | `MpLanding` gut **994** (Y bit-identical; `fline=-1` `fflags=CLIFF` both peers) |

| gut | Android Δ step | Linux Δ step | Δx peers |
|-----|----------------|--------------|----------|
| ≤993 | match | match | 0 |
| **994** | ≈−3.4 | ≈**+15.7** (AdjNew wall push) | −19 |
| 1021→1038 | — | — | **+33.8** locked |

`emergency_restore` @992 then matched TopN through 993 — fork is pure forward-sim at 994.

## Root cause

Same soft-lip family as [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md): AdjNew walls run **before** floor CheckTest; suppress needs PASS|CLIFF residual or sticky latch.

Sticky (`sMPProcessNetplaySoftLipStickyFlags`) was **process-local only** — not in the fighter rollback blob. After load / emergency restore, peers keep frontier-stale latch values while `coll_data` is restored from the blob. Next wall CheckTest then suppresses on one peer and AdjNew-pushes on the other (Y still matched via later floor CLIFF).

Not Ness jibaku launch-dist. Not protocol (`agree_through_load`, input digests MATCH).

## Fix

Under `PORT && SSB64_NETMENU`:

1. Export `mpProcessNetplaySoftLipStickyGet` / `Set` from `decomp/src/mp/mpprocess.c`.
2. Add `soft_lip_sticky_flags` on `SYNetRbSnapFighterBlob`.
3. Capture after `syNetRbSnapCaptureMPColl`; apply after `syNetRbSnapApplyMPColl` (before forward-sim walls), OR-ing snapshotted residual `floor_flags` PASS|CLIFF so a zero latch still arms when the blob end-state was soft-lip.

Offline / non-rollback unchanged. Jibaku harden-snap carve-out unchanged.

## Verify

**Agent:** `cmake --build build --target ssb64 -j 4` (netmenu).

**Human:** Rebuild **AppImage and Android APK**, then Ness JumpAerial along Dream Land CLIFF lip —

- `MpLanding tr_x` matched through gut 994 → SpecialAirHiStart
- No `FRAME_COMMIT_* topn_tx` / PEER figh with Y matched in that window

## Related

- [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md) — invents sticky latch
- [`netplay_airborne_cliff_lip_jumpaerial_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_jumpaerial_fc_drift_2026-07-18.md) — SoftLipEx residual/swept suppress
- [`netplay_stick_latch_resim_fork_2026-07-03.md`](netplay_stick_latch_resim_fork_2026-07-03.md) — same blob-sidecar pattern for process-local latch
