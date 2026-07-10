# INPUT_BIND startup cadence vs display-rate gate pumps (2026-06-02)

**Date:** 2026-06-02  
**Status:** Fix shipped (2026-06-02 follow-up: poll-mode ICE recv + idempotent bind path)  
**Area:** `port/net/sys/netpeer.c`

## Symptoms

- Cross-OS automatch (Android client ↔ Linux host): **~1.5s** visible stall after ICE connect while scene 66 staging runs.
- Android logs: `execution hold` at `tick=0` with `bind=0` for **hold≈120**; `hr=0`, `recv=0`; then sudden `input_bind_ack`.
- Linux host: `execution hold hold=1`, `staging_go` and inbound INPUT immediately.
- Reproduced **without** `debug.env` soak and with **`SSB64_NETPLAY_RECONNECT=0`** (rules out mid-game reconnect hold).

## Root cause

`syNetPeerInputBindServiceTransport()` retransmitted `INPUT_BIND` only when `dSYTaskmanFrameCount % 15 == 0`, while `sSYNetPeerExecutionHoldFrames` advances once per **`syNetPeerUpdateBattleGate()`** call from **`HOSTFRAME_GATE_PUMP`** at **display rate** during staging (high refresh on Android).

Bind exchange was therefore **~15× slower** than the exec-hold counter suggested. The client sat in admission **`E`** (`syNetPeerCheckBattleExecutionReady` false) until a late peer `INPUT_BIND` arrived.

`syNetPeerUpdateStageSceneRendezvous()` pumped ingress for `STAGE_SCENE_*` but never drove bind — only gate pumps after `syNetPeerStartVSSession()`.

## Fix

| Change | Purpose |
|--------|---------|
| **`syNetPeerSendInputBindPacket()`** immediately after `syNetPeerInputBindReset()` in **`syNetPeerStartVSSession`** (new session path) | First-flight bind without waiting for taskman cadence |
| **`syNetPeerInputBindServiceTransport()`** on VS re-entry when bind incomplete | Staging → VSBattle idempotent `StartVSSession` still converges bind |
| Retransmit throttle via **`syNetPeerNowUnixMs()`** (default **33 ms**, env `SSB64_NETPLAY_INPUT_BIND_RETRANSMIT_MS`) instead of taskman `% 15` | Align bind RTX with host-frame gate pumps |
| **`syNetPeerInputBindServiceTransport()`** each **`syNetPeerUpdateStageSceneRendezvous()`** tick | Bind during 2.5 s staging GO wait, not only after VSBattle FuncRead |
| Record **`sSYNetPeerInputBindLastSendUnixMs`** on successful send | Avoid duplicate back-to-back datagrams same wall ms |

### Follow-up (2026-06-02) — still held ~120 on Android after first patch

Soak with session `446263849` showed fdsan/queue fix OK but Android client still **`hold=120`**, **`bind=0`**, **`hr=0`** while Linux host cleared bind at **`hold=1`**. Android uses **`libjuice concurrency=poll`**; Linux uses **`thread`**.

| Gap | Fix |
|-----|-----|
| **`syNetPeerRunBootstrap()`** sets **`IsActive`** before **`syNetPeerStartVSSession()`**, so the “new session” **`InputBindReset` + immediate send** block never ran on automatch | On **idempotent** `StartVSSession`, when bind incomplete: **`InputBindReset()`** + **`syNetPeerSendInputBindPacket()`** before **`InputBindServiceTransport()`** |
| **`syNetPeerPumpIngressTransport()`** did not **`mmIcePoll()`** during execution hold (only bootstrap recv and post-ready gate did) | Call **`mmIcePoll()`** before **`syNetPeerReceiveRemoteInput()`** whenever ICE transport is active |

### Follow-up (2026-06-02) — poll-mode HTTPS pause recv blackout

Idempotent bind + **`mmIcePoll()`** alone did not fix **`hold≈120`**. Root cause: **`SSB64_MATCHMAKING_ICE_HTTPS_SERIALIZE=1`** (Android default) pauses libjuice poll during worker **`GET /v1/match/{ticket}`** while ICE is already **`CONNECTING`/`CONNECTED`**. Poll mode skips paused agents for **recv** but **send** still works — client transmits **`INPUT_BIND`**, host acks at **`hold=1`**, client cannot receive until pause depth clears.

| Change | File |
|--------|------|
| **`mmIceEnsureIoResumed()`** — drain stacked **`juice_pause_io`** depth | `mm_ice.c` |
| **`mmIceShouldSerializeMatchmakingHttps()`** — no serialize only when **`COMPLETED`** (not during connect) | `mm_ice.c`, **`mmHttpsRequest()`**, trickle sync, **`POST …/ice`** |
| Drop worker match polls + nested connect-tick HTTPS pause during **`role_ready`** wait | `mm_matchmaking.c`, `mm_ice_automatch.c` |
| Ensure-resume at shutdown, connect resume, connect success, HTTPS unlock, exec-hold ingress pump, idempotent **`StartVSSession`** | `mm_ice_automatch.c`, `netpeer.c`, `mm_ice_reconnect.c` |

See [android_ice_connect_fdsan_2026-05-30.md](android_ice_connect_fdsan_2026-05-30.md) for fdsan/poll context and env bisect matrix.

## Verification (soak)

- Same LAN pair: Android client `execution hold` should stay **≪ 30** frames; `input_bind_ack` within **~100–200 ms** of host after `VS session start`.
- No `SSB64 Reconnect:` / `ICE Reconnect:` during connect when `SSB64_NETPLAY_RECONNECT=0`.

## See also

- [netplay_automatch_startup_pacing_2026-05-26.md](netplay_automatch_startup_pacing_2026-05-26.md) — pre-bind INPUT staging, strict wire lead buffer
- [netplay_cross_os_pacing_symmetry_2026-05-27.md](netplay_cross_os_pacing_symmetry_2026-05-27.md) — `HOSTFRAME_GATE_PUMP`, decouple display/sim
