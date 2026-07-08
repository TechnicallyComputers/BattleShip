# Yoshi egg second capture early escape — 2026-07-07

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Scope:** `decomp/src/ft/ftcommon/ftcommoncaptureyoshi.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

Soak2 (Samus vs Yoshi): victim time inside the egg is inconsistent across captures. The first egg often lasts the expected mash window (~210 frames), but the **second capture usually escapes early** — sometimes almost immediately — instead of matching vanilla.

## Root cause

Netplay adopts a recycled egg-lay effect shell on `YoshiEgg` entry (`syNetRbSnapTryAdoptLiveYoshiEggLayEffectForFighter`) to avoid twin shells after rollback reconcile. Fresh mints call `ftCommonYoshiEggBeginPrepareAnimForNetplay` (index 0 / prepare wiggle); **adopted shells skipped that reset**.

After the first escape, leftover shells could retain:

- `index == 1` with `anim_frame <= 0` → `ftCommonYoshiEggProcUpdate` treats as instant escape
- `index == 1` / `force_index == 1` / break anim frame from prior hatch prep → shortened visible wiggle before auto-escape

Prior escape hatch cosmetics (hidden rollback shells) could also linger on the victim GObj until ejected.

## Fix

| Change | Purpose |
|--------|---------|
| `ftCommonYoshiEggMakeEffect` | After successful adopt, call `ftCommonYoshiEggBeginPrepareAnimForNetplay` (same as fresh mint) |
| Adopt gated on `effect_gobj==NULL` | `MakeEffect` runs every `ProcUpdate` tick; unconditional prepare reset restarted index-0 wiggle every frame (no visible wiggle) |
| `syNetRbSnapPickYoshiEggLayForCaptureAdopt` | Prefer prepare shells (index 0, lowest anim_frame) over break-phase orphans; `PickCanonical` highest-frame pick was wrong for capture entry |
| `ftCommonYoshiEggProcUpdate` countdown narrowed | Netmenu 15-tick countdown ran in parallel with the ~24f break anim and always won the race (~9f early escape, deterministic on both peers). Now a stalled-shell fallback only: ticks when the shell has not entered break anim (`index != 1`); with a progressing break anim, vanilla `index==1 && anim_frame<=0` drives escape |
| Blob: `child_anim_frame` | Egg-lay break/wiggle anim lives on the child DObj; only the root GObj `anim_frame` round-tripped. Saved (quantized) for effects with a child DObj, restored on `YOSHI_EGG_LAY` apply. Not folded into the effect hash |
| Apply: unconditional egg-lay anim rebind | `SetAnim` was skipped when `force_index == index` (both 1), so a recycled shell could keep the wrong joint-anim binding while `anim_frame` counted down invisibly. Apply now rebinds to `index` always, then restores root + child anim frames |
| `syNetRbSnapTryAdoptLiveYoshiEggLayEffectForFighter` | Eject prior hatch cosmetics; reject/eject break-complete shells (`HatchAlreadyComplete`) and force fresh mint |
| `syNetRbSnapEjectYoshiEggLayHatchCosmeticsForFighter` | Helper to clear hidden hatch shells before re-capture adopt |
| Hatch cosmetic eject guard | Match hatch shells by bank/proc (not blanket hidden-from-rollback); require live `ep` before `fighter_gobj` read — fixes SIGSEGV @553 when gobj 1011 had `ep=nil` during adopt |

## Verify

Yoshi neutral-B → egg Samus twice in one stock under rollback. Both eggs should require the full mash/auto-escape timing (no instant break on second entry). Re-run soak2-linux + soak2-android.

Related: [netplay_yoshi_egg_lay_hatch_rollback_2026-06-05.md](netplay_yoshi_egg_lay_hatch_rollback_2026-06-05.md), [netplay_yoshi_egg_verify_figh_drift_2026-07-07.md](netplay_yoshi_egg_verify_figh_drift_2026-07-07.md).
