# Netplay: Ness PK Hold confirmed-only remote aim + weapon-only baseline absorb

**Date:** 2026-07-15
**Status:** PARTIAL — weapon absorb kept; PublishFrame durable skip **reverted** (see
`netplay_ness_pk_hold_skip_durable_aim_fork_2026-07-15.md`)
**Follow-up to:** `netplay_ness_pk_hold_aim_ggpo_defer_2026-07-15.md` (volatile-only GGPO defer)

## Symptom (post Hold-aim-defer soak)

After mid-Hold GGPO was allowed, jibaku succeeded (~4×), but Hang shifted later mid-Hold:

```
LEDGER_REFRESH_COMPLETED_SIM_CORRECT … (±1 stick during Hold)
resim begin … Hold-span GGPO
RESIM_BASELINE_BISECT … peer_vs_armed wpn=1 (figh/world/map/cam/anim match)
→ deepen storm → SEAL_ROWS_TIMEOUT / SELF_SEAL_SKIP not_wire_confirmed → session stop
```

## Root cause

1. **Promote** already skipped hold-last invent during `FcResimDeferScope`, but **`PublishFrame`** still stored predicted remote stick (last-confirmed invent from resolve) into durable published history for the current Hold tick. Late wire ±1 then fired `LEDGER_REFRESH_COMPLETED_SIM_CORRECT` → Hold-span GGPO.
2. Hold-span resim baseline often disagrees on **weapon only** (PK Thunder head path fragile mid-Hold). Treating that as hard mismatch → deepen → seal storm.

## Fix (`PORT && SSB64_NETMENU`)

1. ~~**`PublishFrame` — skip durable predicted invent during Hold.**~~ **REVERTED** — soak
   `353785594` showed silent Hold aim forks (ledger filled history after sim; inputs MATCH; head
   forked). Reverted in `netplay_ness_pk_hold_skip_durable_aim_fork_2026-07-15.md`.

2. **`netrollback.c` — PK Hold weapon-only baseline absorb (kept).**
   `syNetplayNessAnyLiveFighterInPkHoldAimScope` (Start/Hold/End). When peer_vs_armed is
   weapon-only, adopt peer wire weapon and open the replay gate
   (`RESIM_BASELINE_PK_HOLD_WPN_ONLY_ABSORB`) instead of deepen.

## Verify

- Weapon absorb: Hold GGPO that bisects wpn-only opens gate (no deepen/seal hang).
- Do **not** expect absence of mid-Hold `LEDGER_REFRESH` — that path must remain for aim truth.

## Related

- `netplay_ness_pk_hold_aim_ggpo_defer_2026-07-15.md` — allow mid-Hold TryBegin
- `netplay_input_authority_tuple_fork_fail_closed_2026-07-15.md` — completed-sim ledger GGPO
- `netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md` — Hold resim canonicalize
- `netplay_fc_recovery_weapon_drift_2026-06-11.md` — FC recovery wpn-only load soft-continue
