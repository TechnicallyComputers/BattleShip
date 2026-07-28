# Netplay — light exclusive-frontier poison reload → SoftLip / replay_determinism

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host, Dream Land Ness ditto, soak `2028838966`, seed `3538623210`  
**Related:** [wire-ready clamp](netplay_light_pred_cap_wire_ready_coalesce_2026-07-27.md), [post-resim target save gap](netplay_synctest_post_resim_target_save_gap_2026-07-13.md), [pred-cap resolved floor](netplay_light_episode_resolved_floor_pred_cap_2026-07-27.md)

## Symptom

- SoftLip / physics fork after a **healed** light episode; FC later `class=replay_determinism`, onset seeds ~613.
- `LIGHT_WIRE_READY_CLAMP` correctly shrunk target (e.g. `617→614`); first light resim `load=612 mismatch=613 target=614` matched Android post-resim (`fhash` / TopN agree).
- Next episode: `load=614 mismatch=615` — SoftLip diverges during “matched wire” resim of 615–616.

## Root cause

1. **Hold_last invent** — Linux host predicted P1 WalkFast `sx=61`; Android owner released `61→58→…`. GGPO queued correction at sim 613 (`published sx=61 | remote sx=58`).
2. **Light heal is exclusive** — resim saves `[mismatch, target)` → saves **613**, not **614**. Live is held by `AwaitLiveSimAfterResim` (+ epoch_cap), so first-pass SoftLipX / status at **@614** stays in the ring.
3. **Ring poison reload** — `ResolveLoadTickForSnapshot` / `GetStoredSubsystemHashes` accept any **valid** slot. `MarkLoadUnsafe` alone is insufficient: FindLatestValid / existence probes still pick **614**. Next Begin loads poison → resim of later ticks forks SoftLip even when inputs match.

`floor_edge_skip` SoftLipX on CLIFF is ambient noise; AdjNew SoftLip writers are not the durable fork.

## Fix

`PORT && SSB64_NETMENU`:

1. **`syNetRbSnapshotInvalidateTick(tick)`** — clear `is_valid` + `is_load_safe`; walk `LastCommitted` / `LastLoadSafe` tips back via FindLatest*.
2. **`syNetRollbackFinishForwardResim`** — when a **light** episode finishes, invalidate `completed_target` (exclusive frontier) and log `LIGHT_EXCLUSIVE_FRONTIER_INVALIDATE`.

Do **not** `SavePostTick(exclusive T)` with end-of-`(T−1)` state — wrong semantics for `load=T`.

## Verify on re-soak

- After light Finish with `target=T`, expect `LIGHT_EXCLUSIVE_FRONTIER_INVALIDATE tick=T`.
- Next light Begin should Prefer `load=T-1` (healed), not reload first-pass `@T`.
- SoftLip should not permanent-fork immediately after a wire-clamped light heal of a WalkFast release invent.
