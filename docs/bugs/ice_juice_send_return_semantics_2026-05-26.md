# ICE `juice_send` return semantics vs netpeer byte-count checks — 2026-05-26

## Symptoms

- LAN automatch over ICE with default **`SSB64_NETPLAY_REQUIRE_INPUT_BIND=1`**: endless `execution hold`, `bind=0`, no `input_bind_ack`.
- Same session with **`SSB64_NETPLAY_REQUIRE_INPUT_BIND=0`**: reaches VSBattle (bind gate bypassed).
- Logs: `battle_exec_sync send_fail role=host bytes=28 sent=0 err=9` while immediate `battle_exec_sync recv` and `both_sides_latched_startup` (packets actually sent).
- Misleading **`err=9` (EBADF)** from `syNetPeerOsSocketLastError()` — no game UDP socket when libjuice owns port 7778.

Complements [ice_cross_nat_pair_selection_2026-05-26.md](ice_cross_nat_pair_selection_2026-05-26.md) (control-plane routing over ICE was necessary but not sufficient).

## Root cause

[`juice_send`](../../third_party/libjuice/src/juice.c) returns **`JUICE_ERR_SUCCESS` (0)** on success, not the datagram length:

```c
if (ret >= 0)
    return JUICE_ERR_SUCCESS;
```

[`mmIceSend`](../../port/net/matchmaking/mm_ice.c) passed that through unchanged. [`syNetPeerSendGameDatagram`](../../port/net/sys/netpeer.c) and callers treat success as **`sent == (int)expected_bytes`**:

| Packet | Bytes | Effect when juice returns 0 |
|--------|-------|-----------------------------|
| `INPUT_BIND` | 20 | `sSYNetPeerInputBindSent` never set → strict bind stuck |
| `BATTLE_EXEC_SYNC` | 28 | False `send_fail`; host still sets `host_sent` after call |
| `INPUT` / warmup / session params | variable | Counters / seq bookkeeping skipped until “success” |

## Fix

| File | Change |
|------|--------|
| `mm_ice.c` | `mmIceSend`: `JUICE_ERR_SUCCESS` → `(int)len`; `JUICE_ERR_AGAIN` → `0`; else `-1`. `mmIceLastSendJuiceError()`, `mmIceSendErrorString()`. |
| `netpeer.c` | `syNetPeerLogDatagramSendFailure()` uses juice status on ICE path; `input_bind` / `battle_exec_sync` failure logs. |

## Verification

**Build:** `cmake --build <worktree>/build --target ssb64 -j 4`

**LAN (PC + Android, default gates):**

```bash
SSB64_MATCHMAKING_ICE_VERBOSE=1
SSB64_NETPLAY_INGRESS_DIAG=1
# Do NOT set SSB64_NETPLAY_REQUIRE_INPUT_BIND=0
```

- Both sides: `input_bind_ack`, `bind=1` in `battle_gate_wait` / `execution hold`
- No `battle_exec_sync send_fail` on first host propose (or `send_ok`)
- `control send path=ICE`
- VSBattle, sustained `INPUT recv` / `INPUT send`

**LTE cross-NAT:** Wi‑Fi off on phone; remote path srflx/relay/WAN (not peer `192.168.x.x`); same bind/exec-sync criteria.

## Staging delay (guest ~36s)

Session `30596958`: guest scene 66 `recv=0` / `client_got=0` while host latched ~18s. Likely aggravated by local exec-sync/bind state never acknowledging sends (same return-semantics bug). After this fix, re-test; if delay remains, trace `syNetPeerUpdate()` / `syNetPeerPumpIngressTransport` on automatch staging scene vs `scVSBattleFuncUpdate` exec-hold path.

## Rollback desync (`LOAD_HASH_DRIFT` @240)

Not an ICE transport issue (session `30596958` had symmetric host↔host ICE and working INPUT). Separate track:

- Repro with matched PC/Android builds
- `SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1`
- See [netplay_dk_jungle_effect_pop_desync](netplay_dk_jungle_effect_pop_desync_2026-05-25.md) class of forward-sim / snapshot issues
