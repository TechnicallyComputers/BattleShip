# Netplay — Damage hitstun_tics status_vars scrub → DamageFly early exit

**Date:** 2026-07-20  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** `1519592712` (Android client + Linux host), seed `1450034625`

## Symptom

- Pair/seed OK; synctest `9 OK / 0 FAIL`; `LOAD_HASH_DRIFT=0`; `ggpo real_stick=0`.
- Permanent `figh`+`anim` diverge from tick **1113**; `mph`/`rng`/`world` stay matched.
- Android P0: DamageFlyN(52) → DamageFall(57) at **1113**; Linux stays Fly until **1116** (+3).
- Through 1114–1116 TopN `tr_x`/`tr_y` still bit-match while status differs.
- Linux `PEER_SNAPSHOT_DIVERGE` @ load **1126** (`REPLAY_DETERMINISM`): figh+anim only.
- Peer fighter_detail @1126: Android `status_tics=13` vs Linux `10` (exact +3).
- Scan: PHYSICS_FORK@1127 1-ULP `topn_ty` + RESIM_STICK@1127 — cascade, not writers.

## Root cause

`ftCommonDamageAirCommonProcUpdate` exits Fly when:

```c
anim_frame <= 0.0F && ftStatusVarsDamage(fp)->hitstun_tics == 0
```

P0 slot `anim_hash` was frozen at `0x2009DF84` from ~1077–1112 (anim already ended).
The remaining gate is **`hitstun_tics`**.

`FTCommonStatusVars` is a union: `damage` aliases `attackair` / `dead` / `rebirth` / …

`syNetRbSnapScrubInactiveStatusVarsInBlob` zeroed those “inactive” overlays on every ring
save. For DamageHi1…WallDamage that **poisons** captured `hitstun_tics` (and the rest of
`common.damage`) while live still holds the real counter.

Tooling gap: `fhash_light` only folded `is_hitstun` (bool, in full hash). Counter skew stayed
hash-blind until the status change at Fall entry — same class as JumpAerial/KneeBend scrub
([`netplay_jumpaerial_statusvars_scrub_synctest_2026-07-19.md`](netplay_jumpaerial_statusvars_scrub_synctest_2026-07-19.md)).

Asymmetric synctest/rollback load near the Fly end (Android cseed jump @1112) applies the
scrubbed blob on one peer → early Fall; the other keeps live hitstun → +3 `status_tics`.

## Fix

In `port/net/sys/netrollbacksnapshot.c` + `netsync.c`:

1. Early-return scrub for `DamageStart`…`DamageEnd` (DamageHi1…WallDamage).
2. Fold `hitstun_tics` + `is_knockback_over` into `syNetSyncHashFighterStructLight` and mirror
   in `syNetRbSnapHashFighterBlobLight` (`PORT && SSB64_NETMENU`).
3. Second-layer load_drift: `fold_hitstun_tics` / `fold_kb_over`.
4. `fighter_detail`: always log `hitstun_tics=` / `kb_over=` (0 outside Damage*).
5. `scripts/netplay-scan-drift.py`: STATUSVARS_SCRUB recognizes damage fold fields / statuses.

## Acceptance

Android↔Linux Dream Land soak with DamageFly tumble past soft lips + synctest:

- [ ] No PEER figh-only from DamageFly→Fall `status_tics` skew after matched TopN.
- [ ] Matching `hitstun_tics` on `fighter_detail` / `rollback_load` across peers during Fly.
- [ ] If scrub regresses: expect `STATUSVARS_SCRUB` + `fold_hitstun_tics` live≠blob, not silent
      physics cascade at Fall+CLIFF.
