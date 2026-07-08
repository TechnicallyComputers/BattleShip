# Fox reflector stale shell forward-sim SIGSEGV — 2026-07-08

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`

## Symptom

soak2 (Fox vs Kirby, SYNCTEST OFF): both Linux AppImage and Android client `SIGSEGV` at tick **1725** when Fox down-B reflector hits Kirby and Fox enters `DamageFly`.

```
SSB64: gobj_alloc id=1011 … frame=1744          /* shock small */
SSB64: ftMainSetStatus status=0x1f motion=25    /* Fox DamageFly */
SSB64: gobj_alloc id=1011 …                     /* Kirby hit spark particle path */
SSB64: gcEjectGObj sim_tick=1725 gobj=… id=1011 /* stale Fox reflector from tick ~1705 */
SSB64: !!!! CRASH SIGSEGV fault_addr=0x7f4a00000037 pc=func_ovl2_801031E0 (lbParticleMakeScriptID)
```

Ring snapshots at ticks 1723–1724 had `effect_count=0` while the live reflector shell (`respawn=4`, parent Fox) still sat on link 6 from the earlier reflector window (ticks 1705–1707).

## Root cause

1. **`syNetRbSnapPruneStaleFoxReflectors`** only ran on snapshot reconcile/apply — not on the live forward-sim frontier. After Fox left reflector scope (`status=26` Wait) the shell persisted for ~20 ticks until a damage hit spawned new FX on the same link.
2. **`syNetRbSnapClearFighterEffectPointerIfMatch`** only cleared `status_vars.fox.speciallw.effect_gobj` while Fox was **in** reflector scope. Prune/eject out of scope left a dangling coupled pointer + `is_effect_attach` on the fighter blob path.
3. Mid-tick eject of the stale reflector during Kirby damage spark allocation (`func_ovl2_801031E0` / `lbParticleMakeScriptID`) corrupted particle state → SIGSEGV (`x0=0x7f4a00000009`, `fault_addr=0x7f4a00000037`).

Round-3 Kirby inhale zombie sweep (2026-07-07) stopped **erroneously** ejecting the reflector every forward tick; the reflector then survived into the hit window where the missing forward prune + broken pointer clear surfaced this crash class.

## Fix

| Change | Purpose |
|--------|---------|
| `syNetRbSnapClearFighterEffectPointerIfMatch` | Clear Fox `speciallw.effect_gobj` whenever the ejected GObj matches (drop in-scope guard) |
| `syNetRbSnapEjectFoxReflectorEffectGObj` | Dedicated teardown: clear fighter coupling, joint `user_data.p`, EFStruct pool, then `gcEjectGObj` |
| `syNetRbSnapEjectGObj` early route | Detect `efManagerFoxReflectorProcUpdate` shells and use dedicated eject |
| `syNetRbSnapPruneStaleFoxReflectors` | Call dedicated eject for all prune paths |
| `syNetRbSnapForwardPruneStaleFoxReflectors` | `syNetRbSnapPruneStaleFoxReflectors(NULL)` from `syNetRollbackAfterBattleUpdate` (mirrors Kirby inhale wind forward prune) |
| `syNetRbSnapFillSlotFromLive` | Prune stale reflectors before fighter capture (hash/save safety) |

## Verification

- soak2 Fox vs Kirby, SYNCTEST OFF: Fox reflector hit on Kirby at ~tick 1725 — no SIGSEGV.
- Confirm reflector VFX still animates during active reflector (round-3 inhale sweep regression).
- Confirm Kirby inhale wind still clears on release (prior fix).

## Code pointers

| Area | Symbol |
|------|--------|
| Forward prune | `syNetRbSnapForwardPruneStaleFoxReflectors` |
| Dedicated eject | `syNetRbSnapEjectFoxReflectorEffectGObj` |
| Stale prune | `syNetRbSnapPruneStaleFoxReflectors` |
| Pointer clear | `syNetRbSnapClearFighterEffectPointerIfMatch` |
| AfterBattleUpdate hook | `syNetRollbackAfterBattleUpdate` |
