# Netplay: effect-pool free-list corruption → quake make-effect SIGSEGV — 2026-06-28

**Status:** GUARD + DIAGNOSTIC SHIPPED (`PORT && SSB64_NETMENU`, soak pending)
**Scope:** PORT netmenu rollback effect reconcile / `EFStruct` pool free path

## Symptom

`soak1` cross-ISA run (android host / linux guest) **crashed deterministically on
both peers at the same forward-sim tick** while creating a camera quake:

```
gobj_alloc id=1011 link=6 ... frame=1974        (android)  /  frame=1997 (linux)
figatree-bind fkind=2 walked=25 bound=17 ...
!!!! CRASH SIGSEGV
```

- Linux: `efManagerQuakeMakeEffect+0x31`, `lr=0x0`, `fault_addr=0x0`
- Android: `fault_addr=0x82b72000000000` (non-canonical garbage pointer)
- No resim/rollback active at the crash (`phase_lock_commit gen=1973 → next_sim=1974`).
  Earlier in the run: `NetRbSnapshot: quake_sanitize ejected=1 tick=1555`.

Identical crash on arm64 (Android) and x86_64 (Linux) ⇒ deterministic game-state
logic, **not** memory non-determinism and **not** a peer desync. (The same soak's
`sync-report` `MATCH: UNSTABLE` / tick-433 "mismatch" was a separate
`netplay-trim-logs.py` reconciliation artifact — see below — and is unrelated.)

## Root cause

The `EFStruct` pool (`EFFECT_ALLOC_NUM = 38`) is a singly-linked free list.
Allocation pops the head and dereferences `->next`
(`efManagerGetNextStructAlloc`); freeing pushes onto the head with **no
validation** (`efManagerSetPrevStructAlloc`). The Android `fault_addr` is exactly
`sEFManagerStructsAllocFree = ep->next` reading `->next` off a garbage `ep` that a
prior free had pushed as the list head.

The bad pointer originates in the netplay rollback effect-reconcile path
(`port/net/sys/netrollbacksnapshot.c`). `syNetRbSnapEjectGObj` did
`gcEjectGObj(gobj); efManagerSetPrevStructAlloc(efGetStruct(gobj));` **without
clearing `gobj->user_data.p`** and without validating the struct. The quake
adopt/respawn/stamp machinery (Phases 38–47 of
[netplay_quake_effect_verify_drift](netplay_quake_effect_verify_drift_2026-06-07.md))
sets and copies `user_data.p` while reconciling live quakes against slot blobs, so
two GObjs can end up sharing one `EFStruct`, or a struct can be freed again via a
later reconcile pass. Either case pushes a stale/aliased (eventually garbage)
`EFStruct` onto the free list; the next forward `efManagerQuakeMakeEffect`
allocation pops it and faults. Offline never runs this path.

(The `gcEjectGObj` `obj_kind == 0xFE` sentinel only guards a *second eject of the
same GObj*; it does not guard a duplicate *struct free* arriving via a different
GObj, which is the aliasing vector here. The item-eject sibling
`syNetRbSnapEjectItemGObjForRollback` already clears `user_data.p` after the
free — the effect path was the lone one that didn't.)

## Fix (guard + diagnostic)

| Area | Change |
|------|--------|
| `efManagerInitEffects` | Capture pool base into `sEFManagerNetPoolBase` (`PORT && SSB64_NETMENU` only) |
| `efManagerNetSafeFreeStruct` (new, netmenu-only) | Range-check `ep` against the pool array (bounds + element alignment) and walk the free list (bounded by pool size) to reject a double-free; **log the offending eject site** via `port_log("SSB64: ef_pool_free_reject reason=... site=... gobj=... id=... ep=...")` and skip the push instead of corrupting the chain. Otherwise calls `efManagerSetPrevStructAlloc`. |
| `syNetRbSnapEjectGObj` / Yoshi-egg-lay hatch cosmetic proc | Free through `efManagerNetSafeFreeStruct(ep, gobj, site)` and clear `gobj->user_data.p = NULL` (mirrors the item-eject path) |

The decomp allocator (`efManagerSetPrevStructAlloc` / `efManagerGetNextStructAlloc`)
is **untouched** — offline builds (`SSB64_NETMENU` undefined) never compile the
guard and free exactly as before, so ROM-matching is unaffected. This converts a
silent free-list corruption + delayed crash into a logged, skipped free (the
struct leaks, the game survives) that **names the eject site** for the next soak.

Built clean: `build-netmenu` (`SSB64_NETMENU=ON`, Debug) — `ssb64_game` + linked
`BattleShip` executable.

## Verify

1. Re-run the `soak1` cross-ISA pair. Expect: no SIGSEGV in `efManagerQuakeMakeEffect`.
2. If `SSB64: ef_pool_free_reject reason=double_free|out_of_range site=<...>` appears,
   that names the reconcile/eject site producing the aliased/stale struct — fix the
   producer (stop two GObjs sharing one `EFStruct`, or eject without clearing
   `user_data.p`) as the follow-up root-cause pass.
3. Camera quake during heavy landing still plays; no new `LOAD_HASH_DRIFT`.

## Audit hook

A deterministic, cross-ISA-identical crash in an effect **allocator** (popping a
garbage free-list head) during forward sim, after the rollback effect-reconcile has
run = a netplay eject pushing a stale/aliased/garbage `EFStruct`. Any rollback
eject that returns a struct to a manager pool must clear the GObj's `user_data.p`
and must not free the same struct twice.
