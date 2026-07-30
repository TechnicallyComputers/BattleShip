# CatchWait: C2b bank authority for `throw_wait`

**Date:** 2026-07-29
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Seed:** soak1 `1685497605` (session `614503255`) — Ness ditto Dream Land; Linux host /
Android guest. `PEER_SNAPSHOT_DIVERGE` @ load **1724** — `figh`+`anim`+`cam`,
`item`/`wpn`/`rng`/`map` MATCH, `inputs_agree=1`, `class=replay_determinism`.

## Symptom

Grab hold matched through CapturePulled/CatchPull (@1717–18) and CaptureWait/CatchWait
(@1719–1723) with matching `fhash_light` + `anim`. At **1724**:

| Peer | P0 | P1 |
|------|----|----|
| Linux | ThrownMarioB (186) | ThrowF (169) |
| Android | CaptureWait (172) | CatchWait (168) |

Android stayed in hold until ~1730. Inputs around 1720–23 were neutral (`btn=0`, stick 0).
`FTCOMMON_CATCH_THROW_WAIT = 60` — only ~5 frames in CatchWait, so the throw cannot be
natural timer expiry. Linux-only `SYNCTEST_OK` + `emergency_restore` ran mid-CatchWait
@1722.

## Root cause — C2 half-wired (same class as Turn / Squat / KneeBend)

Tagged capture reads `bank[CatchWait]` and apply projects bank → union, but
`ftStatusVarsCatchWait()` still returned the **union**. Forward sim decrements
`throw_wait` in the union; the bank slot stays 0/stale. Mid-CatchWait ring save
captured `throw_wait=0`; synctest/load projected that zero →
`ftCommonThrowCheckInterruptCatchWait` saw `throw_wait == 0` and entered ThrowF
while the peer's live countdown continued.

`throw_wait` was also **hash-blind** in `fhash_light`, so FIGHTER_LIGHT_ONSET could
not catch the skew before the status fork (`fhash` still matched @1723).

## Fix (architecture)

1. **Accessor redirect.** `ftStatusVarsCatchWait()` →
   `syNetplayStatusVarsBankAuthoritySlot(…, CatchWait, …)` under
   `syNetplayRollbackSemanticsActive()`. `CatchWaitSetStatus` inits `throw_wait`;
   status-scoped — no blob sidecar (KneeBend/Landing recipe).
2. **fhash_light fold.** Accumulate `throw_wait` while `status_id == CatchWait`.
3. **Witness.** Integrity check reads `bank[CatchWait]` via `BankSlot` (not the
   accessor — recursion; not the raw union — stale after migration).

No SoftLip/FC exceptions. Offline (`SSB64_NETMENU=OFF`) unchanged.

Files: `decomp/src/ft/ftstatusvars.h`, `port/net/sys/netsync.c`,
`port/net/sys/netplay_statusvars_witness.c`, `port/net/sys/netplay_statusvars_bank.h`.

## Follow-up

- Re-soak seed `1685497605`: expect no CatchWait→ThrowF split after mid-hold
  synctest/light load; no PEER@1724 from this path.
- **Capture / CatchMain** overlays are still half-wired (`ftStatusVarsCapture` /
  `CatchMain` return union; `ftcommoncapturepulled.c` still has raw
  `status_vars.common.capture.is_goto_pulled_wait` R/W). Migrate next if a soak
  sticks in CapturePulled or CatchPull after a mid-grab load — route raw sites to
  accessors first (directive 6).

## Related

- [Turn bank authority](netplay_turn_statusvars_bank_authority_2026-07-28.md)
- [Squat/Landing bank authority](netplay_squat_landing_statusvars_bank_authority_2026-07-28.md)
- [KneeBend/JA bank authority](netplay_kneebend_jumpaerial_statusvars_bank_authority_2026-07-28.md)
- [Ness grab floor sink / throw_wait witness](netplay_ness_grab_floor_sink_2026-06-03.md)
- `docs/refactor/ftstatusvars_overlay_map_2026-06-02.md`
