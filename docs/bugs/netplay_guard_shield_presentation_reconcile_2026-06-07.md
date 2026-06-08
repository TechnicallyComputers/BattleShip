# Guard shield solid bubble + R→shield after spam — 2026-06-07

**Status:** FIX SHIPPED (soak pending)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Shield-spam soak (Android host + Linux guest, synctest off, `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`):

- Shield bubble renders **opaque/solid** instead of vanilla translucent (`efManagerShieldProcDisplay` alpha `0xC0`).
- After sustained spam, **R/grab throws up shield** instead of grab.
- Logs: 188× `guard_shield_ensure path=ok reason=guard_on_missing` (always respawns GObj 1011), 1626× `guard_shield_prune path=keep reason=guard_id_rebind_live`, 0× `guard_shield_heal`, many `guard_shield_linger_end … is_shield=1 z_auth=1` on GuardOff (154).

Synctest-on run: shields still looked correct (translucent) but `SYNCTEST_FAIL` / `LOAD_HASH_DRIFT` persisted — separate cross-ISA fighter/anim blob track, not this presentation fix.

## Root cause

1. **Duplicate spawn / decouple churn:** `ReconcileFighterShieldCoupling` cleared valid `guard->effect_gobj` when `FindLiveShieldEffectForFighter` returned NULL (pointer-only match on `ep->fighter_gobj`). That triggered `TryEnsureLiveShieldEffectForFighter` → `efManagerShieldMakeEffect` every GuardOn tap while the vanilla bubble still lived on the effect list (counted by `shield.player`). Duplicate bubbles → endless `guard_id_rebind_live` without eject; extra GObj may miss display proc → solid appearance.

2. **Ownership mismatch:** `LiveShieldEffectOwnedByFighter` rejected player-slot match when `ep->fighter_gobj` was decoupled but pointed at a stale pointer, while `CountLiveShieldEffectsForPlayer` still counted the bubble.

3. **Stale `is_shield` / grab trap:** `ShieldHealEligible` blocked heal on `z_held` before Wait/GuardOff-end checks; 0 heal lines in soak while `is_shield=1` lingered into grab attempts.

## Fix

1. **`LiveShieldEffectOwnedByFighter`:** `shield.player` + resolved fighter GObj is authoritative; drop early reject on mismatched `ep->fighter_gobj`.

2. **`FindLiveShieldEffectForFighter`:** Use ownership helper (player slot + fighter GObj), not raw pointer equality.

3. **`ReconcileFighterShieldCoupling`:** When `guard_gobj` resolves to a live shield but finder missed it, **adopt/repair** via `AuditLiveShieldEffectOwner` instead of clearing coupling.

4. **`TryEnsureLiveShieldEffectForFighter`:** Before spawn, adopt resolved `guard->effect_gobj` when it is a valid owned shield.

5. **`ShieldHealEligible`:** Heal Wait, GuardSetOff, and release-complete GuardOff (`release_lag==0`, `is_release`) **before** active-guard and `z_held` gates.

6. **`CaptureFighterCoupledIds`:** When `guard->effect_gobj` is NULL but a live shield GObj exists for the player (`FindLiveShieldEffectForFighter`), capture its id into `guard_effect_gobj_id` so eff-only LOAD_HASH_DRIFT (`live=1011 blob=0`) does not occur on Guard (153).

## Verification

Re-soak with `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`:

- Expect near-zero `guard_on_missing` on normal taps (vanilla bubble adopted).
- Expect `guard_shield_heal` on Wait/GuardOff-end stale `is_shield`.
- Fewer `guard_id_rebind_live` lines; translucent shields on synctest off.
- R/grab should not re-enter shield after spam when bubble is gone.
