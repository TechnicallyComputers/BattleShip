# NetRollback — cross-peer item hash drift after resim (1737)

**Status:** INVESTIGATION (instrumentation + hash hardening shipped; soak re-validation pending)

## Symptom

After symmetric resim around sim tick **1737**, both peers logged identical local post-resim hashes (`figh`, baseline `item`), but **cross-peer `item` post hashes diverged** (`0x13CFDC36` host vs `0xD7B5B2B4` client) while `fighter_detail` at the same tick was **byte-identical** on both slots. NetSync validation then reported matched `figh`+`inp` for ~71 windows (1770–3840) before sustained `figh` diff from ~3900.

## Root-cause classification (plan B0–B1)

| Step | Check | Result |
|------|-------|--------|
| **B0** | XOR walk order or fold inputs differ at hash-compute tick | **Likely (B0/B1)** — item hash is order-dependent linked-list walk; snapshot restore matches by `gobj_id` but does not restore list order; sim ticks 1737–1747 can relink/spawn/GC items before hash. |
| **B1** | Order matches but hash omits fields | **Possible** — rollback item fold omitted `gobj_id` until 2026-05-18; snapshot blob is richer than XOR fold. |
| **B2** | Snapshot load verify fails at load tick | Not primary in reference soak (`fighter_detail` matched). |
| **B3** | Mid-resim per-tick `item=` trace diverges | Use `SSB64_NETPLAY_RESIM_TICK_TRACE` + `SSB64_NETPLAY_ITEM_HASH_TRACE=1` on both peers. |
| **B4** | True non-determinism | Last resort after B0–B3 clean. |

## Fixes shipped (2026-05-18)

1. **`SSB64_NETPLAY_ITEM_HASH_TRACE=1`** — ordered `(gobj_id, kind, type)` walk + per-step fold at hash-compute time (`syNetSyncLogItemHashWalkTrace`).
2. **Order-independent item hash** — `syNetSyncHashActiveItemsForRollback` sorts active items by `gobj_id` before XOR fold; fold includes `gobj_id`.
3. **`SYNETPEER_PACKET_RESIM_POST` (25)** — cross-peer post-resim digest handshake; compare on local completion when episode keys match (`syNetRollbackTryEmitResimPostHandshake`).

## Re-soak pass criteria

- Host/client `item_hash_walk` logs at target tick **match** (count, order, final hash).
- `RESIM_POST_MATCH` at each resim boundary (or immediate `RESIM_POST_DIVERGE` + re-arm).
- No sustained `figh` diff from validation tick ~3900+ with false-digital fix enabled.
- `FRAME_COMMIT_DIAG compared > 0` when `SSB64_NETPLAY_FRAME_COMMIT_TOKEN=1`.

## Related

- [`netinput_false_digital_prediction_2026-05-18.md`](netinput_false_digital_prediction_2026-05-18.md)
- [`../netplay_rollback_refactor_contracts.md`](../netplay_rollback_refactor_contracts.md) — hash partition map
