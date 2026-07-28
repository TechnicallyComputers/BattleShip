# Netplay — light-ep MERGE_DEEPEN undercuts hold_last watermark → JumpAerial SoftLip fork

**Date:** 2026-07-27  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android client + Linux host, Dream Land, seed `3846973281`  
**Follow-on to:** [netplay_light_episode_resolved_floor_pred_cap_2026-07-27.md](netplay_light_episode_resolved_floor_pred_cap_2026-07-27.md)

## Symptom

- Transient figh blips heal through ~739; permanent diverge from **741**.
- P0 JumpAerial (status=25) SoftLip: gut=740 matches, gut=741 forks (`topn_x` / `ja_vel_x`).
- FC@**841** `state_diverge=1`, inputs agree, recovery `class=replay_determinism`.
- FC onset points at **740**.

## Root cause

Light ep `mismatch=739 target=741` for p0 stick `(0,0)→(-7,13)`:

| Tick | Android resim input | Linux host |
|------|---------------------|------------|
| 739 | wire `(-7,13)` | LOCAL `(-7,13)` |
| 740 | **hold_last** `(-7,13)` | LOCAL `(-19,31)` |

True wire `(-19,31)` for 740 arrived mid-resim / at complete (`post_pre_promote`), after 740 was already simmed with the guess. SoftLip@741 forked.

PRED_CAP correctly capped `resolved_through→740`, but the deferred that should have rewound 740 was lost:

1. **`CORRECTION_MERGE_DEEPEN mismatch=740→739`** — a deepen pulled deferred mismatch *below* the predicted-replay watermark; `DEFERRED_KEEP_PRED_SPAN` requires `mismatch >= watermark`, so post-match **cleared** the deferred.
2. **`DeferRemoteInputCorrection`** early-returned when `ResimPending==FALSE` even while `IsResimulating` — mid-replay late wire could fail to arm deferred at all (promote-only).

## Fix

`port/net/sys/netrollback.c` (`PORT && SSB64_NETMENU`):

1. **`DEFERRED_RAISE_PRED_SPAN`** — if deferred target still covers ticks past the watermark but mismatch was deepened below it, raise mismatch to the watermark and keep.
2. **`CORRECTION_MERGE_DEEPEN_PRED_HOLD`** — refuse to deepen mismatch below an active hold_last watermark; still widen target.
3. **`DeferRemoteInputCorrection`** — arm while `IsResimulating` as well as `ResimPending`.

## Acceptance

- [ ] Re-soak stick motion into JumpAerial SoftLip: expect `DEFERRED_RAISE_PRED_SPAN` and/or `MERGE_DEEPEN_PRED_HOLD` when late wire races the episode; follow-up light ep at the hold_last tick with wire sticks.
- [ ] No permanent SoftLip X fork on the tick after a light-ep hold_last when the real stick arrives in the same episode window.
- [ ] FC `state_diverge` stays 0 across a comparable Dream Land soak.

## Follow-on (same-day soak `2609295990`)

PRED keep retained the deferred but left `target` past the contiguous wire frontier → hold_last
micro-cascade SoftLip fork from @400. Fixed in
[netplay_light_pred_cap_wire_ready_coalesce_2026-07-27.md](netplay_light_pred_cap_wire_ready_coalesce_2026-07-27.md).
