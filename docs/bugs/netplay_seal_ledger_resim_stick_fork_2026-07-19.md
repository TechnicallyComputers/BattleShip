# Seal vs ledger stick apply during sealed resim (JumpAerial CLIFF PEER)

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Session:** soak1 `1043859099` seed `1143724347` (Android client ↔ Linux host)

## Symptom

| Signal | Detail |
|--------|--------|
| Kill | `PEER_SNAPSHOT_DIVERGE` @989 figh (`replay_determinism`); no FC |
| Physics | `PHYSICS_FORK_ONSET` @980 `topn_tx` CLIFF sticky; SoftLipPhase `post_phys` |
| Actor | P0 Ness JumpAerial (`status=24`); `ja_drift` matched, `ja_vel_x` forked |
| SoftLipX | No P0 JumpAerial writer rows (floor_edge_skip on P1 only) |

First-pass SoftLip @980 matched. Fork appears on the **post-GGPO@979 resim** pass.

## Smoking gun

During sealed resim of tick 979:

| Peer | Role | `STICK_SAMPLE` @979 |
|------|------|---------------------|
| Android | remote P0 (initiator) | **sx=7 sy=83** (wire / ledger) |
| Linux | local P0 (follower) | **sx=32 sy=72** (sealed row) |

GGPO queued pred `(-10,82)` → remote `(7,83)`. Linux `LOCAL_PUBLISH` @979 was `(7,83)`. Freeze post-input digest matched (`inp=0xA093C799`) — both peers sealed the same table — but **apply** diverged:

1. Android `PublishFrame` for remote slots **replaced** the sealed resolve with authority **ledger** `(7,83)`.
2. Linux local slot has no ledger overwrite → published sealed `(32,72)` (gameplay HID restage preferred by `CopyEpisodeLocalAuthoritySealFrame` over wire-locked History).

Same `ja_drift` @980, different `ja_vel_x` (0 vs ~0.88) → TopN.x fork. SoftLipPhase / JumpAerial quantize are consequences, not the writer.

After resim, Android logged `REMOTE_PUBLISH_SEAL_OVERRIDE` `(7,83)→(32,72)` — seal stomping the correct wire confirm.

## Root cause

Two cooperating holes:

1. **`syNetInputPublishFrame`** (Phase 2 ledger): during sealed resim, remote publish preferred ledger over the episode seal row ResolveFrame already selected. Initiator (remote) and follower (local) therefore applied different sticks despite matching `freeze_post_inp`.
2. **`syNetInputCopyEpisodeLocalAuthoritySealFrame`**: preferred `ResolveLocalAuthority` (gameplay-first) over wire-locked published/transmitted History, so the seal could capture a later HID restage `(32,72)` instead of the `LOCAL_PUBLISH` / wire sample `(7,83)`.

Class family: [`netplay_post_resim_wirelocked_hid_restage_2026-07-13.md`](netplay_post_resim_wirelocked_hid_restage_2026-07-13.md), [`netplay_confirmed_publish_write_once_2026-07-12.md`](netplay_confirmed_publish_write_once_2026-07-12.md) (seal-outranks-write-once contract).

## Fix (`port/net/sys/netinput.c`)

1. **Sealed resim publish:** skip ledger / write-once rewrite when `IsResimulating && EpisodeInputsSealed`. Optional `SEALED_RESIM_LEDGER_SKIP` log when ledger would have disagreed.
2. **Local seal copy:** prefer `TryGetLocalWireLockedSample` (Transmitted / published History) before `ResolveLocalAuthorityFrame`.

Offline / non-rollback unchanged.

## Tooling

`scripts/netplay-scan-drift.py`:

- `RESIM_STICK_FORK` — cross-peer `STICK_SAMPLE` near GGPO / PHYSICS_FORK
- `SEAL_LEDGER_STOMP` — `REMOTE_PUBLISH_SEAL_OVERRIDE` / `SEALED_RESIM_LEDGER_SKIP`

## Verify

**Agent:** `cmake --build build --target ssb64 -j 4` (netmenu).

**Human re-soak** (cliff SoftLipPhase env on both peers):

- GGPO stick REPLACE mid-JumpAerial: both peers `STICK_SAMPLE` match through the sealed span
- No `RESIM_STICK_FORK`; `SEALED_RESIM_LEDGER_SKIP` only when ledger≠seal (and both still agree on seal)
- No `PHYSICS_FORK` / PEER from this path at SoftLipPhase `post_phys` with matched `ja_vel_x`

## Related

- [`netplay_jumpaerial_cliff_softlip_phase_probe_2026-07-19.md`](netplay_jumpaerial_cliff_softlip_phase_probe_2026-07-19.md) — named SoftLipPhase; this soak is the seal/ledger follow-up
- [`netplay_stick_replace_policy_consolidate_2026-07-12.md`](netplay_stick_replace_policy_consolidate_2026-07-12.md)
- [`netplay_post_resim_pl_latch_stick_range_poison_fc_2026-07-17.md`](netplay_post_resim_pl_latch_stick_range_poison_fc_2026-07-17.md)
