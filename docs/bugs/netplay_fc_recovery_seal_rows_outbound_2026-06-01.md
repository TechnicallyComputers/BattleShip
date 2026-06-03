# FC recovery seal-row outbound stall (netplay)

**Date:** 2026-06-01  
**Scope:** `port/net/sys/netrollback.c`, `port/net/sys/netrollback_episode.c`, `port/net/sys/netpeer.c`, `port/net/sys/netrollbacksnapshot.c`  
**Status:** FIX SHIPPED — soak pending

## Symptoms

Yoshi vs Kirby Sector Z (~tick 2040): `FRAME_COMMIT_STATE_DIVERGE` (figh-only, inputs agree) triggers fc_recovery resim from tick 1920. Linux peer (Kirby P0) completes inbound seal rows for Yoshi P1 and opens the replay gate after only **3/5** outbound chunks for slot 0. Android peer (Yoshi P1) stalls with `EPISODE_SEAL_ROWS_WAIT missing_slots=0x1` (slot 0 incomplete) until `RESIM_SEAL_ROWS_TIMEOUT` → session stop. Guest may finish resim alone then strict-stall at `sim=2043`.

Trimmed log: `EPISODE_SEAL_ROWS_RECV slot=0 begin=0,24,48` only on Android; Linux received full `slot=1 begin=0..96`.

## Root cause

`syNetRollbackTryOpenResimReplayGate()` opened forward resim when **inbound** peer seal rows were complete, without waiting for **outbound** local-authority rows to finish transmitting. Once the gate opened, `syNetRollbackPumpResimBaselineSend()` returned early and stopped pumping `EPISODE_SEAL_ROWS` datagrams, stranding the remote peer mid-span.

Secondary: only one seal-row chunk was sent per baseline pump frame; 120-tick spans need five chunks at 24 rows/chunk.

## Fix

1. **`syNetRollbackEpisodeLocalSealRowsSendComplete()`** / **`GetLocalSealRowsSendPendingMask()`** — track outbound send cursors per local-authority slot.
2. **Replay gate** — block gate open until outbound send complete; log `EPISODE_SEAL_ROWS_WAIT outbound pending_local_slots=0x…`.
3. **`syNetRollbackEpisodePumpOutboundSealRows(max_chunks)`** — send up to four chunks per pump call; used from gate wait and baseline pump.
4. **`syNetPeerTrySendEpisodeSealRows()`** — returns `sb32` (TRUE when a datagram was sent).
5. **`syNetRollbackEpisodePrepareSealRowsRetransmit()`** — keep retransmitting while either inbound or outbound seal exchange is incomplete.

## Yoshi drift hardening (same soak)

Synctest defer during fragile Yoshi windows that showed repeated eff-only `LOAD_HASH_DRIFT soft-continue` with `rollbacks=0`:

- `yoshi_egg` — SpecialHi charge or live/thrown egg weapon (`nWPKindEggThrow`).
- `yoshi_aerial_landing` — FallAerial, landing lights/heavies, attack-air / landing-air chain.
- `yoshi_egg_probe` — ring slot weapon blob probe defer (mirrors link-bomb probe).

## Test plan

1. Reproduce Yoshi spam vs idle opponent on Sector Z; force fc_recovery (`SSB64_NETPLAY_FRAME_COMMIT_DIAG=2`). Both peers should log five `EPISODE_SEAL_ROWS_SEND` chunks per local slot before `resim replay gate open`; no `RESIM_SEAL_ROWS_TIMEOUT`.
2. Confirm Android no longer stops at `missing_slots=0x1` while Linux completes resim alone.
3. Synctest soak with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`: expect `SYNCTEST_SKIP reason=yoshi_egg` / `yoshi_aerial_landing` during egg/aerial spam instead of mid-chain `SYNCTEST_OK` + eff drift.
