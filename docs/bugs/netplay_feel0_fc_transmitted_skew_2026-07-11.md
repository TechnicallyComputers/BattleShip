# Feel-0 FC input digest skew (Transmitted over Gameplay)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-11  
**Session:** `1989925098` seed `1900646176` (Android client ↔ Linux host, D=2)

## Symptom

`netplay-scan-drift` / sync-report:

```text
FRAME_COMMIT @600: diverged=figh inputs=DIFFER (input/pairing skew)
Linux P0 Turn(18) vs Android P0 Dash(15)
```

Same peer (Android): NetSync `hist_win=[480,600) all=0x60E624C7` matched Linux FC `inp`, but Android FC `inp=0x1844FC52`. Published history ≠ frame-commit authority on one peer.

## Root cause

Feel-0 split gameplay vs send-lead rings, but `syNetInputResolveLocalAuthorityFrameEx` still preferred **Transmitted** over **Gameplay**.

Egress `AppendDelayedLocalRowsToBundle` NoteTransmits `delay[sim]` when `t == sim`. Send can run before FuncRead stages the real HID sample for that tick, so `delay[sim]` is still the provisional hold-last filled at `sample-1`. That locks Transmitted (and Promote published) to provisional while `MakeLocalFrame` / sim already use Gameplay = real HID after sample.

FC hashes ResolveLocalAuthority → Transmitted (stale). NetSync hist and the peer’s confirmed wire eventually reflect the real row → `inputs=DIFFER` and fighter fork (Dash vs Turn).

## Fix

1. **Resolve order (NETMENU):** Gameplay → Transmitted → latch (no live HID on resim).
2. **`NoteTransmittedSimFrame`:** refuse local-delay NoteTransmit until Gameplay exists for that tick; if send-lead ≠ gameplay, store gameplay.
3. **Staging:** when a real sample overwrites send-lead and Transmitted already diverges, re-NoteTransmit the sample row so published realigns.

## Verify

- Re-soak Linux ↔ Android with `FORCE_MISMATCH` still armed: FC@600 must not report `inputs=DIFFER` from local hist≠authority.
- Android (or either peer) FC `inp` must equal that peer’s NetSync `hist_win` combined digest for the same window when slots are stable.
- Stick onset after GO: no `rollback_epoch` / `load_fail_hold` storm from provisional NoteTransmit (wire-only ahead rows unchanged).
