# DamageE2 multi-hit resist snapshot hole (`is_knockback_paused` / `damage_knockback_stack`)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** `1952491642` (Android follower / Linux initiator), FC@670 `figh` inputs MATCH

## Symptom

- Pair/seed OK; synctest clean (`LOAD_HASH_DRIFT=0`).
- Episode `mismatch=647 load=646 target=652` `correction_player=1` (P1 DamageE2 under Ness jibaku multi-hit).
- Load fighter_detail P1 matched bit-identically (`fhash=0x44ACCD9F`, `hitlag=6`, `status_tics=16`, `dmg=60`, same `vel_dmg_air`).
- Seals matched across the wire.
- `rollback_post` P1 diverged:
  - Linux initiator: `status_tics=4`, **new** `vel_dmg_air`, `dmg=90`, `hitlag=18` → full `GotoDamageStatus` re-entry.
  - Android follower: `status_tics=21`, **stale** load `vel_dmg_air`, `dmg=90`, `hitlag=18` → ColAnim-only path.
- Forward sim never re-agreed → FC@670 / PHYSICS_FORK@670 / STATUS_FORK@671 (cascade).

## Root cause

`ftCommonDamageUpdateMain` chooses ColAnim vs status re-entry with:

```c
hitlag_tics > 0 && is_knockback_paused && (damage_knockback < damage_knockback_stack + 30.0F)
```

`SYNetRbSnapFighterBlob` restored `hitlag_tics` / `damage_queue` / element but **not** `is_knockback_paused`, `damage_knockback_stack` (or pending `damage_knockback` / `damage_lag`).

After `ness_pk_defer` + follower `BASELINE_PREEMPTIVE_LIVE_CAP` (live ≈667, load 646), apply put ring `hitlag=6` back while leaving **live frontier** paused/stack. Same seals → different resist branch on the next electric multi-hit.

SoftLip / TopN@670 were not the writer.

## Fix

1. Capture/apply on fighter blob: `is_knockback_paused`, `damage_knockback_stack`, `damage_knockback`, `damage_lag`.
2. Hard-pin those (+ `hitlag_tics`) after figatree so load verify matches ring.
3. Fold into `fhash_light` / blob light (+ second-layer `fighter_field_diff`).
4. Always-on `DAMAGE_RESIST_BRANCH` log under active VS when `kb!=0 && hitlag>0`.

## Acceptance

Ness jibaku multi-hit on a DamageE2 victim across deferred/live-capped rewind:

- Matching `rollback_post` P1 `status_tics` / `vel_dmg_air` / `fhash`.
- Matching `DAMAGE_RESIST_BRANCH` (`colanim` vs `status_reentry`) on both peers during resim.
- No FC `figh` with inputs MATCH from this class.
