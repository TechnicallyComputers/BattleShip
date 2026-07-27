# Hash-confirm mismatch force rewind (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** frame-commit / invent GGPO tax  
**After:** [suppress_wire_ggpo](netplay_hash_confirm_suppress_wire_ggpo_2026-07-26.md)

## Symptom

Soak `seed=2351999622`: Linux invent REPLACE @533–542 armed `hash_confirm_schedule`, all resolved `HASH_CONFIRM_DEFER_RESOLVE class=rewind reason=snap_mismatch`, but **no** `GGPO deferred` for those ticks. Next material REPLACE @543 opened GGPO → `BASELINE_UNIVERSE` @542 deepen storm → session end.

## Root cause

Soft-defer / ledger refresh **StoreFrame**s wire into History before the rewind decision. `QueueOrWiden` → `RequestInputCorrection` then sees:

- `is_predicted == 0` (ledger confirmed), and/or
- published gameplay equals remote wire

→ silent return. Invent-baked first-pass state (snap already mismatched) never resims.

## Fix

`syNetRollbackForceDeferredInputCorrection(player, sim_tick, reason)` — arm deferred GGPO like BRANCH_DEFERRED same-stick force (bypass StickReplace / prediction gates). Fold into open resim/deferred when already pending.

Call sites:

1. `HASH_CONFIRM_DEFER_RESOLVE` rewind (`snap_mismatch` / `deadline`)
2. LEDGER needs_rewind when `snap_agree` KnownMismatch and Schedule did not handle
3. Hash-confirm defer supersede flush

Logs: `HASH_CONFIRM_FORCE_GGPO … reason=… path=queue|fold_open`.

## Acceptance

Matched APK + Linux:

- After `class=rewind reason=snap_mismatch`: `HASH_CONFIRM_FORCE_GGPO` + `GGPO deferred` / `resim begin` for that tick (or fold into open episode)
- Fewer immediate `BASELINE_UNIVERSE` storms right after invent snap_mismatch clusters
- Still 0× confirm→same-tick GGPO; Promote path unchanged
- 0× JA SoftLip PEER from hash_confirm (740113729)

## Related

- [`netplay_hash_confirm_suppress_wire_ggpo_2026-07-26.md`](netplay_hash_confirm_suppress_wire_ggpo_2026-07-26.md)
- [`netplay_hash_confirm_snap_tick_defer_2026-07-26.md`](netplay_hash_confirm_snap_tick_defer_2026-07-26.md)
