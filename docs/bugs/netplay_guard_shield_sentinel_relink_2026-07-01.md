# Netplay — dual-shield-spam WATCHDOG HANG / SIGABRT from sentinel-gobj re-unlink

**Date:** 2026-07-01
**Status:** Fix implemented (`port/net/sys/netrollbacksnapshot.c`), soak pending
**Area:** Rollback reconcile GObj lifecycle (`syNetRbSnapUnlinkSentinelGObjFromLists`)

## Symptom

Soak2 session (max_sim_tick=1508+): both peers freeze deterministically at the same tick —
not a desync (no `SYNCTEST_FAIL`/`PEER_SNAPSHOT_DIVERGE`). `soak2-android.log` cuts off mid-frame;
`soak2-linux.log` keeps logging and shows the main thread genuinely wedged:

```
SSB64: gobj_alloc gobj=... id=1011 link=6 caller=... frame=1509
SSB64 NetRbSnapshot: guard_shield_heal_skip tick=1508 player=1 status=152 ...
SSB64: WATCHDOG HANG since_frame=3001ms ... frame=3406 yield_count=165339
SSB64: WATCHDOG HANG since_frame=5051ms ... frame=3406 yield_count=165339   (frame/yield never advance)
... (7 total, up to since_frame=13253ms)
efManagerShieldProcUpdate+0x0
gcRunGObjProcess+0x83
gcRunAll+0xd5
SSB64: !!!! CRASH SIGABRT fault_addr=0x3e800207186
```

Repro: both fighters spamming Guard (shield) rapidly. The hang lands the instant player 1
raises Guard again (`status=152`) and a **fresh** shield-bubble `GObj` is allocated at the
volatile pool id `1011` — the same id churns dozens of times earlier in the session
(`effect save skip ... gobj_id=1011 reason=gobj_id_duplicate` bursts). A separate rate-limited
`RelocPointerTable: invalid/stale token ...` (`decomp/src/lb/lbcommon.c:894`, figatree joint
bind) was also observed in the same session on the console/stderr spdlog sink (not captured in
the `SSB64:`-prefixed port_log file) — consistent with the same memory having been corrupted/
reused out from under a stale reference.

This soak was run to verify the same-day fix in
[netplay_shield_effect_efstruct_null](netplay_shield_effect_efstruct_null_2026-07-01.md) (NULL
`EFStruct*` guard in `efManagerShieldProcUpdate`). That fix works — no more `SIGSEGV
fault_addr=0xe0` — but the same dual-shield-spam stress now surfaces a hang instead, bottoming
out in the same function.

## Root cause

`gcEjectGObj` (`decomp/src/sys/objman.c`) sets `obj_kind = GOBJ_PORT_EJECTED_SENTINEL` (`0xFE`)
in **exactly one place**, immediately *after* it has already run
`gcRemoveGObjFromDLLinkedList`/`gcRemoveGObjFromLinkedList` and `gcSetGObjPrevAlloc` for that
`GObj`. Critically, `gcSetGObjPrevAlloc` **repurposes `gobj->link_next` as the `sGCCommonHead`
free-list chain pointer**:

```c
void gcSetGObjPrevAlloc(GObj *gobj)
{
	gobj->link_next = sGCCommonHead;
	sGCCommonHead = gobj;
	sGCCommonsActiveNum--;
}
```

`gobj->link_prev` is left untouched — a stale dangling pointer to whatever was this `GObj`'s
former live neighbor. Neither field is ever cleared.

`syNetRbSnapUnlinkSentinelGObjFromLists` (added for the earlier
[netplay_guard_shield_zombie_eject_uaf](netplay_guard_shield_zombie_eject_uaf_2026-06-05.md) fix)
runs whenever rollback reconcile sees `obj_kind == 0xFE` on a shield-bubble `GObj`, and
unconditionally called `gcRemoveGObjFromLinkedList`/`gcRemoveGObjFromDLLinkedList` on it *again*,
on the theory that a double-ejected `GObj` could still be dangling in the live list. But since
`GOBJ_PORT_EJECTED_SENTINEL` is only ever set *after* the real unlink already ran, **every** gobj
observed with `obj_kind == 0xFE` has already been fully removed from every list `gcEjectGObj`
manages — there is nothing left to scrub, and the stale/repurposed fields make a second unlink
actively dangerous:

- `gcRemoveGObjFromLinkedList` reads `gobj->link_prev` (stale, dangling into whatever is now at
  that address) and `gobj->link_next` (the **free-list chain pointer**). Writing
  `gobj->link_prev->link_next = gobj->link_next` **splices the free list into the live
  common-link chain** at the position of the stale former neighbor — `gcRunAll`'s per-tick walk
  (`decomp/src/sys/objman.c:2378-2390`) then wanders from a live effect `GObj` straight into
  free-list nodes, and the next `gobj_alloc` (popping that same corrupted free list) hands out a
  `GObj` that's now also reachable from the live chain. Depending on the exact addresses
  involved this manifests as either a UAF (a stale neighbor already reused/freed) or — as in this
  soak — an infinite/near-infinite walk that pins `gcRunAll`'s loop and trips the watchdog.
- The function then also unconditionally nulled `link_prev`/`link_next` afterward, which — when
  hit on an already-freed gobj — **truncates the free list**, permanently leaking every gobj
  chained behind it (a slow pool-exhaustion leak compounding across a long soak).
- The DL-list half has the same defect: `gcRemoveGObjFromDLLinkedList` never resets
  `dl_link_id`, so the existing `dl_link_id != ARRAY_COUNT(gGCCommonDLLinks)` guard does not
  prevent a second call; it writes through stale `dl_link_prev`/`dl_link_next` into whatever has
  since reused that memory.

## Fix

`syNetRbSnapUnlinkSentinelGObjFromLists` now verifies actual list membership (bounded walk of
`gGCCommonLinks[link_id]` / `gGCCommonDLLinks[dl_link_id]`) before touching any link fields,
instead of trusting `obj_kind == 0xFE` alone:

- `syNetRbSnapGObjStillInCommonLink` / `syNetRbSnapGObjStillInDLLink` — new helpers, walk the
  live list from its head and report whether the sentinel `GObj` is genuinely still reachable.
- Only call `gcRemoveGObjFromLinkedList`/`gcRemoveGObjFromDLLinkedList` (and only then null the
  fields) when the membership check confirms the gobj is actually still linked. If it's not found
  — the common case, since the original `gcEjectGObj` already removed it — skip entirely and
  leave `link_next`/`link_prev` alone so the free-list chain and any still-live neighbor stay
  intact.

This preserves the original zombie-eject fix's intent for the (rarer, still-linked) case while
eliminating the corruption on every other call, which is the common case reached from dual-shield
spam churn.

## Verify

- `build-netmenu` rebuilds and links clean (`cmake --build build --target ssb64 -j 4`).
- Re-run the dual-shield Guard-spam soak past the tick class that previously produced
  `WATCHDOG HANG` → `SIGABRT` in `efManagerShieldProcUpdate`/`gcRunGObjProcess`/`gcRunAll`;
  expect no hang and no `RelocPointerTable: invalid/stale token` bursts correlated with shield
  churn. `guard_shield_prune`/`gobj_id_duplicate` churn on id `1011` is expected to continue
  (presentation-only, tracked by the tap-churn phase history) — this fix only targets the list
  corruption, not the underlying id-reuse churn.
