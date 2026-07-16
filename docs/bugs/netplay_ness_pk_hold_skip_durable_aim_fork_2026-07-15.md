# Netplay: Hold `SKIP_DURABLE` silent aim fork → FC figh diverge

**Date:** 2026-07-15
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)
**Soak:** session `353785594` seed `2424332138` — `FRAME_COMMIT_STATE_DIVERGE @1360` figh inputs MATCH

## Symptom

Both peers stop at validation 1360 / snap 1359. Ness air jibaku (`status=236`) —
`topn_tx`/`topn_ty` diverge (~133 / ~22 units). World/item/rng match; inputs MATCH.

## Timeline

| Tick | Event |
|------|-------|
| 1279–1339 | 2nd PK Hold — fighter **root identical** on both peers |
| **1304** | **Thunder head first forks** (Android vs Linux); grows to ~21 units by 1340 |
| 1303, 1311–12, 1316–17, 1325 | Android sim used **stale stick**; Linux used local truth |
| 1340 | `jibaku_trigger` — same tick both peers, different head → different launch `vel_air` |
| 1359→1360 | FC diverge on player 0 `topn` |

Example @1303 (Android):

```
REMOTE_PUBLISH_SKIP reason=hold_last_ness_pk_scope
REMOTE_PUBLISH_SKIP_DURABLE reason=hold_confirmed_only sx=-8 sy=-83
STICK_SAMPLE tick=1303 sx=-8 sy=-83          ← sim consumed this
REMOTE_PUBLISH ledger_wire sx=-10 sy=-82     ← after sample; history claims truth
```

Linux local @1303: `sx=-10 sy=-82`. Frame-commit later compares ledger/history → inputs MATCH.

## Root cause

`PublishFrame` Hold "confirmed-only" path skipped durable history for predicted remote rows
(`REMOTE_PUBLISH_SKIP_DURABLE`) while still driving the ephemeral controller with hold-last.
When wire arrived after that tick's sim:

1. Ledger filled published history to match the peer → **inputs MATCH**
2. No `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` (`had_published == FALSE`) → **no GGPO**
3. PK Thunder head aimed from stale stick → launch fork → FC figh diverge

Weapon-only baseline absorb is still correct for real mid-Hold GGPO; skipping durable invent
was the wrong companion.

## Fix (`PORT && SSB64_NETMENU`)

1. **Revert** `PublishFrame` durable-history skip during `FcResimDeferScope`. Provisional
   predicted rows must land in `sSYNetInputHistory` so completed-sim ledger refresh can GGPO.
2. **`RefreshPublishedFromAuthorityLedger`**: if history miss but `last_published.tick == sim_tick`,
   treat `last_published` as the consumed baseline for GGPO (belt against any future skip).
3. Keep **PK Hold weapon-only baseline absorb** (`RESIM_BASELINE_PK_HOLD_WPN_ONLY_ABSORB`).

Promote still skips *hold-last invent of completed-sim / Hold promote* (`hold_last_ness_pk_scope`);
current-tick invent remains PublishFrame's job.

## Verify

- Rebuild netmenu; re-soak Ness PK Hold with remote stick motion.
- Expect: mid-Hold `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` → short GGPO (not silent fork).
- Expect: no `FRAME_COMMIT_STATE_DIVERGE` after same-tick Hold aim mismatch.
- If Hold GGPO hits wpn-only baseline: absorb log, not deepen/seal hang.

## Related

- `netplay_ness_pk_hold_confirmed_aim_weapon_absorb_2026-07-15.md` — introduced SKIP_DURABLE (partially reverted)
- `netplay_input_authority_tuple_fork_fail_closed_2026-07-15.md` — completed-sim ledger GGPO
- `netplay_ness_pkthunder_jibaku_air_arc_quantize_2026-07-10.md` — post-launch arc (not this seed)
