# Dead / Rebirth / Damage: C2b bank authority (KO path)

**Date:** 2026-07-28
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Seed:** soak1 `3685555679` (session `235251572`) — locomotion (Turn/KB/JA bank) felt smoother;
KO path failed with three MATCH-input FCs:

| FC | Status | Diff |
|----|--------|------|
| 1266 | DamageFlyRoll | `topn_tx` ULP |
| 1389 | DeadDown | `tap_stick_*`; `dead_gate_wait` live=1 vs blob=9 @1380 seed |
| 1406 | RebirthDown | `topn_ty` local≠0 vs peer=0 |

## Root cause

Same half-wired C2 hole as Turn/JA: tagged capture reads `bank[Dead|Rebirth|Damage]` but
accessors still wrote the union. Mid-death / mid-rebirth / mid-DamageFly loads projected stale
bank bytes. `dead_gate_wait` survived as a mirror band-aid; blob vs live `dead.wait` skew at
FC seed is the smoking gun for Dead.

## Fix (architecture)

Redirect `ftStatusVarsDead` / `Rebirth` / `Damage` through `syNetplayStatusVarsBankAuthoritySlot()`
under rollback semantics. No blob sidecars (all status-scoped for capture ownership; Thrown
pre-seeding `damage.script_id` into `bank[Damage]` is a bank-isolation win).

Witness integrity for Dead/KneeBend/JumpAerial now samples the bank slot (not the stale union
projection) so C2b migration does not spam false `corrupt dead_gate` lines.

Files: `decomp/src/ft/ftstatusvars.h`, `port/net/sys/netplay_statusvars_witness.c`,
`port/net/sys/netplay_statusvars_bank.h`.

## Follow-up

- Soak KO path: expect stable `dead_wait` countdown, rebirth pose round-trip, no `topn_ty=0`
  after mid-rebirth load.
- Residual DamageFly `topn_tx` ULP with matching hitstun may be physics — not FC exceptions.
- Retire `dead_gate_wait` mirror only after soak proves bank authority (directive 6).

## Related

- [Turn bank](netplay_turn_statusvars_bank_authority_2026-07-28.md)
- [KneeBend/JA bank](netplay_kneebend_jumpaerial_statusvars_bank_authority_2026-07-28.md)
- `docs/refactor/ftstatusvars_overlay_map_2026-06-02.md`
