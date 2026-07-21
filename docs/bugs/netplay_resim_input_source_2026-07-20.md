# Remote resim input source selection (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `256718957` seed `2228521424` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** PEER@439 → PHYSICS@442 / RESIM_STICK@442 P1 (`-58` vs `-31`)

## Symptom

| Signal | Detail |
|--------|--------|
| Provenance | Working — `SEAL_PACK_GAP_HOLD`, gameplay/`LOCAL_PUBLISH` first-writes; not History mint poison |
| Earliest hard | `PEER_SNAPSHOT@439` after P0 light lag @436 |
| Stick | Linux STICK@442 P1 `(-31,5)` while Android owner sealed/published `(-58,6)` |
| Late wire | `REMOTE_PUBLISH P1@442 ledger_wire (-58,6)` only **after** `POST_RESIM_LIVE` |
| Confirm lie | `STRICT_INPUT fabricated_confirm` promoted hold_last History to `RemoteConfirmed` + `pred=0` |

## Root cause

History/seal provenance was not the writer. Remote **resim resolve** fell through to hold_last, then this path stamped it as confirmed:

```c
if (GetHistoryFrame(...)) {
    out_frame->source = RemoteConfirmed;  /* even when is_predicted */
    out_frame->is_predicted = FALSE;
}
```

Invariant violated:

> Resim must never consume hold_last / predicted History as confirmed when a sealed or wire-confirmed row for that tick should govern — and must never **promote** prediction to confirmed.

## Fix

| Layer | Change |
|-------|--------|
| Selection | Explicit remote resim hierarchy: sealed → wire → **strict-confirmed History only** → ledger → hold_last (stays predicted) |
| Diag | `RESIM_INPUT_SOURCE` — selected tier + availability flags (`wire/sealed/hist/hist_conf/ledger/in_span`) |
| Assert | `RESIM_INPUT_SOURCE_ASSERT` when `selected=hold_last` and (`in_span` or unconfirmed History present) |
| Hygiene | `store_published_api` normalizes remote human rows to `RemoteConfirmed` + provenance (stops false `MINT_DOWNGRADE`) |

History freeze/provenance left alone (falsified as root for this soak).

## Acceptance (re-soak)

Dual-stick both peers:

- Grep `RESIM_INPUT_SOURCE` around first RESIM_STICK / PEER — `selected=hold_last` with `in_span=1` should be rare; if present, no `fabricated_confirm` for same tick
- Zero `STRICT_INPUT fabricated_confirm` that pairs with hold_last sticks later corrected by ledger
- Prefer `selected=wire` or `selected=sealed` for in-span remote ticks once peer seal/wire admitted
- Soft recovery OK; PEER deepen not rooted in fabricated confirm of T-1 stick

## Related

- [`netplay_history_provenance_2026-07-20.md`](netplay_history_provenance_2026-07-20.md) — mint ownership (prior layer; still valid)
- [`netplay_sealed_resim_load_tick_neutral_invent_2026-07-19.md`](netplay_sealed_resim_load_tick_neutral_invent_2026-07-19.md) — sealed miss invent class
