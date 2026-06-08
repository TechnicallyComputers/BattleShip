# Guard shield solid bubble + R→shield after spam — 2026-06-07

**Status:** FIX SHIPPED (Phase 37 soak pending)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Shield-spam soak (Android host + Linux guest, synctest off, `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`):

- Shield bubble renders **opaque/solid** instead of vanilla translucent (`efManagerShieldProcDisplay` alpha `0xC0`).
- After sustained spam, **R/grab throws up shield** instead of grab.
- Logs: 188× `guard_shield_ensure path=ok reason=guard_on_missing` (always respawns GObj 1011), 1626× `guard_shield_prune path=keep reason=guard_id_rebind_live`, 0× `guard_shield_heal`, many `guard_shield_linger_end … is_shield=1 z_auth=1` on GuardOff (154).

Synctest-on run: shields still looked correct (translucent) but `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` persisted — separate cross-ISA fighter/anim blob track, not this presentation fix.

**Phase 37 follow-up (2026-06-07):** Solid shields persisted in both `SYNCTEST=0` and `SYNCTEST=1` gameplay after Phase 36 presentation fixes. Logs at tick 494 showed `effect_count=4` with three shield bubbles (`eff_fold_diag`: `respawn=2`, `shield_player=1`, all `gobj_id=1011`) and two `gobj_alloc … id=1011` with **different pointers** in one tick. Dedupe logged `guard_id_rebind_live` (keep) but never `guard_id_rebind_duplicate` / `reason=duplicate` eject.

## Root cause

### Phase 36 (spawn/adopt/heal)

1. **Duplicate spawn / decouple churn:** `ReconcileFighterShieldCoupling` cleared valid `guard->effect_gobj` when `FindLiveShieldEffectForFighter` returned NULL (pointer-only match on `ep->fighter_gobj`). That triggered `TryEnsureLiveShieldEffectForFighter` → `efManagerShieldMakeEffect` every GuardOn tap while the vanilla bubble still lived on the effect list (counted by `shield.player`). Duplicate bubbles → endless `guard_id_rebind_live` without eject; extra GObj may miss display proc → solid appearance.

2. **Ownership mismatch:** `LiveShieldEffectOwnedByFighter` rejected player-slot match when `ep->fighter_gobj` was decoupled but pointed at a stale pointer, while `CountLiveShieldEffectsForPlayer` still counted the bubble.

3. **Stale `is_shield` / grab trap:** `ShieldHealEligible` blocked heal on `z_held` before Wait/GuardOff-end checks; 0 heal lines in soak while `is_shield=1` lingered into grab attempts.

### Phase 37 (dedupe identity)

4. **Id-equality dedupe no-op:** `syNetRbSnapShieldEffectMatchesKeep` treated `gobj->id == keep_gobj->id` as a match. Shield effects share a fixed pool link id (1011); rollback ensure/respawn can mint multiple live GObjs with the same id. `PruneDuplicateShieldEffects` and `guard_id_rebind_live` in `ReconcileFighterShieldCoupling` skipped eject for every duplicate because all shared id 1011. Each bubble still ran `efManagerShieldProcDisplay` (alpha `0xC0`); **stacked** translucent shells composite to opaque — not a missing render proc.

## Fix

### Phase 36

1. **`LiveShieldEffectOwnedByFighter`:** `shield.player` + resolved fighter GObj is authoritative; drop early reject on mismatched `ep->fighter_gobj`.

2. **`FindLiveShieldEffectForFighter`:** Use ownership helper (player slot + fighter GObj), not raw pointer equality.

3. **`ReconcileFighterShieldCoupling`:** When `guard_gobj` resolves to a live shield but finder missed it, **adopt/repair** via `AuditLiveShieldEffectOwner` instead of clearing coupling.

4. **`TryEnsureLiveShieldEffectForFighter`:** Before spawn, adopt resolved `guard->effect_gobj` when it is a valid owned shield.

5. **`ShieldHealEligible`:** Heal Wait, GuardSetOff, and release-complete GuardOff (`release_lag==0`, `is_release`) **before** active-guard and `z_held` gates.

6. **`CaptureFighterCoupledIds`:** When `guard->effect_gobj` is NULL but a live shield GObj exists for the player (`FindLiveShieldEffectForFighter`), capture its id into `guard_effect_gobj_id` so eff-only LOAD_HASH_DRIFT (`live=1011 blob=0`) does not occur on Guard (153).

### Phase 37

7. **`syNetRbSnapShieldEffectMatchesKeep`:** Match **pointer identity only** (`gobj == keep_gobj`); remove `gobj->id == keep_id` branch. Lets `PruneDuplicateShieldEffects` eject extras (`reason=duplicate`) and lets `guard_id_rebind_live` fall through to `guard_id_rebind_duplicate` eject when `guard_eff != gobj` and count > 1.

### Phase 38 (2026-06-08) — shield stamina drain stops during sustained hold

**Symptom:** Offline: holding shield drains `shield_health` every 16 frames (`FTCOMMON_GUARD_DECAY_INT`). Rollback synctest: drain runs a few ticks then stops; logs show `shield_health=55` stuck, ~6-frame GuardOn→GuardOff cycles, double `guard_shield_ensure`, and `guard_ptr_mismatch` eject on every GuardOn.

**Cause:** (1) `EnsureLiveShieldEffectsOnAuthHold` ran twice per live-forward reconcile (before and after prune), minting duplicate id-1011 bubbles when vanilla already owned one. (2) Phase 37 pointer-only `MatchesKeep` left the `guard_eff != gobj` prune branch dead — it always fell through to `guard_ptr_mismatch` eject instead of adopt/rebind. (3) Ensure spawned even when `CountLiveShieldEffectsForPlayer > 0`.

**Fix:** Single post-prune ensure pass; `guard_ptr_adopt` / `guard_ptr_duplicate` replace blind `guard_ptr_mismatch` eject; skip ensure spawn when player slot already has a live shield. **`guard_shield_stamina` diag** logs `shield_health`, `shield_decay_wait`, `is_release`, `release_lag`, `z_live`, bubble presence on change (gated `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`).

**Verify:** Sustained hold should show monotonic `shield_decay_wait` countdown and periodic `shield_health` drops in `guard_shield_stamina` lines; zero `guard_ptr_mismatch`; at most one `guard_shield_ensure path=ok` per GuardOn entry; no `player_slot_bubble_exists` spam during hold.

## Impact

| Area | Before Phase 37 | After Phase 37 |
|------|-----------------|----------------|
| Visual | 2–3 stacked shield GObjs → opaque bubble (synctest on/off) | Single bubble → vanilla translucent `0xC0` |
| Dedupe logs | Only `guard_id_rebind_live` keeps; no `reason=duplicate` eject | Expect `guard_id_rebind_duplicate` or `reason=duplicate` when extras exist |
| `eff_fold_diag` | Multiple `respawn=2` shield rows same player/id | `count=1` shield row per player during Guard |
| Hash / sync | Unchanged (eff hash already folded stacked bubbles; dedupe reduces churn) | Cleaner effect list; fewer spurious ensure spawns |

**Shared helper:** `syNetRbSnapYoshiEggLayEffectMatchesKeep` delegates to this function — same id-collision class for neutral-B egg shell duplicates. Yoshi Z egg shield uses separate Yoshi shield MakeEffect path but shares dedupe via `PruneDuplicateShieldEffects`. Yoshi soak tracked separately; see [netplay_yoshi_egg_shield_rollback_2026-06-06.md](netplay_yoshi_egg_shield_rollback_2026-06-06.md) and [netplay_yoshi_egg_lay_2026-06-01.md](netplay_yoshi_egg_lay_2026-06-01.md).

## Verification

Re-soak with `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1` (and optionally `SSB64_NETPLAY_EFFECT_FOLD_DIAG=1`):

- Expect near-zero `guard_on_missing` on normal taps (vanilla bubble adopted).
- Expect `guard_shield_heal` on Wait/GuardOff-end stale `is_shield`.
- **Phase 37:** Expect `guard_id_rebind_duplicate` or `guard_shield_prune … reason=duplicate` when rollback mints extras; **no** sustained `effect_count≥2` shield rows for one player in `eff_fold_diag`.
- Translucent shields on synctest off **and** on.
- R/grab should not re-enter shield after spam when bubble is gone.
