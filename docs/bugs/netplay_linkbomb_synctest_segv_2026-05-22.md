# Netplay Link bomb SYNCTEST SIGSEGV (fighter item respawn)

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

2P soak with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`: Link bomb thrown → explodes at **sim tick ~4410** on both peers:

- `SYNCTEST_SKIP reason=item_throw` for ticks 4406–4409
- First probe after destroy: `item save tick=4410`, emergency capture `tick=4294967295`, then **SIGSEGV**
- `pc=0x0`, `lr=0x0`, `fault_addr=0x0` — null function pointer call
- Ring snapshot at probe tick 4409 had `item_count=2` (bomb in flight + ground item)

Unrelated to Mario/Kirby fireball spawn fixes.

## Root cause

Rollback item apply respawned unmatched blobs via `itManagerMakeItemSetupCommon()`. Fighter items **Ness PK Fire** and **Link bomb** are intentionally `NULL` in `dITManagerProcMakeList[]` — they are created only through fighter-specific makers (`itLinkBombMakeItem`, etc.).

Synctest loads `probe_tick = completed_tick - 1` after live bomb destroy. Ring slot still contains the in-flight bomb blob; apply tried `dITManagerProcMakeList[nITKindLinkBomb](…)` → jump to NULL.

Item blobs also omitted `is_thrown`, so respawned bombs could not restore thrown status procs for hash verify.

## Fix

| Change | Location |
|--------|----------|
| `syNetRbSnapRespawnItemFromBlob()` — route `nITKindLinkBomb` through `itManagerMakeItem` + owner fighter; log/skip other fighter kinds | [`port/net/sys/netrollbacksnapshot.c`](port/net/sys/netrollbacksnapshot.c) |
| `syNetRbSnapReapplyLinkBombStatusAfterBlob()` — restore Hold/Thrown/Dropped/Wait/Fall procs after blob apply | same |
| Capture/apply `item_flags` bit `0x04` for `is_thrown` | same |
| `item_flags` bit `0x08` + nibble `0xF0`: portable Link bomb status (Hold/Thrown/Explode/…) for rollback reapply | same |
| Explode reapply binds procs only (no `ExplodeInitAttackColl`) so blob `multi`/`event_id`/attack coll survive load | same |

## 2026-05-29 follow-up (DK/Link cross-ISA rollback)

Log: single `LOAD_HASH_DRIFT` at tick **1055** during `item_hold` with item+figh+eff mismatch; soft-continued.
Explode status was inferred as Thrown/Wait after blob apply, resetting attack state and leaving phantom hitboxes.

## Soak pass criteria

Link (or Kirby copy Link) bomb throw → explode with synctest enabled:

- No SIGSEGV on first post-throw probe
- Optional: `SYNCTEST_OK` at probe (may still `SYNCTEST_FAIL` on item hash until further round-trip work)
- Log `item respawn unsupported kind=20` only if Ness PK Fire blob respawn attempted (no crash)

## Related

- [`netplay_grab_synctest_throw_segv_2026-05-20.md`](netplay_grab_synctest_throw_segv_2026-05-20.md)
- [`netplay_item_hold_synctest_throw_2026-05-20.md`](netplay_item_hold_synctest_throw_2026-05-20.md)
- [`netrollback_item_snapshot_roundtrip_2026-05-19.md`](netrollback_item_snapshot_roundtrip_2026-05-19.md)
