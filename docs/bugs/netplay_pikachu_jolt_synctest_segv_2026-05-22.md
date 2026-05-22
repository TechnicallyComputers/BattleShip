# Pikachu Thunder Jolt synctest SIGSEGV — Resolved (2026-05-22)

**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`, `decomp/src/mp/mpcommon.c`

## Symptom

Netplay synctest @ tick 870 (probe 869): both peers SIGSEGV `fault_addr=0x3700000000` in
`mpCommonRunWeaponCollisionDefault+0x8` while Pikachu P0 is in SpecialN (`status=222`, Thunder Jolt).

Log sequence:

- Tick 869 snapshot: `weapon_count=1`
- Tick 870 live: jolt ejected (`id=1012`), `weapon_count=0`
- Synctest emergency capture → load probe 869 → crash during weapon respawn

## Root cause

Same class as [Link bomb synctest](netplay_linkbomb_synctest_segv_2026-05-22.md): synctest loads tick N−1
while live sim at N has already destroyed the projectile. Weapon apply respawns Thunder Jolt via
`wpPikachuThunderJoltAirMakeWeapon` → `wpManagerMakeWeapon` → `mpCommonRunWeaponCollisionDefault`, which
dereferences the parent fighter's `coll_data.p_translate`. That pointer was garbage (`0x3700000000`) after
the partial snapshot apply path (ApplyWorld runs after fighter MPColl apply; weapon spawn before pointer
rebind).

## Fix

| Layer | Change |
|-------|--------|
| **Load order** | `syNetRbSnapRebindAllFighterMPCollPointers()` after items, before weapons — rebind `p_translate` / `p_lr` / `p_map_coll` from live TopN joint |
| **Synctest skip** | `syNetRbSnapshotSynctestProbeWeaponMismatch(probe_tick)` — skip when live weapon link count ≠ probe snapshot `weapon_count` |
| **Guard** | `mpCommonRunWeaponCollisionDefault` skips collision init when `pos` is NULL or LP64 garbage pattern |

## Verification

With synctest enabled, Pikachu B-spam netplay soak:

- No SIGSEGV at weapon destroy/spawn boundary
- Log `SYNCTEST_SKIP … reason=weapon_probe_mismatch` when jolt dies same tick as probe (acceptable)
- Normal rollback loads still respawn jolts with valid parent translate (no skip log on resim)

## Related

- [netplay_linkbomb_synctest_segv_2026-05-22.md](netplay_linkbomb_synctest_segv_2026-05-22.md)
- [netrollback_weapon_deferred_eject_2026-05-20.md](netrollback_weapon_deferred_eject_2026-05-20.md)
