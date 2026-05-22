# Mario / Luigi empty fireball after rollback resim

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (phase 4.4 — soak pending)  
**Subsystem:** `decomp/src/ft/ftchar/ftmario/ftmariospecialn.c`

## Symptom

Netplay soak: Mario plays neutral-B throw anim but no fireball appears (`wpn=0x811C9DC5` for many frames while `status=190 motion=165`). Same class as Yoshi empty egg throw after synctest / missed anim event.

## Root cause

`ftMarioSpecialNProcAccessory` only spawns when `motion_vars.flags.flag0` is set by the anim event. After rollback restore or resim landing mid-`SpecialN`, `flag0` is already cleared and the event does not re-fire.

## Fix

| Area | Change |
|------|--------|
| **ProcAccessory** | PORT: `flag0` path unchanged; if `flag1==0` and `anim_frame >= 6`, emergency spawn once per status entry. |
| **Dedup** | `syNetRbSnapHeldItemWeaponNeedsSpawn(..., nWPKindFireball, ...)` before `wpMarioFireballMakeWeapon`. |
| **InitStatusVars** | Clear `flag1` with `flag0` on SpecialN entry (spawn-once marker). |

### Phase 4.3 regression (2026-05-20 soak)

Emergency spawn + hand-position dedup caused **double spawns** after rollback: anim-event fireball moved >60 units from hand by frame 6, dedup missed it, emergency added a second. Synctest retained all blobs → phantom hits.

| Area | Phase 4.3 change |
|------|------------------|
| **Spawn gate** | `flag1` blocks all further spawn attempts for this SpecialN entry. |
| **Dedup** | Existence check uses `NeedsSpawn(..., NULL, NULL)` — any owned fireball, not hand proximity. |
| **Near cull** | `syNetRbSnapCullOwnedFireballsNearPose` removes duplicate owned fireballs near spawn hand after spawn/skip. |

### Phase 4.4 (2026-05-20 soak follow-up)

Phase 4.3 `NeedsSpawn(..., NULL, NULL)` blocked spawn whenever *any* owned fireball existed (even off-stage), latched `flag1` on skip, and left mid-`SpecialN` empty throws when that ball was destroyed (tick 703 class).

| Area | Phase 4.4 change |
|------|------------------|
| **Hand dedup** | `syNetRbSnapFireballNeedsSpawnAtHand` — sim-slot ownership + ≤60 units from spawn joint only. |
| **Reacquire** | `syNetRbSnapReacquireFireballAtHand` — nearest owned fireball at hand, not stage-wide. |
| **flag1 latch** | Set only after successful spawn or hand reacquire; cleared when latched but no owned fireball remains (mid-status retry). |
| **Far fireballs** | Off-hand owned fireballs no longer block a new throw from the hand. |

Luigi shares `ftMarioSpecialNProcAccessory`.

## Verification

Soak: no multi-frame `SpecialN` windows with empty `wpn` hash after B tap. `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1` optional.

## Related

- [`netrollback_weapon_deferred_eject_2026-05-20.md`](netrollback_weapon_deferred_eject_2026-05-20.md) Phase 4.5 — apply orphan eject deferred until coupled rebind.
- [`yoshi_egg_orphan_duplicate_2026-05-20.md`](yoshi_egg_orphan_duplicate_2026-05-20.md) Phase 4.2 empty throw.
