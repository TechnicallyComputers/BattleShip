# Netplay: post-resim exclusive tick stuck at target-1 (wire/hr cap) → +1 phase skew → PEER_SNAPSHOT_DIVERGE — 2026-07-15

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-15  
**Session:** `1133978048` seed `2904404870` (Android client ↔ Linux host, Dream Land, early stick ramp)

## Symptom

scan-drift PASS. Host (Android) UNSTABLE `PEER_SNAPSHOT_DIVERGE` @load ~425; guest (Linux) STABLE soft recovery. Three span-2 stick GGPOs OK, then diverge. max_sim ~426.

Fail-closed partitions at load **425**: figh / anim / map / cam diverge; world / rng / item / wpn match. `inputs agree through load` → state deepen exhaust.

## Root cause

After epoch-3 resim (`load=418 mismatch=419 target=421`):

| Peer | `POST_RESIM_LIVE` |
|------|-------------------|
| Android | `sim=420 target=421` **hr=420** |
| Linux | `sim=421 target=421` hr=424 |

Both replayed/saved 419–420 with matching post digest (`mph=0x34A36CEE`). Android’s last resim `AdvanceAuthoritativeSimTick` was **blocked by wire/hr caps** (`RollbackSimAdvanceAllowed`), so `GetTick` stayed at **target-1**.

Then:

1. `resolved_through = target` (421).
2. Live re-sims tick 420 on already-completed-420 state → physics of 421.
3. `AfterBattleUpdate` **skips save** (`completed_tick < resolved_through`).
4. Next live tick saves as **421** with physics of **422**.

Evidence: from 421 onward `Android[t] == Linux[t+1]` for map + pupupu Wait (`ww`/`blink` skip one); JumpAerial onset Android@421 vs Linux@422. Incidental stick GGPO @426 only discovered the fork.

Not Open→Blow (still Wait). Same class of exclusive-frontier contract as [`netplay_synctest_post_resim_target_save_gap_2026-07-13.md`](netplay_synctest_post_resim_target_save_gap_2026-07-13.md), but exit tick misaligned rather than missing save.

## Fix

Under `PORT && SSB64_NETMENU`:

1. **`syNetInputRollbackSimAdvanceAllowed`** — while resimulating, return TRUE (still honor `BATTLE_SIM_HOLD`). Wire/hr caps are live-pacing only.
2. **`AdvanceResimBudget` completion** — if `GetTick != target`, pin `syNetInputSetTick(target)` and log `POST_RESIM_EXCLUSIVE_TICK_PIN` before `FinishForwardResim`.

## Verify

Re-soak early stick-ramp Dream Land through 3+ span-2 GGPOs into JumpAerial:

- Both peers `POST_RESIM_LIVE sim=T target=T` (no follower `sim=T-1`)
- Matched `pupupu_ground` / JumpAerial onset after promote
- No map+figh `PEER_SNAPSHOT_DIVERGE` a few ticks later with inputs agreeing

Follow-up (2026-07-16): pin can be correct (`sim=T`) while the first live `SavePostTick(T)` still captures post-(T-1) if PeerUpdate finishes resim mid-FuncUpdate without gcRunAll — [`netplay_post_resim_live_save_without_battle_map_skew_2026-07-16.md`](netplay_post_resim_live_save_without_battle_map_skew_2026-07-16.md).
