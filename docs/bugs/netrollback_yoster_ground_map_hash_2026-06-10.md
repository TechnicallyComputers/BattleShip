# Yoster ground map hash cross-ISA baseline mismatch (2026-06-10)

## Symptom

Soak2 (Yoshi's Island, inject @240): resim baseline gate never opens. `RESIM_BASELINE_BISECT` shows `map=1` at every load tick 239→113 while figh/world/rng/anim agree. Forward `sim_state_tick mph` matches cross-peer; post-load `hash_map` diverges (Linux vs Android).

## Root cause

`syNetRbSnapFoldGroundPayloadHashForRollback()` had no `nGRKindYoster` case. Yoster ground fold used raw byte FNV over the full payload, including:

- `map_head` stored as `uintptr_t` (pointer bits differ Linux vs AArch64)
- Cloud `translate` / `altitude` / `pressure` as raw IEEE f32 without hash-grid quantization

Local load verify passed on each peer; cross-peer baseline compared slot `hash_map` saved at capture time.

## Fix

Phase 40 in `port/net/sys/netrollbacksnapshot.c`:

- In hash fold scratch only: zero `map_head` before fold (restore still uses captured pointer)
- Quantize Yoster cloud F32 fields via `syNetplayQuantizeF32ForRollbackHash` (full + legacy blob layouts)

Diag: `map_hash_save` compact log on every slot save when `SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1`; baseline SEND/RECV and BISECT lines include explicit `map=` digests.

## Verify

Re-run soak2 Linux↔Android. Expect `resim baseline digest matched` @239 and resim complete past inject.
