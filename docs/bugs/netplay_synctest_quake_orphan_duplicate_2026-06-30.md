# Netplay synctest quake orphan duplicate — 2026-06-30

**Scope:** PORT netmenu rollback synctest verify + emergency restore  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

**3404 follow-up (same soak class, post-509 fix):** verify `eff_fold_diag count=2` with **two identical** `respawn=1` / `quake_pri=2` quakes at the same position and **same ring gobj_id** (`1011`) while slot `effect_count=1`. `slot_effect_enforce` ejected one orphan but blob-matching id-collision duplicate survived enforce + retrack.

## Symptom

Cross-ISA synctest soak with **no inject** (`SYNCTEST=1`, `resim=0`): peers stay aligned through forward sim, but the first Link-bomb / grab window probed at tick **509** hits `SYNCTEST_FAIL` with **eff-only** `LOAD_HASH_DRIFT` (`slot=0x9B837CA4`, `live=0xC4E1223D`). `eff_fold_diag tag=verify` shows `count=2` vs capture `count=1`: two live quakes at the same position — one with `quake_pri=2` (matches slot blob), one stale shell with `quake_pri=0` that fails `MatchesBlob`. Match continues ~570 ticks, then SIGSEGV near soak end (effect pool / stale XF class).

With `SYNCTEST=0`, bomb pull is fine — synctest load-verify is the amplifier.

**3595 follow-up (same soak, post-509 fix):** first probe after dual-quake window (live `count=2` with `respawn=0`/pri=4 shell + slot bomb quake, slot `effect_count=1`) at tick **3595** hits `SYNCTEST_FAIL` with verify **empty** eff fold (`count=0`, `live=0x811C9DC5`) vs slot `count=1` (`0x957F6ADC`). `quake_unmatched_prune` ran but `EjectAllNonCanonicalEffectsForVerify` ejected the canonical quake before checking `canonical_gobj_ptrs`.

## Root cause

Existing duplicate/excess quake pruners only eject live quakes that **match** a slot blob identity. Stale bomb-quake shells with wrong priority survive, `EnsureQuakeEffectsFromSlot` respawns/adopts a second quake, and verify hashes both. Post-fail `quake_sanitize` only ejects dead/null/low-anim shells, not priority-mismatched orphans. `EnforceSlotAuthoritativeEffectSet` could also skip eject when a stale quake shared the reconciled `gobj_id` with the canonical quake.

## Fix (509 duplicate + 3595 empty-verify follow-up)

| Helper / site | Change |
|---------------|--------|
| `syNetRbSnapPruneUnmatchedLiveQuakeEffects` | Eject live quakes that do not match any slot quake blob; chain duplicate + excess pruners |
| `syNetRbSnapPruneNonRespawnableQuakeShells` | Verify-only pre-Ensure: eject `IsQuake` shells with `respawn_kind != QUake` (dual-quake collapse @3595) |
| `syNetRbSnapEnsureQuakeEffectsFromSlot` | Resim load: unmatched pre-Ensure; verify-only: shell pre-Ensure only |
| `syNetRbSnapshotFinalizeVerifyEffectStateInternal` | Verify-only: unmatched prune **after** Ensure (509 pri=0 orphans post blob apply) |
| `syNetRbSnapEjectAllNonCanonicalEffectsForVerify` | Check `canonical_gobj_ptrs` **before** quake blob-match eject (3595 had `count=0` when canonical quake failed post-freeze `MatchesBlob`) |
| `syNetRbSnapEnforceSlotAuthoritativeEffectSet` | Eject reconciled-id quake orphans with wrong priority |
| Emergency restore + sanitize | Shell prune only after enforce (not full unmatched) |
| `syNetRbSnapEnforceSlotAuthoritativeEffectSet` eject pass | Eject **any** reconciled-id effect whose ptr is not canonical (not only pri-mismatched quakes) |
| `syNetRbSnapResolveLiveEffectGobjForBlobApply` | Prefer already-tracked canonical quake ptr before `gcFindGObjByID` on id collisions |
| `syNetRbSnapRetrackCanonicalSlotEffectsBeforeVerifyEject` | Drop verify TryRespawn (enforce already respawned); adopt via FindLiveQuake only |
| `syNetRbSnapPruneSurplusBlobMatchedQuakesForVerify` | Pre-final-eject: one live quake per slot blob (prefer canonical ptr) |
| `syNetRbSnapEjectAllNonCanonicalEffectsForVerify` | Eject all non-canonical quakes when slot owns a quake blob |

Built clean (`build-netmenu` Debug).

### 3404 identical-quake follow-up (2026-06-30)

| Helper / site | Change |
|---------------|--------|
| (see rows above) | Collapse N blob-matching quakes sharing reconciled gobj_id → 1 before verify hash |

## Verification

Re-soak cross-ISA with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`, no inject, DK grab + Link bomb window:

- Expect no `SYNCTEST_FAIL` / eff-only drift at tick ~509 or ~3595
- Expect `eff_fold_diag tag=verify count=1` when capture had one quake
- Expect no late SIGSEGV through bomb+grab windows

Audit hooks:

- `count=live>slot` + mismatched `quake_pri` = stale orphan before ensure (509 class)
- `count=live<slot` + `slot_effect_enforce canonical=1` + verify `count=0` = canonical quake ejected in verify eject pass (3595 class)
