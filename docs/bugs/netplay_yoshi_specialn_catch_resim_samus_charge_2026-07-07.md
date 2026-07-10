# Netplay — Yoshi SpecialN catch lost on resim vs Samus charge (soak2 @520)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom (soak2 Samus vs Yoshi, Linux + Android)

- Yoshi neutral B appears to whiff on Samus during charge loop, then works after she exits charge.
- Soak2 tick **520**: forward sim **does** land egg (P0 `CaptureYoshi` 177, P1 `SpecialNCatch` 229) while Samus still in `SpecialNLoop` (223).
- Rollback episode (`FORCE_MISMATCH` @520) loads ring@519 and resims 520–521; authoritative timeline **loses** the grab.
- Yoshi tongue stays out (228) through ~535 with no re-catch; second B @575 succeeds after Samus returns to Wait.

## Root cause

1. **Not charge hitbox immunity** — vanilla catch succeeds on forward path with coupled charge-shot weapon present.

2. **Full egg-lay presentation repair ran during pre-catch SpecialN extend (228)**  
   `syNetRbSnapRefreshYoshiEggLayPresentationFromSlot` (modelpart cosmetic + figatree hard-pin) was keyed off `YoshiEggLayPresentationScope`, which includes attacker status 228. That scope is correct for post-catch / victim phases but poisons **frame-critical tongue catch colliders** on rollback load and resim replay.

3. **Samus charge presentation skipped on load**  
   `PrepareLoadedSlotForVerify` used `else if` — egg-lay branch won over `RefreshSamusChargePresentationFromSlot`, so charge-shot position could drift on the load path concurrent with Yoshi tongue extend.

4. **Stale attack_records** (secondary) — prior tongue/charge-ball interactions could block `ftMainSearchCatch` re-connect on replay ticks via `is_interact_hurt` / `group_id != 7`.

## Fix (`port/net/sys/netrollbacksnapshot.c`)

Split Yoshi egg-lay repair into two scopes:

| Scope | Statuses | Repair path |
|-------|----------|-------------|
| **Pre-catch extend** | Yoshi/Kirby-copy `SpecialN` / `SpecialAirN` (228/231), no victim | `syNetRbSnapRefreshYoshiSpecialNPreCatchExtendGameplayFromSlot`: VsLoad joint fidelity, Samus charge refresh, catch-params rebind, attack-record sanitize, `ftParamRefreshAttackCollID` |
| **Full egg-lay** | `CaptureYoshi`, `YoshiEgg`, Catch/Release (229/230…) | Existing `RefreshYoshiEggLayPresentationFromSlot` + effects |

Applied on: `ApplyFighter` hard-pin, `FillSlotFromLive` tail, `PrepareLoadedSlotForVerify`, `ResyncLiveFightersFromSlotForSim`, `RefreshIntroPresentationAfterForwardResimTick`, `PrepareYoshiEggLayForVerifyHash`, weapon verify repair gate.

## Verify

Re-run soak2 Samus vs Yoshi through tick 535 with `FORCE_MISMATCH` @520:

- Resim 519→522 reproduces 229/177 at 520 **or** re-catch before Yoshi returns to Wait @536.
- `prepare_verify_yoshi_specialn_precatch` log tag instead of `prepare_verify_yoshi_egglay` at load@519.
- Charge weapon ejected (`weapon_count=0`) on successful resim capture @520.
