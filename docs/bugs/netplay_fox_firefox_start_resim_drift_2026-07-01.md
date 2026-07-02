# Netplay — Fox Firefox SpecialHiStart resim load drift (soak2)

**Date:** 2026-07-01  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Session:** `364064941` (Android client / Linux host, Sector Z, `FORCE_MISMATCH` @520)

## Symptom

Forward sim ticks 515–520: both peers bit-identical with Fox in `SpecialHiStart` (status 230, motion 205) — startup before charge rings / flame travel. Resim after `FORCE_MISMATCH` @520:

- First load @518 verify passes (`live figh == slot figh`).
- Immediately after, `LOAD_SLOT_LIVE_DRIFT pre-resim` fires and reloads @517.
- Resim replay shows Fox at status 232 (Hold) or 234 (Travel) on tick numbers where the ring still has 230.
- Android host: `PEER_SNAPSHOT_DIVERGE load_tick=518` (`figh`/`map`/`rng`/`anim` disagree; `world`/`item` match).
- Guest: STABLE (soft recovery). Synctest scan reads PASS (`load_hash_drift=0`) — does not parse `PEER_SNAPSHOT_DIVERGE`.

## Root cause

Fox Firefox gate **sanitize/catch-up ran during the entire defer scope**, including `SpecialHiStart`. While status 230 is live, `status_vars.fox.specialhi` is the wrong union overlay — clamping `launch_delay` / `anim_frames` there stomps Start bytes, changes `figh` without a status transition, and makes `syNetRollbackTryDeeperLoadBeforeResim` see slot/live drift right after an otherwise clean load verify. Forward-resim presentation refresh also ran Hold/Travel catch-up on Start ticks, advancing the state machine before sealed forward sim could replay the startup anim-end.

Distinct from round 4 of `netplay_fox_appear_firefox_charge_soak2` (effect-count fragile walkback); this session had no `RESIM_ANCHOR_FRAGILE_WALKBACK`.

## Fix

| Change | Purpose |
|--------|---------|
| `syNetplayFoxFighterInFirefoxStartScope` | Identify `SpecialHiStart` / `SpecialAirHiStart` |
| Scope-aware `syNetplayFoxSanitizeFirefoxStatusVars` | Only clamp `launch_delay` in Hold, `anim_frames`/`decelerate_wait`/`angle` in Travel; no-op on Start |
| `syNetplayFoxCatchUpAllAfterLoadVerify` | Skip Start — Hold/Travel catch-up only |
| `syNetRbSnapApplyFighterNetplayPost` | Same Start gate on sanitize/catch-up during apply |
| `syNetRbSnapRefreshFoxResimPresentationFromSlot` | Re-pin presentation on Start; defer catch-up to Hold/Travel |

## Verify

Re-run soak2 cross-ISA through Fox Firefox startup + `FORCE_MISMATCH` resim @518–522. Expect:

- No `LOAD_SLOT_LIVE_DRIFT pre-resim` immediately after a matching load @518 when Fox is still status 230
- Duplicate `fighter_slot_hash tick=N` lines (forward vs resim) agree on Fox `status`/`motion` for the same tick
- No `PEER_SNAPSHOT_DIVERGE` at the Start→Hold boundary
- Hold/Travel stuck-state recovery (negative `launch_delay` / `anim_frames`) still works once Fox reaches those statuses
