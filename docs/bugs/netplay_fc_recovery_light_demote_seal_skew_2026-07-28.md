# Netplay — light Finish clears FC arm → deferred FC demoted to light → seal tuple skew hang

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host, Dream Land Ness ditto, seed `2117792121`  
**Follow-on to:** [snap_agree mismatch FC escalate](netplay_snap_agree_mismatch_fc_escalate_2026-07-28.md)

## Symptom

Escalation correctly armed off-grid FC@572 with onset **562 in-ring** (`recovery_started=1`, no `ONSET_UNRECOVERABLE`). Then:

| Peer | Begin | Outcome |
|------|-------|---------|
| Linux | `fc_recovery=1` span `561→573` heavy | Seal wait `missing_slots=0x2` |
| Android | `LIGHT_WIRE_READY_CLAMP 573→562` / `owner=local_light` / `fc_recovery=0` | Seal `mismatch=562` |

Mutual `EPISODE_SEAL_ROWS_REJECT reason=stale_episode_tuple` → Linux `RESIM_SEAL_ROWS_TIMEOUT` → host VS stop → guest `VS_SESSION_END`.

## Root cause

1. FC arms `FcStateRecoveryActive` + deferred pending (`561→573`).
2. While deferred waits (`fc_resim_busy`), Android runs an ordinary light GGPO (`574→576`).
3. `OnResimCompleted` treats “completed target ≥ FcStateRecoveryTarget” as FC done and **clears the arm** — even though deferred FC never BeginResim’d (`DeferredStateMismatchPending` still true).
4. Deferred FC then `BeginResim` with arm clear → light classifier → `LIGHT_WIRE_READY_CLAMP` shrinks target to wire-ready exclusive end → seal tuple no longer matches Linux’s heavy span.

## Fix

`PORT && SSB64_NETMENU`:

1. **`OnResimCompleted`** — do not clear `FcStateRecovery` while `DeferredStateMismatchPending` (FC has not started yet).
2. **`BeginResim` light classifier** — also refuse light while deferred FC is pending (blocks new light demotions / steal during the wait).
3. **`TryBeginDeferredStateMismatch`** — re-pin `FcStateRecoveryActive` + mismatch/target immediately before `BeginResim` (deferred flag is cleared just prior; re-pin guarantees the heavy path).

## Verify on re-soak

- After escalate FC diverge: both peers `fc_recovery=1`, same mismatch/target (no `BEGIN_LIGHT` / `LIGHT_WIRE_READY_CLAMP` on that Begin).
- No `stale_episode_tuple` ping-pong between FC seals; prefer seal settle / resim complete over `RESIM_SEAL_ROWS_TIMEOUT`.
- Interleaved light GGPOs while FC deferred must not log-clear the FC arm before deferred Begin.
