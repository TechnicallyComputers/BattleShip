# Intro Wait force-neutral input (mash before GO)

**Date:** 2026-07-12  
**Session:** soak1 post-GO Turn@394 (Android local p1) vs Linux Wait + predicted neutral → GGPO/`load_fail_hold` hang  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Synctest passes (`SYNCTEST_SKIP intro_wait` then `SYNCTEST_OK` at first Go tick). Mashing stick/buttons during Appear/countdown still desyncs immediately after GO: local enters Turn/Dash on first live sample while the peer predicted neutral hold-last, GGPO queues, then epoch/`load_fail_hold` can stall the session.

## Why not “enable synctest during intro”

Synctest already skips `game_status == Wait`. Re-enabling it would exercise Appear/Wait hash fidelity, not HID in the input rings. Both peers can match Wait fighter hashes while wire/gameplay rings still carry mash that only matters at the first Go interrupt.

## Fix

While VS is active and `game_status == Wait`:

1. **Local sample** (`syNetInputBuildLocalFrameFromLatch`) — store/send neutral buttons+stick (feel-0 provisional runway remains, but neutral).
2. **Remote ingest** (`syNetInputSetRemoteInputFromPacket` + gap-fill seed) — coerce wire samples to neutral and clear `last_non_neutral`.

First post-Go HID sample is the first non-neutral authority row. Pair with Episode FSM GGPO arming (`netplay_episode_fsm_ggpo_drop_after_promote_2026-07-12.md`) so genuine post-Go prediction misses still resim.

## Verify

- Mash stick/A during intro; after GO both peers should leave Wait on the **same** first interrupt tick (no Android-only Turn while Linux Wait).
- During Wait: no `LOCAL_PUBLISH` / remote REPLACE with non-zero stick; feel-0 `hr` still advances (neutral provisionals).
- Synctest still `intro_wait` skip through countdown; `SYNCTEST_OK` after Go unchanged.
