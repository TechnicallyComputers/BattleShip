# Yoshi neutral-B egg hatch animation — rollback lifecycle — 2026-06-05

**Status:** FIX SHIPPED (soak pending — phase 13: synctest live-tail hatch ensure + hidden-shell eject fix)  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`

## Symptom

Cross-ISA soak: when a victim escapes Yoshi neutral-B egg (`YoshiEgg` / status 178), hatch shell animation and shatter VFX fire inconsistently — sometimes sim escape (Fall) runs without presentation.

Logs showed `effect_apply kind=YOSHI_EGG_LAY fighter_gobj_id=0` (parent not resolved) and eff hash drift after load while fighter blob matched.

## Root cause

1. **Egg-lay effect parent resolution** — effect blobs saved with `fighter_gobj_id=0` when `ep->fighter_gobj` was cleared; apply/respawn did not reverse-lookup victim via `captureyoshi_effect_gobj_id` (unlike guard shield `ResolveShieldParentGobj`).
2. **Live reconcile gap** — `PruneStaleYoshiEggLayEffects` returned early when `slot==NULL`, so orphan egg-lay effects survived between rollback loads.
3. **Hatch presentation** — escape-at-save (`Fall` + escape velocity) skips `ftCommonYoshiEggProcUpdate` on load; LBParticle hatch scripts were wiped by rollback particle reset with no replay, and the original egg-lay GObj was already absent so its break animation could not advance.
4. **SIGSEGV in `ftCommonYoshiEggProcInterrupt`** — `no_fighter` prune ejected egg-lay GObj without clearing `captureyoshi.effect_gobj` on victim; next tick wiggled `->child == NULL` (`fault_addr=0x20`).
5. **Replay-before-render + root mutation** — hatch replay fired during rollback load/resim, so the shell/particles could be advanced or reset before any rendered frame. Generic effect save quantization also touched the fighter-attached Yoshi egg root DObj; vanilla wiggle belongs to the child DObj, while the root must stay parent-derived from victim TopN.
6. **Post-escape ghost shell** — live escape always queued a cosmetic shell + immediate particles on flush, even when the capture egg had already finished break anim index 1 on a visible frame. Fighter popped to Fall first, then a second full break anim replayed from frame 0.
7. **Capture mash escape (phase 10)** — Z-shield / up-special tests left Yoshi-shield or mis-parented effects on shared GObj id 1011 while Kirby was in `YoshiEgg`, crowding out the victim egg-lay shell. Escape sim ran but hatch cosmetic could not start; deferred flush alone missed particles. Snapshot save deduped `EGG_ESCAPE` after `EGG_LAY` per victim so synctest logs showed `yoshi_egg_escape_duplicate` while two live effects remained.

## Fix

| Change | Purpose |
|--------|---------|
| `FindYoshiEggLayOwnerGobjFromSlot` / `ResolveYoshiEggLayParentGobj` | Resolve victim on apply, respawn, and capture backfill |
| Apply path | Re-parent egg-lay DObj to victim TopN joint; re-couple `captureyoshi.effect_gobj` |
| `ReconcileYoshiEggLayEffectsCore/Live/AtTick` | Ensure + prune + sanitize on load verify and live forward |
| `EnsureLiveYoshiEggLayEffects` | Respawn egg-lay shell on live/resim path when YoshiEgg has no valid effect |
| `ClearAllFightersEffectPointerIfMatch` | Decouple captureyoshi/guard pointers on orphan eject |
| `SanitizeCaptureYoshiEffectGobj` | Validate gobj id + DObj `child` before YoshiEgg interrupt |
| `ftCommonYoshiEggProcInterrupt` netmenu guard | Skip wiggle when egg DObj tree incomplete (rollback safety net) |
| `ReplayYoshiEggLayHatchCosmeticsFromSlot` | Replay explode/break particles on load when blob is post-escape Fall |
| Deferred hatch replay queue | Queue resim-detected hatch events and flush them after live battle update so shell + particles are born on a renderable frame |
| Hatch shell cosmetic | Spawn a rollback-only egg-lay shell on the escape frame, force anim index 1, and self-eject when the break animation ends |
| **`defer_particles` + live spawn** | Removed: visible live escape always queues break shell replay (`replay_shell=1`) with particles deferred to shell break end |
| **`replay_shell` queue flag** | Visible live escape always replays cosmetic break shell on flush (index 1 only); resim/snapshot-apply unchanged |
| **Prepare shell cleanup** | Reuse live egg-lay GObj: transition to hatch (index 1) via `force_index` + `SetAnim`; do not reset `anim_frame` to 0. Eject duplicate shells only (`Except(keep_gobj)`). Proc ejects after break anim completes (`anim_frame_before > 0` → `<= 0`), not on first tick |
| **Prepare timing (netplay)** | Victim egg skips throw intro (index 2) in `ftCommonYoshiEggBeginPrepareAnimForNetplay`; hatch transition runs on escape tick via `syNetRbSnapStartYoshiEggLayHatchCosmeticLive`, not deferred flush; flush skips shell replay when hatch already complete (particles only) |
| Hidden cosmetic marker | Keep the replay shell out of effect snapshot/hash/reconcile coupling so it never becomes `captureyoshi.effect_gobj` or causes `eff` drift |
| Yoshi egg root quantize skip | Preserve the fighter-attached egg root DObj while still quantizing free-floating effects; captured-player stick input only wiggles the child shell |
| Effect diag | `resolved_parent=` on apply; `yoshi_egg_lay_hatch_replay ... shell_gobj_id=` lines |
| **Phase 10: intruder eject** | On escape queue only: eject shield / egg-escape intruders on victim; whitelist egg-lay by coupled pointer, `proc_update`, or respawn=8 (not `HasUpdateProc`) |
| **Phase 10b: reconcile regression** | Removed per-tick `EjectIntrudingEffectsOnAllYoshiEggVictims` from reconcile — it ejected respawn=8 shells and blocked escape; flag0 countdown runs when shell present but break incomplete |
| **Phase 10: hatch rewind** | `PrepareYoshiEggLayHatchCosmeticShell` sets break anim frame to ~24 so hatch proc can play visibly after escape-at-index-1 |
| **Phase 10: immediate particles** | `QueueYoshiEggLayHatchCosmeticsLive` fires `ReplayCosmeticYoshiEggExplode` before deferred shell-only queue when shell replay cannot start |
| **Phase 10: snapshot dedup** | Separate per-victim dedup lists for `EGG_LAY` vs `EGG_ESCAPE` saves |
| **Phase 11: hatch GObj proc bind** | `Prepare` ends egg-lay `proc_update` on the GObj and registers `syNetRbSnapYoshiEggLayHatchCosmeticProcUpdate` (pointer-only `ep->proc_update` swap was never scheduled) |
| **Phase 11: immediate escape particles** | `QueueYoshiEggLayHatchCosmeticsLive` always fires `ReplayCosmeticYoshiEggExplode` when hatch shell start succeeds; clears deferred particle pending to avoid double burst |
| **Phase 12: resim defer** | `QueueYoshiEggLayHatchCosmeticsLive` defers shell+particles during `syNetRollbackIsResimulating()`; `FlushDeferred` replays on first live frame |
| **Phase 12: synctest verify Fall replay** | `ReplayYoshiEggLayHatchCosmeticsFromSlot` replays Fall escape hatch during `verify_only` loads (hidden cosmetic, hash-safe); slot apply + flush fire particles when `defer_particles=0` |
| **Phase 13: synctest live-tail ensure** | Record escape tick/pos on every live queue; `TryEnsureLive` replays missing hatch shell within 32 ticks after `yoshi_egg_lay_probe` / `effect_count_transition_probe` skips and after `synctest_restore` |
| **Phase 13: hidden hatch preserve** | `EjectNonHatchYoshiEggLayEffectsForFighterExcept` no longer ejects rollback-hidden hatch cosmetics (only skips them) |
| **Phase 13: live start diag** | `yoshi_egg_lay_hatch_start_live` + `yoshi_egg_lay_hatch_ensure` under `SNAPSHOT_EFFECT_DIAG=1` |

## Verify

Yoshi neutral B egg lay → mash escape under rollback. Both peers should show hatch shell anim + shatter VFX every escape.

- `SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1`: `resolved_parent=<victim gobj>` (not 0); prune/ensure lines during egg window.
- Load at escape tick: `yoshi_egg_lay_hatch_replay` when Fall + escape `vel_y` signature. During resim, expect `deferred=1` followed by `yoshi_egg_lay_hatch_replay_flush ... shell_gobj_id != 0 replay_shell=1` on the next live frame.
- Visible live escape after break completes: `yoshi_egg_lay_hatch_replay_flush ... replay_shell=0` (particles only, no second shell anim after Fall).
- Visible live escape before break completes (resim-hidden break): `replay_shell=1`; particles appear when cosmetic shell break anim ends, not at flush start.
- Captured-player stick input moves the egg child DObj only: the shell wiggles/squishes in place and no longer walks across the stage.
- No eff-only `LOAD_HASH_DRIFT` soft-continues with frozen egg effect hash during status 178.
- `synctest=1`: after escape, expect `yoshi_egg_lay_hatch_start_live` on escape tick and/or `yoshi_egg_lay_hatch_ensure reason=effect_count_transition_probe|yoshi_egg_lay_probe|synctest_restore` when fragile probe skips restore wiped the hidden shell.

Related: [netplay_yoshi_egg_lay_2026-06-01.md](netplay_yoshi_egg_lay_2026-06-01.md), [netplay_yoshi_egg_explode_particles_2026-06-01.md](netplay_yoshi_egg_explode_particles_2026-06-01.md).
