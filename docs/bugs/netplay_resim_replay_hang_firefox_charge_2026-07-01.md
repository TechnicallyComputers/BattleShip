# Netplay resim replay hang during Fox Firefox charge (2026-07-01)

**Soak:** soak2 Linux + Android, session `152487374`, `SSB64_NETPLAY_ROLLBACK_INJECT_TICK=520` (`FORCE_MISMATCH`).

## Symptom

Both peers **hard-lock** during resim replay while Fox is in air-Firefox charge:

- Rollback load @519: Fox status **228** (`SpecialAirHiStart`)
- Replay @520: transition to **230** (`SpecialAirHiHold`, charge)
- Hang after tick 521 `launch_delay` decrement
- Watchdog: 3+ s stall in `gcRunGObjProcess` inside `scVSBattleFuncUpdateBattleSimOnly`, called from `syNetPeerHandlePacket` → `syNetPeerReceiveRemoteInput` → `syNetPeerUpdate`

NetSync stayed paired through tick 520 (RNG, hashes). `netplay-scan-drift.py` PASS — this is a **replay scheduling / GObj thread deadlock** class, not hash drift.

## Hypothesis

Resim replay runs **synchronously inside packet ingress** (`syNetRollbackTryOpenResimReplayGate` → `syNetRollbackAdvanceResimBudget` → `scVSBattleFuncUpdateBattleSimOnly` → `ifCommonBattleUpdateInterfaceAll` / `gcRunAll`). Thread-kind interface GObj processes block on `gGCMesgQueue` recv while no producer runs on the ingress stack — classic self-deadlock.

Prior art: [`netplay_battle_go_resim_wait_skew_2026-06-11.md`](netplay_battle_go_resim_wait_skew_2026-06-11.md) deferred live interface during seal-wait, but **forward resim replay still runs full interface** via `BattleSimOnly`.

## Diagnostics added (2026-07-01)

Module: [`port/net/sys/netplay_resim_replay_hang_diag.c`](../../port/net/sys/netplay_resim_replay_hang_diag.c)

| Env | Purpose |
|-----|---------|
| `SSB64_NETPLAY_RESIM_REPLAY_HANG_DIAG=1` | Enable hang/replay trace (default off) |
| `SSB64_NETPLAY_RESIM_REPLAY_HANG_DIAG_VERBOSE=1` | Per-GObj/process spam (very noisy) |

Log prefix: `SSB64 Netplay: RESIM_REPLAY_HANG_DIAG`

**Key lines:**

| Tag | When |
|-----|------|
| `replay_gate_open` | Resim replay gate opens (`ingress`, episode ticks) |
| `replay_tick_begin` / `replay_tick_end` | Each replayed sim tick |
| `battle_sim_only_begin` | `scVSBattleFuncUpdateBattleSimOnly` entry (`gc_mesg_valid`) |
| `gc_proc_thread_recv_wait` | Thread process about to block on empty `gGCMesgQueue` |
| `hang_snapshot` / `hang_gobj` | Emitted by port watchdog on hang detect |
| `fox … launch_delay` | Fox Firefox scope context when defer gate is active |

**Hooks:** `netrollback.c` (gate + replay loop), `netpeer.c` (ingress depth), `scvsbattle.c` (`BattleSimOnly`), `objman.c` (`gcRunGObj` / `gcRunGObjProcess`), `port_watchdog.cpp` (hang snapshot).

## Re-soak pass criteria

With `SSB64_NETPLAY_RESIM_REPLAY_HANG_DIAG=1` on both peers:

1. Last lines before `WATCHDOG HANG` should include `hang_snapshot` with `ingress>0` if nested replay-from-packet is confirmed.
2. `gc_proc_thread_recv_wait` with `mesg_valid=0` implicates thread recv block.
3. `hang_gobj` `proc_func_id` / `func_id` pin the blocking interface thread.

If confirmed, fix candidates: defer `AdvanceResimBudget` out of ingress, or skip/defer interface thread processes during resim replay (mirror seal-wait defer for replay path).

## Related

- [`netplay_fox_appear_firefox_charge_soak2_2026-07-01.md`](netplay_fox_appear_firefox_charge_soak2_2026-07-01.md) — forward-sim determinism resolved (round 6); this hang is a **new bug class**.
- [`netplay_battle_go_resim_wait_skew_2026-06-11.md`](netplay_battle_go_resim_wait_skew_2026-06-11.md) — interface defer during seal-wait only.
