# Netplay DK Jungle TaruCann synctest + fire gating — 2026-06-09

**Status:** FIX SHIPPED (fourth pass — reconcile presentation; soak pending)

## Symptom

Kirby vs Pikachu DK Jungle synctest soak: `SYNCTEST_FAIL` @ tick 869 on both peers. Pikachu in `TaruCann` (~105f into barrel). Gameplay: barrel shoot anim + SFX play but fighter not ejected until manual A/B — auto-fire gated.

With `SSB64_NETPLAY_ROLLBACK_SYNCTEST=0`, auto-eject works (confirmed `netplay-trimmed-syncon.log` soak). Bug is synctest emergency-restore specific.

## Root cause

Three coupled issues (hash drift — fixed in first pass):

1. **Map hash round-trip:** Phase 5c `slot->barrel` is authoritative for barrel DObj anim, but jungle ground v3 still folds legacy `root_anim_wait_bits` / `child_anim_wait_bits`. After `syNetRbSnapApplyBarrel`, verify re-capture disagreed on `child_anim_wait_bits` (offset 32) → `LOAD_HASH_DRIFT` / soft-continue blocked.

2. **Rider wait hydration (probe ticks):** Ground v3 captures `tarucann_rider_release_wait` / `shoot_wait` at save, but restore never copied them back onto the fighter before `ftCommonTaruCannReconcileShootStateAfterRollback`. Reconcile saw `release_wait=0` while snap had 105 → auto-fire timer reset; shoot presentation could run without armed `shoot_wait`.

3. **Hash oracle mismatch:** Blob light hash folded TaruCann waits; live `syNetSyncHashFighterStructLight` did not → persistent `full_ok=0` / `blob_ok=0` while riding.

**Auto-fire still broken after hash fix (second pass):** `syNetRbSnapResyncFighterTaruCannGobjs` looked up ground jungle via `syNetRbSnapshotSlotForTick(snap_tick)` and skipped `snap_tick == 0xFFFFFFFF`. Synctest `RestoreLiveEmergency()` applies the emergency sentinel slot every ~120 ticks; resync ran with `snap_jungle == NULL` → `release_wait=0` after restore. Forward sim could accumulate at most ~119 frames between probes (`180` never reached). Manual A/B still worked via `ProcInterrupt` / reconcile input history.

**Still broken after slot-based resync (third pass):** Ring probe hydrate worked (`resync_pre snap_riders=1`) but emergency restore still logged `snap_riders=-1` on Android while `ctx=restore` showed valid `snap_release_wait`. `syNetRbSnapGroundJunglePayloadFromSlot` used stricter slot-metadata guards than restore diag (`src` + `payload_len`). Fix: hydrate from the same `src`/`payload_len` in `syNetRbSnapEnsureJungleTaruCannAfterParticleReset` immediately after restore, before ApplyBarrel/reconcile; unify rider payload lookup via `syNetRbSnapGroundJungleRiderPayload`.

**Premature shoot presentation after auto-fire worked (fourth pass):** `netplay-trimmed-syncon.log` — eject @ ~180f OK, but two failed fire anims (right after entry @509 synctest, again just before real launch). Reconcile `release_wait >= 180` catch-up re-armed `shoot_wait` + `AddAnimShoot` on restore while hydrate kept `shoot_wait=0`; unconditional `AddAnimFill` restarted child anim every restore (entry pop). Fix: reconcile only syncs shoot anim when `shoot_wait > 0` or manual input edge; suppress orphan shoot joint only; auto-fire threshold left to `ftCommonTaruCannProcUpdate`.

## Fix

| Area | Change |
|------|--------|
| `netrollbacksnapshot.c` | `syNetRbSnapFoldJungleGroundPayloadHashBytes`: exclude 8-byte anim_wait window from jungle map hash when v3 rider tail present |
| `netrollbacksnapshot.c` | `syNetRbSnapHydrateTaruCannRiderWaitsFromGround`: apply v3 rider waits before reconcile |
| `netrollbacksnapshot.c` | `syNetRbSnapGroundJungleRiderPayload` + `syNetRbSnapHydrateAllTaruCannRidersFromGround`: hydrate from restore `src`/`payload_len` in EnsureJungle (before ApplyBarrel); slot resync uses same rider payload helper |
| `netsync.c` | Fold TaruCann `release_wait` + `shoot_wait` in live light hash (mirror blob / Twister) |
| `ftcommontarucann.c` | Reconcile: drop `release_wait >= 180` restore catch-up + unconditional fill restart; orphan shoot suppress only |

## Soak pass criteria

DK Jungle netplay with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`:

- No `SYNCTEST_FAIL` on barrel ride ticks (~764–869 in repro)
- No `map_hash_ground_payload … first_off=32` + `map_mismatch` soft-continue block
- `jungle_tarucann_rider` shows `release_wait` matching `snap_release_wait` after restore **including `tick=4294967295` emergency restore**
- Auto-fire @ 180f ejects on first cycle without manual press after rollback overlap
- Single shoot anim on auto-fire (no premature pops @ synctest entry or pre-180 restore)

## Related

- [`netplay_dk_jungle_tarucann_crash_2026-05-30.md`](netplay_dk_jungle_tarucann_crash_2026-05-30.md) — Phases 4–5c barrel partition family
