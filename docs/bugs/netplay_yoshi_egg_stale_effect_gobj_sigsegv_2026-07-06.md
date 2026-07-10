# Netplay — YoshiEgg stale `effect_gobj` SIGSEGV (soak2)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptoms

- Both peers SIGSEGV at tick 529–530 during Samus-in-egg / Yoshi egg-lay window.
- `fault_addr=0x8`, `x0=0x7ff1307a4e98`, `x1=0x0` — NULL `EFStruct*` deref on `ep->effect_vars` offset 8.

## Log evidence (soak2-linux @ tick 521 load / 529 crash)

- Resim load @519 after input mismatch @520.
- Load apply ejects stale `gobj_id=1011` shells (`obj=nil`) then immediately recycles the same address for the live egg-lay effect.
- Fighter blob apply binds victim `effect_gobj` **before** prune/eject; coupling survives eject with `user_data.p=nil`.
- Crash tick 529: register `x0` matches recycled shell address `0x7ff1307a4e98`.

## Root cause

`ftCommonYoshiEggProcUpdate` dereferences `efGetStruct(effect_gobj)` without a NULL guard. After rollback load, victim `status_vars.captureyoshi.effect_gobj` can point at a recycled GObj shell whose `user_data.p` was cleared during eject. `syNetRbSnapEjectGObj` did not clear victim YoshiEgg coupling on eject, so the stale pointer survived id recycle.

`efManagerYoshiEggLayProcUpdate` already guards `ep==NULL`; the fighter proc did not.

## Fix

1. **`ftCommonYoshiEggProcUpdate`** — if `ep==NULL` under rollback semantics: clear coupling, `syNetRbSnapSanitizeCaptureYoshiEffectGobj`, try live adopt.
2. **`syNetRbSnapEjectGObj`** — `syNetRbSnapClearAllFightersCaptureYoshiEffectPointerIfMatch` before eject (load-time recycle safety).
3. **`syNetRbSnapSanitizeCaptureYoshiEffectGobj`** — explicit `efGetStruct==NULL` clear before ownership checks.
4. **`syNetRbSnapTryAdoptLiveYoshiEggLayEffectForFighter`** — require live `efGetStruct` before treating coupled pointer as valid.

## Files

- `decomp/src/ft/ftcommon/ftcommoncaptureyoshi.c`
- `port/net/sys/netrollbacksnapshot.c`
