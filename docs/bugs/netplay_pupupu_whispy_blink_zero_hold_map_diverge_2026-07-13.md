# Netplay: Pupupu blink=0 leftover hold → map-only PEER_SNAPSHOT_DIVERGE (2026-07-13)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

**Session:** soak1 `1851430281`, seed `1317421621` (Android client ↔ Linux host, Dream Land / `gkind=6`)

## Symptom

Synctest PASS (18 OK / 0 FAIL). After ~2270 ticks, Android:

```text
RESIM_BASELINE_MISMATCH map-only deeper exhausted … peer_map=0x80B78429 local_map=0x41769640
PEER_SNAPSHOT_DIVERGE load_tick=2265
  figh/world/item/rng/anim/wpn/cam MATCH — only map differs
PEER_SNAPSHOT_DIVERGE — stopping VS session
```

Linux guest: soft recovery / session end. Drift scan PASS (no LOAD_HASH_DRIFT); sync-report MATCH UNSTABLE on `PEER_SNAPSHOT_DIVERGE`.

## Timeline

| Tick | Both | Notes |
|------|------|-------|
| 2210–2218 | `status=4` Blow, `blink=0`, `eyes=-1` | Hold while leftover eyes anim drains |
| 2216 | stick GGPO `2216→2220` (Android initiator) | Load@2215 Refresh pins Android eyes |
| **2219** | Android `blink=-1` / Linux `blink=0` | First `ground_fold` fork (`kin` still matches) |
| 2228–2229 | Both reseed `blink=171` (ForcedCosmetic) | RNG agrees; fold already forked |
| 2265 | map-only baseline deepen exhaust | Session kill |

## Root cause

1. **Leave-zero gated on leftover `map_gobj[0]->anim_frame`.** When blink hits 0, `UpdateBlink` sets `eyes_status=Blink`; same tick `UpdateGObjAnims` starts the eyes anim and clears `eyes_status` to `-1`. Vanilla then holds `blink=0` until `anim_frame <= 0`. That leftover is cross-ISA unstable (same class as the earlier lockout / Open→Blow bugs).

2. **GGPO amplifies the hold.** `syNetRbSnapRefreshPupupuWhispyMapAnimAfterLoad` pins eyes GObj frame to 0 when `eyes_status==-1`. One peer exits the hold on resim; the other still waits on residual DObj wait → one-tic `blink` skew → `ground_fold` / full `map` hash diverge with matched fighters/RNG.

3. Prior blink fix only forced progress for lockout `(-9..-1)`; **leave-zero still required eyes ended.**

## Fix

Under `syNetplayRollbackSemanticsActive()` in `grPupupuWhispyUpdateBlink`: when `blink_wait == 0` and `eyes_status` is already idle (`-1`), do **not** gate on leftover eyes anim — enter the post-blink lockout immediately. Eyes blink anim still plays for presentation; ForcedCosmetic reseed at `-10` unchanged.

## Verify on re-soak

- Dream Land through Blow with stick GGPO while `blink` is in the 0-hold window.
- Prefer matching `pupupu_ground` blink across peers (no multi-tick `blink=0` then asymmetric leave).
- No map-only `PEER_SNAPSHOT_DIVERGE` with matched figh/rng after Blow blink cycles.

Related: [`netplay_pupupu_whispy_blink_rng_fc_2026-07-12.md`](netplay_pupupu_whispy_blink_rng_fc_2026-07-12.md), [`netplay_pupupu_whispy_open_blow_rng_fc_2026-07-12.md`](netplay_pupupu_whispy_open_blow_rng_fc_2026-07-12.md), [`netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md`](netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md).
