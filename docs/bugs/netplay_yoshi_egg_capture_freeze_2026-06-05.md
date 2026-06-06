# Yoshi neutral-B capture animation freeze (netplay)

**Date:** 2026-06-05  
**Scope:** `port/net/sys/netrollbacksnapshot.c`  
**Status:** FIX SHIPPED (phase 6) — soak pending (capture-window hardening + owner-keyed egg shell reconcile)

## Symptoms

Cross-platform netplay (Android host / Linux guest): Yoshi neutral B captures an opponent, but the swallow animation never advances. Yoshi stays in `SpecialNCatch` (178) with a fixed `anim_hash`; victim may sit in `CaptureYoshi` (230) or pop to `Wait` without ever entering `YoshiEgg` (231). Logs show `yoshi_egg_lay_prune reason=duplicate` every snapshot save with the same `effect_gobj_id`, and repeated `gobj_alloc id=1011 link=6` during the capture window.

Kirby-copy variant: victim's **shield bubble GObj id** (e.g. 1011) collides with egg-lay reconcile — shield gets cleared/adopted as `captureyoshi.effect_gobj`, catcher animation freezes, victim never reaches `YoshiEgg`.

Follow-up capture after the freeze fix: victim reaches `YoshiEgg`, but a tiny egg-lay shell appears in front of the real shell and moves with victim stick wiggle. Logs show `effect_count` jumping from 0 to 3/4, then every frame creates a fresh `gobj_alloc id=1011` and alternates `yoshi_egg_lay_prune reason=duplicate` with `adopt_solo`.

Both peers agree (`state_diverge=0`) — deterministic forward-sim reconcile bug, not input divergence.

## Root cause

1. **Egg-lay reconcile during capture-only window** — `syNetRbSnapReconcileYoshiEggLayEffectsLive()` ran full ensure/prune while fighters were in `CaptureYoshi` / Yoshi `SpecialN/NCatch` but not yet `YoshiEgg`. Vanilla never parents an egg-lay shell until `YoshiEgg`; stray respawns + duplicate prune by stale pointer/id churn corrupted the effect GObj pool every frame.
2. **Duplicate prune without id-rebind** — same GObj id reused at a new pointer → canonical resolved by id ≠ iterated pointer → eject loop (guard-shield had already fixed this pattern).
3. **`catch_gobj` scrub on rollback apply** — `syNetRbSnapScrubFighterGrabCouplingState()` cleared Yoshi's `catch_gobj` because `is_catch_or_capture` stays FALSE during SpecialNCatch (valid per `ftmain` coupling rules), breaking motion-event propagation to the victim on resim loads.
4. **Shield vs egg-lay GObj id collision** — `guard_effect_gobj_id` and `captureyoshi_effect_gobj_id` are separate snapshot fields but share the fighter status-vars union at runtime. Egg-lay prune used `syNetRbSnapClearFighterEffectPointerIfMatch`, which cleared **both** guard and captureyoshi pointers. `syNetRbSnapLiveEffectIsYoshiEggLay` lacked the shield path's `HasUpdateProc` check, so stale pool slots with a matching `proc_update` field were mis-identified as egg-lay and adopted via `adopt_solo`.
5. **Post-capture shell identity still used `GObj::id`** — once `YoshiEgg` was live, sanitizer/rebind validated `captureyoshi.effect_gobj` by `gcFindGObjByID(1011) == pointer`. Egg-lay effect ids are kind ids, not instance ids, so any extra shell with id 1011 could make the real pointer look stale. Rollback then nulled the pointer, vanilla `ftCommonYoshiEggMakeEffect` created another shell next frame, and prune bounced between `duplicate` and `adopt_solo`.

## Fix

1. **Capture-window early-out** — when any fighter is in capture/attack scope but none in `YoshiEgg`, sanitize + eject all stray egg-lay effects only; skip ensure/respawn; rebind grab coupling. Unified live+slot check via `syNetRbSnapYoshiEggLayReconcileCaptureWindowActive`.
2. **Prune id-rebind** — mirror `syNetRbSnapShieldEffectMatchesKeep`: adopt live effect when id matches stale coupled pointer; eject only genuine duplicates.
3. **CaptureYoshi shell guard** — prune ejects egg-lay effects parented to fighters still in `CaptureYoshi`; ensure never respawns during that status.
4. **Scrub** — preserve Yoshi `catch_gobj` while in `syNetRbSnapFighterInYoshiEggLayAttackScope`.
5. **Core order** — sanitize captureyoshi `effect_gobj` before prune on the full reconcile path.
6. **Shield cross-check (phase 2)** — `syNetRbSnapLiveEffectIsYoshiEggLay` requires `HasUpdateProc` and rejects shields; egg prune/ensure skip ids claimed by `syNetRbSnapSlotGuardClaimsEffectId` / live guard coupling; egg paths use `syNetRbSnapClearCaptureYoshiEffectPointerIfMatch` only (never clear guard); `captureyoshi_effect_gobj_id` saved/restored only in `YoshiEgg` status; adopt/keep restricted to `YoshiEgg` victims; guard effect sanitize added; during capture window, egg reconcile runs before guard reconcile in `syNetRollbackAfterBattleUpdate`.
7. **Kirby-copy + live churn (phase 3)** — capture-window attack scope keys off status IDs (Yoshi + Kirby CopyYoshi SpecialN/NCatch/Release), not `fkind == Yoshi`; egg prune id-rebind matches against both stale coupled pointer and resolved canonical (fixes live `duplicate`/`adopt_solo` loop on same id); `adopt_solo` skips guard-claimed ids; victim egg shell requires `syNetRbSnapYoshiEggLayVictimOwnsEffect` (effect parent must be slot owner fighter); guard rebind requires strict `ep->fighter_gobj == ResolveFighterGobjByPlayer(player)`; live guard duplicate rebind keeps same-id instead of eject when `slot == NULL`.
8. **Capture swallow hardening (phase 5)** — `syNetRbSnapCaptureYoshiSwallowInProgressLive/Slot` forces capture-window early-out whenever victim `CaptureYoshi` + attacker NCatch scope are both live (cannot be blocked by spurious `YoshiEgg`); orphan prune rejects egg-lay effects parented to attackers in NCatch scope; solo-path duplicate adds `resolved==gobj` and same-id keep; `adopt_solo` requires victim `YoshiEgg` + `VictimOwnsEffect`; `EffectIdClaimedByGuard` consults current ring slot on live path (stale shield blob ids); `RebindFighterEffectGobjs` only couples egg shell at `YoshiEgg`; post-core slot rebind skipped during capture window.
9. **Owner-keyed shell reconcile (phase 6)** — `captureyoshi.effect_gobj` sanitize/rebind now requires a live egg-lay effect pointer owned by the victim fighter instead of resolving by shared id 1011; ensure adopts an existing owner shell before blob/synthetic respawn; synthetic restore is keyed by owner only; prune keeps one owner-attached shell and ejects extras with `reason=owner_duplicate`.

## Test plan

1. Cross-ISA Yoshi vs CF/Kirby: neutral B capture during forward sim — swallow anim completes, victim enters egg, no per-frame `yoshi_egg_lay_prune duplicate` spam.
2. Victim who shielded earlier in the match: capture does not steal/rebind shield GObj id; shield bubble unaffected after capture ends.
3. FC recovery load mid-capture: Yoshi `catch_gobj` survives apply; victim reaches `YoshiEgg` after resim.
4. Victim wiggles inside `YoshiEgg`: one visible shell moves/animates; no tiny duplicate shell; `effect_count` returns to one owner egg-lay shell instead of climbing to 4.
5. Existing egg-lay hatch / rollback soak from `netplay_yoshi_egg_lay_2026-06-01.md` still passes.
