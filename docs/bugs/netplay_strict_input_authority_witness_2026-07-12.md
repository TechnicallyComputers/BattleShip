# Strict input authority: architectural migration + witness (step 1)

**Date:** 2026-07-12  
**Session:** soak `2109911131` seed `3288125595` (and the whole 2026-07-11/12 stick-fix arc)  
**Status:** WITNESS LANDED + write-once step for promote/patch/publish (`PORT && SSB64_NETMENU`) — see `netplay_confirmed_publish_write_once_2026-07-12.md`. Full mechanical rollback / confirmed-only commit still pending.

## Why an architectural change

Soak `2109911131` FC@480: `inp_local=0x032ACBF6 inp_peer=0x2D4F7C8D` — the peers **committed different input histories** after a ~25-tick stick ramp produced dozens of `REMOTE_CONFIRMED_REPLACE_NEWER` with GGPO only on the largest release deltas. Baseline @477 already forked (`figh 0xDA9C1789` vs `0x7023B1A9`).

Every fix in this arc (jump debounce, seal stall, release deadband, storm cooldown, stash hang, ramp deadband) gates the same question: *"this wire input differs from published — does it matter?"* The architecture forces the question because **feel-0 stamps provisional hold-last values as `RemoteConfirmed`** (send-lead runway). Once fabricated data is labeled confirmed, real inputs arrive as mutations of confirmed history, adjudicated by heuristics whose false-negative regions a continuous analog stick sweeps through. Every skipped rewind is a permanent fork because the input hash is part of frame commit.

## Target invariants (classic GGPO)

1. **Confirmed input is single-writer + write-once.** Only player P's peer writes P's confirmed row for tick t, once. "REPLACE of confirmed" cannot exist.
2. **Rollback is mechanical.** Record what the sim consumed per tick; `consumed != confirmed` (exact compare, no deadband) → rollback. Delete `GameplayCorrectionIsSignificantEx`, both deadbands, release/onset/defer heuristics, storm cooldown.
3. **Commit only over confirmed inputs.** Commit frontier = `min(confirmed frontier across players)` → `inputs=DIFFER` at FC impossible by construction; remaining FC diverges are pure sim-determinism bugs.

## Migration plan

| Step | What | Status |
|------|------|--------|
| 1 | Log-only witness enumerating every invariant violation + writer | **landed** |
| 1b | Write-once: promote / patch / publish refuse confirmed gameplay rewrite | **landed** (`netplay_confirmed_publish_write_once`) |
| 2 | Commit frontier moves to confirmed-only inputs | pending |
| 3 | Exact-compare mechanical rollback behind flag; soak; delete heuristics | pending |

## Step 1: the witness

`SSB64_NETPLAY_STRICT_INPUT=1` (netmenu builds only, zero behavior change). Hooked in `syNetInputStoreFrame`; writers tagged via `SYNETINPUT_STRICT_TAG`.

Violation kinds (`STRICT_INPUT kind=… writer=…`, 200 detail lines/match, summary every 256 + at next session start):

| Kind | Meaning |
|------|---------|
| `wire_overwrite` | Strict-confirmed wire ring row rewritten with different gameplay (feel-0 runway resend) |
| `wire_downgrade` | Strict-confirmed wire row rewritten by non-strict source (gap-fill/predicted) |
| `fabricated_confirm` | Published row stamped `RemoteConfirmed` with no matching strict wire row (sealed reconcile, runway stamping) |
| `confirm_rewrite` | Confirmed published row rewritten by confirmed with different gameplay |
| `confirm_downgrade` | Confirmed published row rewritten by non-confirmed source (Promote/mark-predicted) |

Writer tags: `wire_commit`, `wire_gap_fill`, `wire_predicted`, `promote_remote_authority`, `patch_publish`, `mark_predicted_replace`, `publish_frame`, `resim_wire`, `resim_wire_pred`, `post_resim_wire`, `resim_sealed`, `store_published_api`, `debug_xor`, else `untagged`.

## Verify

Run one stick-heavy soak with `SSB64_NETPLAY_STRICT_INPUT=1` on both peers. The summary counts + per-writer detail lines are the migration worklist: each `fabricated_confirm` / `confirm_downgrade` writer must move to the predicted layer; each `wire_overwrite` measures the runway protocol change needed (provisional rows must be tagged provisional on the wire, or the runway removed).

Related: `netplay_stick_replace_policy_consolidate_2026-07-12.md`, `netplay_stick_ramp_predict_deadband_silent_promote_2026-07-12.md`, `netplay_feel0_provisional_remote_phase_lag_2026-07-11.md`, `netplay_zero_delay_local_feel_2026-07-11.md`.
