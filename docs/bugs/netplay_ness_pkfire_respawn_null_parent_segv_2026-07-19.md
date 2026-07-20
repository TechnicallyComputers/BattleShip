# Netplay — Ness PK Fire respawn NULL parent SIGSEGV (soak 383687403)

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Session:** soak1 `383687403` seed `3154968275` (Android client ↔ Linux host)

## Symptom

Deterministic dual-peer crash during synctest / emergency restore around Ness PK Fire pillar birth:

```text
SSB64: !!!! CRASH SIGSEGV fault_addr=0xe0
… itManagerMakeItem+0x968
… gFTNessFileSpecial1+0x0   ← misleading (item_desc->p_file / unwinder noise)
```

| Signal | Detail |
|--------|--------|
| Tick | ~2794 — P1 Ness `SpecialN` (226/201); PK Fire weapon kind=13 → item kind=20 |
| Sync | No `FRAME_COMMIT` / `LOAD_HASH_DRIFT` before crash (`scan-drift` PASS) |
| Path | Forward spawn item → synctest load@2793 (item=0) ejects pillar → verify → restore emergency live (item=1) → respawn pillar → SIGSEGV |
| Registers | `x3=0`; `fault_addr=0xe0` = `offsetof(GObj, user_data)` |

## Root cause

`syNetRbSnapRespawnNessPKFireFromBlob()` mirrored vanilla flags:

```c
itManagerMakeItem(NULL, &dITNessPKFireItemDesc, pos, vel,
                  ITEM_FLAG_COLLPROJECT | ITEM_FLAG_PARENT_WEAPON);
```

Vanilla `itNessPKFireMakeItem` passes the **live weapon** as `parent_gobj`. Rollback respawn has no weapon parent. With `COLLPROJECT` + `PARENT_WEAPON`, `itManagerMakeItem` does:

```c
wpGetStruct(parent_gobj)->coll_data.p_translate  /* parent_gobj == NULL */
```

→ load from `NULL + 0xe0` (`GObj.user_data`).

Backtrace's `gFTNessFileSpecial1` is `dITNessPKFireItemDesc.p_file`, not the caller.

## Fix

| Change | Location |
|--------|----------|
| Respawn with `ITEM_FLAG_PARENT_DEFAULT` (blob pose authoritative; no parent project) | [`port/net/sys/netrollbacksnapshot.c`](../../port/net/sys/netrollbacksnapshot.c) |
| Netmenu guard: NULL parent + non-DEFAULT parent mask → coerce to DEFAULT before project | [`decomp/src/it/itmanager.c`](../../decomp/src/it/itmanager.c) (`PORT && SSB64_NETMENU`) |

## Re-soak

1. Ness PK Fire hit → pillar spawn under `ROLLBACK_SYNCTEST=1`.
2. No `fault_addr=0xe0` in `itManagerMakeItem` on either peer.
3. No new item-only `LOAD_HASH_DRIFT` around the pillar tick.

## Related

- [`netplay_ness_pkfire_item_respawn_2026-07-09.md`](netplay_ness_pkfire_item_respawn_2026-07-09.md) — added PK Fire respawn path (this bug was latent in that maker)
- [`netplay_kirby_vulcanjab_efstruct_null_2026-07-01.md`](netplay_kirby_vulcanjab_efstruct_null_2026-07-01.md) — same `fault_addr=0xe0` signature, different NULL struct
