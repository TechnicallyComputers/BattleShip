# Netplay — C2 `STATUSVARS_TAG_HEAL` storm from sticky `live_overlay` on unowned statuses

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host, Dream Land Ness ditto, seed `4092906820`  
**Related:** [C2 overlay map](../refactor/ftstatusvars_overlay_map_2026-06-02.md), [squat scrub FC](netplay_squat_pass_wait_statusvars_scrub_fc_2026-07-28.md)

## Symptom

Post-C2a/C2b soak cleared scrub-class Pass/Squat/Landing FC and reached clean `VS_SESSION_END` @5379 with 0× `PEER_SNAPSHOT_DIVERGE`. Logs still showed ~900×:

```
STATUSVARS_TAG_HEAL … blob_status=10|22|23|26|27|33 captured_tag=3|4|5|6|8|10 expected_tag=-1
```

Plus FC field_diff `status_vars_overlay live=0xFFFF blob=0x5` during Jump MATCH-input SoftLip FC.

## Root cause

Ownership table returns `nFTStatusVarsOverlayNone` for Wait / Walk / Dash / JumpF/B / Fall / Pass / …. C2b `ftMainSetStatus` only retagged when expected ≠ None, so `live_overlay` **stuck** on the previous owned overlay (KneeBend after jump, Squat after Pass, Turn after Wait, …).

Capture then emitted `bank[stale]` + stale tag. Apply validate healed every unowned status to `-1` and logged TAG_HEAL; bank restore skipped (`tag >= 0`), leaving sticky live tags for the next capture.

Not a gameplay desync by itself (light folds are status-gated), but an authority hole and log flood that masked real SoftLip / invent work.

## Fix

`PORT && SSB64_NETMENU`:

1. **`ftMainSetStatus`** — always `BankSetLiveOverlay(expected)`, including `None`; project union only when expected is a real overlay.
2. **Capture** — owned: `bank[expected]` + tag; unowned: union projection + tag=`None` (never emit sticky prior).
3. **Apply validate** — expected `None`: quiet-clear stale owned tags (no TAG_HEAL log). Real overlay mismatch: log + heal as before.
4. **Apply bank** — tag `None`: clear `live_overlay` and keep union memcpy; do not re-stick a prior slot.

## Verify on re-soak

- Near-zero `STATUSVARS_TAG_HEAL` (only real owned-tag mismatches, if any).
- No `status_vars_overlay` field_diff of `FFFF` vs owned id on Wait/Jump/Fall.
- Scrub-class FC still absent; remaining MATCH `topn_tx` SoftLip is a separate class.
