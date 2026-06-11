# Netplay battle GO skew during resim seal-wait (2026-06-11)

**Soak:** soak1 Linux + Android after DK/Link intro anchor fix; forced mismatch `INJECT_TICK=240`.

## Symptom

Intro resim at tick 240 **completed**. ~145 ticks later, peers diverged at match GO:

- Linux: `hash_transition` world `4E02DD3E → 1D5400F1` at sim **387**
- Android: same world transition at sim **388** (+1)
- Fighter unlock (`figh` partition) one tick later on Android → gameplay desync by FC validation **480**
- Second rollback was **frame-commit recovery** (inputs agreed), not another `FORCE_MISMATCH`

## Root cause

`ifCommonCountdownThread` drives `ifCommonAnnounceGoSetStatus()` from interface GObj thread sleeps (`gcRunAll` in `ifCommonBattleUpdateInterfaceAll`).

While a rollback episode awaits peer baseline / seal rows (`resim_pending`, replay gate closed), live sim is pinned at the load tick but **`scVSBattleFuncUpdate` still ran `ifCommonBattleUpdateInterfaceAll`**. Countdown threads advanced without a matching sim step. Peers that spent different time in seal-wait (initiator vs follower) latched GO on different sim ticks.

Secondary: `syNetSyncOnNetplayBattleGo()` re-armed `battle_go_pending` every live tick after GO, resetting the latch tick each reconcile (clock anchor bug; stock time-limit matches unaffected).

## Fix

1. **`syNetRollbackShouldDeferInterfaceDuringResimWait()`** — skip live `ifCommonBattleUpdateInterfaceAll` while `resim_pending && !baseline_gate_open`. `scVSBattleFuncUpdate` also returns before live battle sim during seal-wait (prevents `LOAD_SLOT_LIVE_DRIFT`). Forward resim still uses `scVSBattleFuncUpdateBattleSimOnly` + interface.
2. **`syNetSyncOnNetplayBattleGo()`** — latch battle-clock anchor only once (`battle_go_sim_tick` still unset).
3. **`SSB64_NETPLAY_BATTLE_GO_LOG=1`** — `battle_go_apply` + `world_detail` + optional `defer_resim_seal_wait` lines.

## Verify

Re-run soak1 DK/Link with `BATTLE_GO_LOG=1` and `HASH_TRANSITION_LOG=1`. Expect:

- Same sim tick for `battle_go_apply` on both peers
- Same tick for world `hash_transition` to `1D5400F1`
- No FC `STATE_DIVERGE` at 480 from GO skew (item cosmetic drift may still need separate work)

## Diagnostics for soak

| Env | Purpose |
|-----|---------|
| `SSB64_NETPLAY_BATTLE_GO_LOG=1` | GO apply tick, world_detail, defer_resim_seal_wait during resim wait |
| `SSB64_NETPLAY_HASH_TRANSITION_LOG=1` | Per-partition hash step (already in soak) |
| `SSB64_NETPLAY_PEER_DIVERGE_DETAIL=1` | world scalar breakdown on FC diverge |
| `SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=1` | Cross-peer `sim_state_tick` compare around 385–390 |
| `SSB64_NETPLAY_RESIM_RECONCILE_LOG=1` | Published-history reconcile spans after resim |
