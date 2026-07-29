# Turn overlay: C2b bank authority + always-captured blob sidecar

**Date:** 2026-07-28
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Seed:** soak1 `3066947259` (session `1439010986`) — `TURN_DASH_LR_DASH_FORK tick=435 phase=interrupt player=0 (host lr_dash=0 did_dash=0 | guest lr_dash=1 did_dash=1)` → STATUS_FORK@442 (Turn vs Dash) → FC figh@442 `inputs_agree=1` → PEER_SNAPSHOT kill.

## Symptom

After the healed-frontier fixes, the earliest remaining fork was a Turn→Dash split: the
peer that took a mid-Turn ring load lost `turn.lr_dash` (InvertLR dash pin), so its
DashCheckTurn gate never fired while the other peer dashed. Presented as
`REPLAY_DETERMINISM` FC with matching inputs.

## Root cause — C2 was half-wired

The C2a/C2b migration left a hole between capture and forward sim:

- `syNetRbSnapCaptureFighterStatusVarsFromLive` (C2a) reads **`bank[expected]`** for owned
  statuses, and apply restores **blob → bank → union projection** (C2b).
- But the `ftStatusVars*()` accessors still returned **union** pointers, and nothing ever
  synced union → bank during forward sim (`syNetplayStatusVarsBankCopyOverlayIn` was only
  called at apply; `BankInitFighter` had no external caller).

So `bank[Turn]` held zeros (or bytes from the last apply). Every mid-Turn ring save
captured that stale slot, and every load projected it back over the live union — wiping
`lr_dash`/`lr_turn` mid-Turn. The entry-sticky mirror (`sSYNetplayTurnEntryLrDash`) +
`syNetplayHardenTurnLrDash/LrTurn` band-aids papered over this, with their own tick-gating
bugs (soaks `1929938261`, `1646535146`, `1579824759`).

A second structural gap: the Turn overlay **outlives the Turn status**. Dash tap buffering
(`ftcommondash.c`) and AttackS4 read `lr_dash`/`lr_turn` from Wait..Ottotto, so a snapshot
whose live tag is not Turn still must carry Turn bytes — the single tagged payload cannot.

## Fix (architecture, not another exception)

1. **Accessor redirect (bank = forward-sim authority for Turn).**
   `ftStatusVarsTurn()` now returns `bank[Turn]` through
   `syNetplayStatusVarsBankAuthoritySlot()` when `syNetplayRollbackSemanticsActive()`;
   offline modes in the netmenu binary keep vanilla union aliasing (directive 7). All Turn
   readers/writers were already accessor-routed, so no raw-union readers remain. Turn
   SetStatus initializes every field, so the slot needs no union seeding.
2. **Blob `turn_vars` sidecar.** `SYNetRbSnapFighterBlob` gains a
   `sizeof(ftCommonTurnStatusVars)` sidecar captured from `bank[Turn]` on **every** save and
   restored into the bank on **every** apply (before union re-projection), independent of
   `status_vars_overlay`. Mid-Turn loads and dash-tap-window loads now round-trip exactly.

Files: `decomp/src/ft/ftstatusvars.h`, `port/net/sys/netplay_statusvars_bank.{c,h}`,
`port/net/sys/netrollbacksnapshot.c`.

## Retirement path (follow-up)

With bank authority + sidecar, the entry-sticky mirror and Harden repairs
(`syNetplayTurnNoteEntryLrDash`, `syNetplayHardenTurnLrDash`, `syNetplayHardenTurnLrTurn`,
`syNetplayTurnSyncEntryLrDashAfterLoad`) become redundant no-ops for correct runs. Keep one
soak cycle for evidence, then delete (they are mirrors — directive 6 forbids keeping them as
the primary mechanism). Other overlays migrate the same way: redirect accessor + (only if
the overlay is read outside its owning status) a dedicated sidecar.

## Verification

- Netmenu Debug build clean (`cmake --build build --target ssb64`).
- Soak pending: rerun seed `3066947259` pairing; expect no `TURN_DASH_LR_DASH_FORK`, no
  `STATUSVARS_TAG_HEAL` storm, `harden_lr_dash` witness lines gone with
  `SSB64_TURN_DASH_WITNESS=1`.

## Related

- `docs/refactor/ftstatusvars_overlay_map_2026-06-02.md` (Approach C map)
- [tag heal unowned sticky](netplay_statusvars_tag_heal_unowned_sticky_2026-07-28.md)
- [turn lr_dash scrub synctest](netplay_turn_lr_dash_statusvars_scrub_synctest_2026-07-26.md)
- [fc healed frontier survive deepen](netplay_fc_healed_frontier_survive_deepen_2026-07-28.md)
