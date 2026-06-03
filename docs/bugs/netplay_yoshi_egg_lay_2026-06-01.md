# Yoshi neutral-B egg lay rollback desync (netplay)

**Date:** 2026-06-01  
**Scope:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netsync.c`  
**Status:** FIX SHIPPED — soak pending (Yoshi neutral B / Kirby in egg on cross-ISA rollback)

## Symptoms

Cross-ISA soak: Yoshi neutral B puts Kirby in egg (`CaptureYoshi` / `YoshiEgg`), but victim sometimes pops out almost immediately on one peer while the other still shows the shell. Hidden figh drift can accumulate until a later `FRAME_COMMIT_STATE_DIVERGE`.

Distinct from existing **`yoshi_egg`** defer (SpecialHi throwable egg weapon / charge).

## Root cause

1. **No synctest defer** for statuses `177` (`CaptureYoshi`) / `178` (`YoshiEgg`) — only SpecialHi throwable egg was deferred.
2. **`fp->breakout_wait` / mash dirs not snapshotted** — egg physics uses top-level trapped breakout vars plus `status_vars.common.captureyoshi.*`; rollback load left stale mash counters → instant hatch on resim.
3. **`captureyoshi_effect_gobj_id` captured only on status 177** — egg shell effect lost on status 178 load; escape path keyed off effect anim index/frame misfired.
4. **Inactive `captureyoshi` union bytes** not scrubbed outside egg statuses — polluted diagnostics / hash noise.

## Fix

1. **`yoshi_egg_lay` / `yoshi_egg_lay_probe` synctest defer** while any fighter is in CaptureYoshi or YoshiEgg.
2. **Fighter blob:** explicit `breakout_wait`, `breakout_lr`, `breakout_ud`; capture effect id on both 177 and 178; scrub inactive `captureyoshi` overlay on save.
3. **Effect respawn:** `SYNETRB_EFFECT_RESPAWN_YOSHI_EGG_LAY` whitelist + ensure/prune on rollback apply (mirror guard shield pattern).
4. **`fhash_light` fold** for egg-lay breakout / captureyoshi scalars during 177/178.
5. **Sector Z Arwing deck:** extend jump-arc defer (knee/jump/land/fall + attack-air band) while Arwing deck collision is live, even when briefly airborne off line 1.

## Test plan

1. Cross-ISA Yoshi vs Kirby: neutral B egg lay during rollback — Kirby stays in egg for normal duration on both peers; log shows `SYNCTEST_SKIP reason=yoshi_egg_lay`.
2. FC recovery load mid-egg: no instant pop; `captureyoshi_effect_gobj` rebound; breakout timers match.
3. Sector Z: jump on Arwing deck during near pass — `SYNCTEST_SKIP reason=sector_arwing_deck` while airborne in jump arc; no figh-only diverge ~3240.
