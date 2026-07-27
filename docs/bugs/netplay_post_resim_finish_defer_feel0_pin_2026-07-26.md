# Post-resim FINISH_DEFER / Verify early-return skips feel-0 pin (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `449622004` seed `525917575` — soft-PASS to ~1863 (clean window close); residual `LOCAL_PUBLISH_LATCH_REFUSE`  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** invent / publish hygiene (not a hard kill this soak)  
**After:** [mid-span gap-hold walk-back](netplay_post_resim_gap_hold_midspan_walkback_2026-07-26.md)

## Symptom

Universe kill cleared, but Android still logged **194×** ahead `LOCAL_PUBLISH_LATCH_REFUSE` (`tick > frontier`). Canonical cluster after follower resim `431→435`:

| Step | Log |
|------|-----|
| 1 | `FINISH_DEFER_TO_FUNCUPDATE_BEGIN` @435 |
| 2 | `POST_RESIM_LIVE_SAVE_DEFER … finish_boundary_pending` |
| 3 | `EPISODE_FSM Replay → Verify` (Finish early-return) |
| 4 | `commit_promote` → Commit → Live |
| 5 | `LATCH_REFUSE` 433/434/435 `frontier=432` — **0×** `POST_RESIM_GAP_HOLD_LAST` |

Same shape on ~90 Commit spans (e.g. 442–445, 605–609 with DEFER).

## Root cause

`syNetRollbackFinishForwardResim` had two exits that never called `ResyncControllersAfterResim`:

1. **FINISH_DEFER** — live interface already ran this FuncUpdate; return `FALSE` before Resync.
2. **Verify early-return** — FSM not yet Commit/Live; capture post digest and `return TRUE` before Resync.

Commit Finish later ran Promote (hence `LATCH_REFUSE`) without feel-0 for mid-span / exclusive ticks that only had `SEAL_PACK_GAP_HOLD` + predicted History. Mid-span walk-back in Resync cannot help if Resync never runs on those exits.

## Fix

| Site | Change |
|------|--------|
| `FINISH_DEFER` | Call `ResyncControllersAfterResim` (pin feel-0) before arming defer |
| Verify early-return | Same Resync before `return TRUE` |
| `PromoteAfterResimExit` | Re-call Resync (idempotent) immediately before Promote so any skipped pin is healed once `ResimDepth=0` |

## Acceptance

Matched APK + Linux binary:

- After `FINISH_DEFER` / Verify→Commit: `POST_RESIM_GAP_HOLD_LAST` for missing mid-span + exclusive ticks when prior feel-0 exists
- Near-zero ahead `LOCAL_PUBLISH_LATCH_REFUSE` for those post-resim clusters (past-frontier refuse OK)
- No regression of soft-PASS / `BASELINE_UNIVERSE` clear from mid-span soak

## Related

- [`netplay_post_resim_gap_hold_midspan_walkback_2026-07-26.md`](netplay_post_resim_gap_hold_midspan_walkback_2026-07-26.md) — pin window + donor walk-back
- [`netplay_post_resim_local_publish_gap_branch_deferred_2026-07-26.md`](netplay_post_resim_local_publish_gap_branch_deferred_2026-07-26.md) — exclusive-target pin + Promote-after-depth
