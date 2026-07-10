# Netplay Mario/Luigi fireball synctest wpn drift (@2429)

**Date:** 2026-07-10  
**Scope:** `PORT && SSB64_NETMENU`  
**Soak:** session `171256288` (Mario vs Luigi, Dream Land), `SYNCTEST_FAIL` first @2429

## Symptom

```
LOAD_HASH_DRIFT tick=2429 ... wpn=0x811C9DC5/0xDD68C1D1
SYNCTEST_FAIL tick=2429
```

All other partitions match; per-fighter blob hashes OK.

## Game state

- P0 Luigi: status **223** (`SpecialN`), motion 198 — fireball throw windup (anim frame ~15)
- P1 Mario: idle Wait (status 10)
- Ring capture @2429: `weapon_count=0` (fireball anim flag fires next tick @2430)
- Forward sim @2430: `weapon_count=1` (`fireball_spawn path=anim`)
- Load verify @2429: tick-2430 fireball survives deferred eject via `FireballThrowPreserve`; slot empty → `wpn` drift
- `prepare_verify` took `residual_shield` presentation branch, so an else-if-chained fireball cull would never run

## Root cause

Fireball is minted on the **tick after** capture (`anim flag0` at frame ~16). Synctest loads tick 2429 while forward sim has already advanced to 2430 and spawned the ball. Deferred weapon eject skips fireballs owned by fighters still in `SpecialN` throw status, so the orphan from +1 tick remains live while the slot hash is empty.

Same class as Samus `SpecialNEnd` charge release (`docs/bugs/netplay_samus_specialnend_charge_synctest_2026-07-07.md`).

## Fix

| File | Change |
|------|--------|
| `netrollbacksnapshot.c` | Slot-aware `FireballThrowPreserve`: with authoritative slot, unmatched fireballs always eject (empty orphans + non-empty extras) |
| `netrollbacksnapshot.c` | `prepare_verify` fireball cull is an independent `if` (not else-if after residual shield / Samus presentation) |
| `netrollbacksnapshot.c` | Empty-slot cull all owned; non-empty cull unmatched owned vs slot fireball blobs |
| `netrollbacksnapshot.c` | `TryRepairWeaponHashForVerify` empty + non-empty fireball cull paths |
| `netrollbacksnapshot.c` | Gate fireball / PK Fire / Thunder Jolt spawn during `DeferWeaponSimDuringLoadVerify` |
| `netrollbacksnapshot.c` | `ApplyFighterNetplayPost` fireball catch-up (PK Fire parity; no-op under defer-verify) |

## Verify

Re-soak Mario vs Luigi past tick 2429. Expect `SYNCTEST_OK` through fireball throw windows; no `LOAD_HASH_DRIFT diverged=wpn` on pre-spawn or close-range double windows.
