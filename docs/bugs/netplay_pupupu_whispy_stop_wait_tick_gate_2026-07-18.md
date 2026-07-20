# Netplay: Whispy Stop→Wait mouth leftover → +1 wind_wait → map-only PEER_SNAPSHOT_DIVERGE — 2026-07-18

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-18  
**Session:** soak1 seed `2238087760` (Android client ↔ Linux host, Dream Land / Ness jibaku mash)

## Symptom

Felt like “same jibaku patterns” then unexplained desync. scan: no FC figh diverge; all `jibaku_launch_dist` matched. Guest (Linux) UNSTABLE **map-only** `PEER_SNAPSHOT_DIVERGE` @load **1925**:

| Partition | Match? |
|-----------|--------|
| figh / world / item / rng / anim / wpn / cam / kin | match |
| map / ground_fold | **diverge** |

## Root cause

Whispy **Stop→Wait** (`status` 5→1) skewed by **1 tick**:

| Tick | Android | Linux |
|------|---------|-------|
| 1845 | Stop `ww=0` | Stop `ww=0` |
| **1846** | still Stop | **Wait `ww=981`** |
| 1847 | Wait `ww=981` | Wait `ww=980` |

Permanent Android `wind_wait = Linux + 1` until baseline deepen exhaust → fail-closed. Fighters were Fall/Wait — last jibaku was @1443.

`grPupupuWhispyUpdateStop` gated Wait on mouth `map_gobj[1]` anim end (`syNetplayMapGobjAnimFrameEnded`). Close leftovers are not in the Pupupu ground blob; near-zero snap is insufficient for a full-tick leftover skew. Same class as Open→Blow tick gate.

## Fix

Under `syNetplayRollbackSemanticsActive()`:

1. When Close mouth `PlayAnim` runs during Stop, arm `whispy_wind_wait` from `ceil(map_gobj[1]->anim_frame)` (same arming path as Open).
2. `UpdateStop` decrements that wait and fires Wait at 0 (same Wait body / gameplay wait-duration roll as vanilla).
3. Pending `mouth_status == Close` returns early until `UpdateGObjAnims` arms the wait.
4. Offline / non-rollback keeps the anim-end gate.

## Verify

Re-soak Dream Land through a Whispy Blow→Stop→Wait (stick mash / jibaku OK):

- Matched Wait onset (`pupupu_ground` status / `wind_wait` reseed)
- During Stop, `wind_wait` counts Close remaining (not stuck at 0)
- No map-only `PEER_SNAPSHOT_DIVERGE` after Wait

## Related

- [`netplay_pupupu_whispy_open_blow_tick_gate_2026-07-15.md`](netplay_pupupu_whispy_open_blow_tick_gate_2026-07-15.md) — Open→Blow tick gate (template)
- [`netplay_pupupu_whispy_blink_positive_hold_map_diverge_2026-07-15.md`](netplay_pupupu_whispy_blink_positive_hold_map_diverge_2026-07-15.md) — blink +1 map-only
- [`netplay_post_resim_live_save_without_battle_map_skew_2026-07-16.md`](netplay_post_resim_live_save_without_battle_map_skew_2026-07-16.md) — post-resim +1 map (different onset)
