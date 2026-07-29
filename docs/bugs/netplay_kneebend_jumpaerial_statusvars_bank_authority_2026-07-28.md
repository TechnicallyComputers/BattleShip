# KneeBend + JumpAerial: C2b bank authority (no SoftLip/FC exceptions)

**Date:** 2026-07-28
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Seed:** soak1 `2157085813` (session `446066222`) — after Turn bank migration:
`FIGHTER_LIGHT_ONSET@1418` (KneeBend), `SOFTLIP_PHASE_FORK@1424` JumpAerial
`compound=softlip_ja_vel_fork`, FC@1431 `inputs_agree=1`, then hang via
`commit_covered_healed_exclusive_end` (FC recovery refused behind healed frontier).

## Symptom

Dual JumpAerial near Dream Land lip: peers match JA entry (`jumpaerial entry@1420`),
then SoftLipPhase diverges on `topn_x` / `vel_x` / `ja_vel_x`. Android mid-JA resim
left `ja_vel_x=0` while Linux kept friction. FC mint then hung (~700× deferred recovery,
`recovery_started=0`) because onset `1422` sat behind `resolved_through=1429`.

## Root cause — same half-wired C2 hole as Turn

Tagged capture (C2a) reads `bank[JumpAerial]` / `bank[KneeBend]` for owned statuses, and
apply projects bank → union — but `ftStatusVarsJumpAerial()` / `ftStatusVarsKneeBend()`
still returned **union** pointers. Forward sim never refreshed the bank slots, so mid-JA
loads restored stale zeros over live `vel_x`/`drift` and SoftLip amplified the fork.

KneeBend light already disagreed at `@1418` (anim matched) before JA entry re-agreed —
same overlay class on the locomotion chain.

Turn@455 on this seed was a separate branch-predict discard (`lr_dash` matched both peers);
not an `lr_dash` wipe. SoftLip/FC protocol exceptions would not fix the load wipe.

## Fix (architecture)

Redirect both accessors through `syNetplayStatusVarsBankAuthoritySlot()` under
`syNetplayRollbackSemanticsActive()` — identical recipe to Turn:

| Overlay | Sidecar? | Why |
|---------|----------|-----|
| KneeBend | No | SetStatus inits all fields; `JumpSetStatus` reads kneebend **before** `ftMainSetStatus` (still KneeBend). |
| JumpAerial | No | Status-scoped; Ness/Yoshi SetStatus inits every field ProcPhysics uses; tagged capture round-trips. |

No SoftLip collision exceptions, no FC healed-frontier changes in this phase.

Files: `decomp/src/ft/ftstatusvars.h` (KneeBend + JumpAerial accessors).

## Follow-up

- Soak seed `2157085813` / dual-jump Dream Land: expect no mid-JA `ja_vel=0` after light load,
  no SoftLipPhase fork from bank wipe, no FC hang from that path.
- Residual SoftLipPhase forks with **matching** pre/post-load `ja_vel` are real cliff physics
  (separate architecture track) — not FC recovery patches.
- Retire `HardenJumpAerialPassColl` overlay quantize band-aids only after soak evidence
  (directive 6: no new mirrors; delete old ones when bank is proven).

## Related

- [Turn bank authority](netplay_turn_statusvars_bank_authority_2026-07-28.md)
- [JA statusvars scrub synctest](netplay_jumpaerial_statusvars_scrub_synctest_2026-07-19.md)
- [JA vel witness](netplay_jumpaerial_ja_vel_witness_2026-07-19.md)
- `docs/refactor/ftstatusvars_overlay_map_2026-06-02.md`
