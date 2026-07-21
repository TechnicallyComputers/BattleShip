# Authoritative published History is immutable (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `1023513151` seed `348693487` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** PEER@567 → PHYSICS_FORK@576; `SEAL_PACK_HISTORY_SUSPECT` ×15; prior wire-resend guards at 0

## Symptom

| Signal | Detail |
|--------|--------|
| Gap | Linux never `LOCAL_PUBLISH`@567 (`566→568`); first-pass `STICK_SAMPLE@567=(79,22)` |
| Rewrite | Before seal pack, `History[567]=(78,27)` (==568) with no `LOCAL_PUBLISH`/`GAP_RESTAGE` |
| Seal | `SEAL_PACK src=history (78,27)`; soft same-intent vs `(79,22)` → no INTENT/REFUSE |
| Cascade | Android hold `(78,27)` through 570; Linux `@570=(-83,-6)` → RESIM_STICK_FORK → PHYSICS@576 |
| Late | Mass `LOCAL_PUBLISH source=latch` rewriting past ticks 567..620 at sim≈630 |

## Root cause (architectural)

Observed HID, prediction/hold, and authoritative published history shared one mutable `sSYNetInputHistory` slot. Multiple writers could change a gap tick after the first non-predicted publish. Seal packed “whatever is in History,” certifying values that were never authoritative samples.

Invariant violated:

> Once a tick is seal-eligible / published non-predicted local, its authoritative input row must be immutable.

## Fix

| Layer | Change |
|-------|--------|
| History ownership | `HISTORY_AUTH_FREEZE`: refuse gameplay-changing overwrite of local non-predicted History (`StoreFrame` + `PublishFrame` restore + `PromoteLocalAuthority`) |
| Latch past | `LOCAL_PUBLISH_LATCH_REFUSE_PAST`: no latch promote for `tick < gameplay_frontier` |
| Seal pack | Read only transmitted → frozen auth History → gameplay → `SEAL_PACK_GAP_HOLD` from last auth/tx neighbor; never latch; `SEAL_PACK_SKIP_NO_AUTH` if none |

Prediction may still overwrite prediction. Remote confirmed keeps its existing write-once / seal-override path (not local freeze).

## Acceptance (re-soak)

Dual-stick both peers:

- `HISTORY_AUTH_FREEZE` may fire under epoch-hold live-ahead; History sticks for that tick must not change after the log
- Zero `LOCAL_PUBLISH source=latch` for past ticks; `LATCH_REFUSE_PAST` instead
- Seal of gap ticks shows `src=auth_history` or `SEAL_PACK_GAP_HOLD`, not a later tick’s smash
- No PEER deepen chain rooted at a gap tick whose History silently changed

## Related

- [`netplay_history_provenance_2026-07-20.md`](netplay_history_provenance_2026-07-20.md) — **who may mint** gameplay-authoritative History (freeze alone is not enough)
- [`netplay_wire_resend_gap_restage_revision_2026-07-20.md`](netplay_wire_resend_gap_restage_revision_2026-07-20.md) — egress append-only (necessary, not sufficient)
- [`netplay_seal_local_intent_physics_fork_2026-07-20.md`](netplay_seal_local_intent_physics_fork_2026-07-20.md) — seal apply/refuse layer
