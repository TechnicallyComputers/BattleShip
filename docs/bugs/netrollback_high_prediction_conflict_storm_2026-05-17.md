# High-prediction rollback conflict storm (2026-05-17)

## Symptoms

With prediction windows around 3 ticks, peers could run deterministically for a minute or two and then one peer stalled in strict remote-miss admission after a stick-change prediction. Logs showed:

- repeated `REMOTE_CONFIRMED_CONFLICT keep_old` for the same wire ticks, where a stale stick value won over newer confirmed packets;
- rollback storms where `INPUT_MISMATCH_DETAIL` showed identical gameplay values but different metadata (`predicted` vs `confirmed`);
- eventual strict `MISS (R)` abort on one peer with no `LOAD_HASH_DRIFT`.

## Root Cause

Two authority rules were too strict for high-prediction regional matches:

- The input timeline compared published sim ticks with incoming wire ticks and also treated predicted metadata as incorrect even when buttons/sticks matched. That made confirmed rows with identical gameplay trigger rollback.
- Remote confirmed rows had no packet sequence metadata. Once a stale confirmed value occupied a wire tick, later corrected resends for the same tick were rejected forever as conflicts.

## Fix

- Timeline incorrect markers now compare gameplay inputs only: buttons and stick values.
- Remote confirmed rows record the packet sequence that authored them. A newer/equal packet may replace a conflicting confirmed row and mark the corrected gameplay for rollback; older conflicting packets are still rejected.
- Gap-filled rows still remain admission-only and are freely replaced by real confirmed packets.

## Verification

- Build `ssb64`.
- Re-run two-peer high-prediction soaks with `SSB64_NETPLAY_PHASE_LOCK_PREDICTION_TICKS` / window values in the 3-7 range.
- Expected: no repeated equal-gameplay rollback storm, no sustained strict `R` stall from stale confirmed rows, and no `REMOTE_CONFIRMED_CONFLICT keep_old` pattern for newer corrected packets.
