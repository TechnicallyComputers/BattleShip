# Netplay — Ness PK Thunder Hold `pkthunder_pos` snapshot coupling (2026-06-02)

**Date:** 2026-06-02  
**Status:** Fix revised (Phase 2 soak pending — 2026-06-04)  
**Area:** `port/net/sys/netplay_ness_pkthunder_gate.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

Cross-ISA Ness×Ness netplay: visible **position snap** on downward / ground-aiming PK Thunder jibaku after rollback load during Hold. Sim hashes (`figh`, `mph`) stay matched — not a quantize drift. Both peers snap the same way.

## Root cause

Jibaku launch angle uses **`pkthunder_pos`** (last self-hit bolt position) vs fighter translate:

```c
pos.x = fighter.x - pkthunder_pos.x;
pos.y = (fighter.y + 150) - pkthunder_pos.y;
```

Vanilla only writes `pkthunder_pos` on collide. Rollback **capture** stored whatever was in `status_vars` at save time; **apply** restored that blob field **before** weapon GObjs were re-bound. The live PK Thunder head (post-`syNetRbSnapApplyWeapons`) could sit at a different world position than the restored `pkthunder_pos`. Next jibaku used the stale vector → wrong launch / perceived teleport. Same on both peers → no hash fork.

Apply order: fighter blob (`memcpy status_vars`) → … → weapon apply → coupled rebind. `syNetplayNessReconcilePKThunderWeaponsAfterApply` reacquired the head but did not refresh `pkthunder_pos`.

## Fix

| Layer | Change |
|-------|--------|
| **Helper** | `syNetplayNessRefreshPKThunderPosInBlobFromHead` (capture-only) + `syNetplayNessRefreshPKThunderPosForJibakuLaunch` (live, at jibaku SetStatus only) |
| **Apply** | **No** live Hold refresh on apply/reconcile — stale blob `pkthunder_pos.x=0` + distant head was poisoning Hold every rollback load (`capture_only=0` @3740) |
| **Hold tick** | No per-tick head tracking (`SyncPKThunderPosDuringHold` = probe only). Per-tick sync reverted after soak @7461: head-tracking changed jibaku angle vs vanilla and worsened Sector Z pass-floor snaps. |
| **Jibaku launch** | **2026-06-04:** `RefreshPKThunderPosForJibakuLaunch` uses `PKThunderPosNeedsApplyRepair` — keep valid self-hit anchor when `x≠0`; refresh to head only for zero / partial-scrub corrupt blobs. |
| **Apply repair** | **2026-06-04:** `syNetplayNessRepairCorruptPKThunderPosAfterApply` after weapon rebind — fixes partial `pkthunder_pos.x=0` scrub; preserves non-zero blob anchors when head has orbited. |
| **Capture** | Blob-only refresh after `status_vars` memcpy — does **not** mutate live `fp` every ring-save tick |
| **Jibaku** | Self-hit `pkthunder_pos` for launch angle; head refresh only when `NeedsApplyRepair`; diag `event=jibaku_pos_refresh` with `dist=(x,y)` |
| **Gravity** | `sanitize_gravity` on Hold apply — restore premature `pkthunder_gravity_delay=0` scrub from inferred hold-entry value |
| **Diag** | `event=pkthunder_pos_refresh`; `capture_only=1` on blob path only |

Jibaku statuses (231/236) are intentionally excluded — `pkthunder_pos` is fixed at trigger time for the arc.

## Verification

1. Cross-ISA Ness Hold → steer bolt down → jibaku across a rollback load @509-style window: no visible launch snap; optional `pkthunder_pos_refresh` in gate diag.
2. Frame-commit / `sim_state_tick` still match through jibaku (regression).
3. Standing ground jibaku (231) and air downward jibaku (236) both soak-clean.

## Related

- [`netplay_ness_pkthunder_hold_quantize_2026-06-02.md`](netplay_ness_pkthunder_hold_quantize_2026-06-02.md)
- [`netplay_ness_pkthunder_jibaku_anim_length_scrub_2026-06-01.md`](netplay_ness_pkthunder_jibaku_anim_length_scrub_2026-06-01.md)
- [`netplay_ness_pkthunder_instant_jibaku_2026-06-01.md`](netplay_ness_pkthunder_instant_jibaku_2026-06-01.md)
