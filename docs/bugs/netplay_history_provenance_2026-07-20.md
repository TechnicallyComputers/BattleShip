# History provenance — gameplay-authoritative mint (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `67923985` seed `3639329622` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** STATUS_FORK@434 → SEAL poison@436 `(45,6)` → PEER@490 / RESIM_STICK@494

## Symptom

| Signal | Detail |
|--------|--------|
| Earliest | `STATUS_FORK@434` P0 WalkMiddle(12) vs Turn(18) — prediction lag on stick flip |
| Gap | Linux never `LOCAL_PUBLISH`@436; never transmitted@436 |
| Seal | `SEAL_PACK player=0 tick=436 src=auth_history sx=45 sy=6` (== live `@439`) |
| Cascade | Android INTENT/REFUSE kept `(64,21)`; `REPLACE_NEWER` + GGPO forced `(45,6)` |
| Prior patch | `HISTORY_AUTH_FREEZE` / `LATCH_REFUSE_PAST` fired; no wire-resend / gap-restage / load-hash drift |

## Root cause (architectural)

Immutable History solved **mutation after mint**. It did not define **who may mint**.

`auth_history` meant only:

```c
valid && !is_predicted && source == Local
```

A gap tick could acquire Local+!predicted History from latch / reconcile / untagged PublishFrame, freeze immediately, and seal as canonical — without any gameplay sample or LOCAL_PUBLISH.

Invariant violated:

> Only gameplay / LOCAL_PUBLISH may mint gameplay-authoritative History. Freeze and seal consume that provenance, not merely Local&&!predicted.

## Fix

| Layer | Change |
|-------|--------|
| Provenance ring | Parallel `sSYNetInputHistoryProvenance[]` (not in `SYNetInputFrame` — replay/seal wire sizeof unchanged): `NONE`, `PREDICTION`, `GAMEPLAY`, `LOCAL_PUBLISH`, `REMOTE_CONFIRMED`, `GAP_HOLD`, `LATCH` |
| First mint diag | `HISTORY_AUTH_FIRST_WRITE` — tick, sim_tick, writer, prov, sx/sy, have_gameplay, have_tx, mint_downgraded |
| Mint gate | `StoreFrame` → History: Local+!predicted without GAMEPLAY/LOCAL_PUBLISH/REMOTE/GAP_HOLD → `HISTORY_AUTH_MINT_DOWNGRADE` to prediction |
| Latch | `LOCAL_PUBLISH_LATCH_REFUSE` — latch never mints authority (not only past frontier) |
| Reconcile | `RollbackReconcileLocalSlotForResim` restages gameplay or transmitted only — never Resolve→latch |
| Freeze / seal | Require GAMEPLAY or LOCAL_PUBLISH provenance; seal still prefers transmitted, then auth History, then gameplay ring, then `SEAL_PACK_GAP_HOLD` |

## Acceptance (re-soak)

Dual-stick both peers:

- Grep `HISTORY_AUTH_FIRST_WRITE` for any gap tick sealed as `auth_history` — writer must be `promote_local` / `publish_frame` with `have_gameplay=1` (or tx-backed)
- Zero `HISTORY_AUTH_MINT_DOWNGRADE` that later appears as `SEAL_PACK src=auth_history` for that tick
- Gap ticks without gameplay/tx seal as `SEAL_PACK_GAP_HOLD` (neighbor), not poison `auth_history`
- Soft recovery OK; no PEER deepen rooted at a never-published local gap

## Related

- [`netplay_history_auth_immutable_2026-07-20.md`](netplay_history_auth_immutable_2026-07-20.md) — freeze / append-once (necessary, not sufficient)
- [`netplay_wire_resend_gap_restage_revision_2026-07-20.md`](netplay_wire_resend_gap_restage_revision_2026-07-20.md) — egress append-only
