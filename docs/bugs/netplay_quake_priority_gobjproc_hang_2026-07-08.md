# Quake priority → gcAddGObjProcess WATCHDOG hang — 2026-07-08

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Scope:** `decomp/src/ef/efmanager.c`, `decomp/src/sys/objman.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

soak2 session `163251495` (Kirby/Capt vs Fox, Dream Land): both peers hang mid-match (~sim tick 1345–1346) after Fox `DownBounceU` (status 68):

```
SSB64: syDebugPrintf [1]: om : GObjProcess's priority is bad value
WATCHDOG HANG ... frame frozen
gcAddGObjProcess → efManagerQuakeFuncRun → gcRunGObj → gcRunAll → …
hang_gobj id=1011 link=6 func_run=efManagerQuakeFuncRun
```

`hang_cycle=none` — not a list cycle. Vanilla `gcAddGObjProcess` spins `while (TRUE)` when `priority >= 6`.

## Root cause

1. `efManagerQuakeFuncRun` passes `ep->effect_vars.quake.priority` straight into `gcAddGObjProcess`. Valid camera quakes use **0..3** (`priority = 3 - magnitude`).
2. Recycled `EFStruct`s from the free list do **not** clear `effect_vars`. After FX churn (damage sparks / dust / sparkle on DownBounce), a pending `QuakeFuncRun` shell can read a **stomped `u8 priority >= 6`**.
3. Same session also showed stale `DustHeavyDouble` xf ejects immediately before the hang — effect-pool recycle pressure.

Historical related: pri-4 ImpactWave union alias (`netplay_quake_pri4_*`); those paths were fixed, but the allocator still left union bytes live across recycle.

## Fix

| Change | Purpose |
|--------|---------|
| `efManagerGetNextStructAlloc` | `memset(&ep->effect_vars, 0, …)` on netmenu alloc |
| `efManagerQuakeMakeEffect` | Reject/`clamp` magnitude outside 0..3 |
| `efManagerQuakeFuncRun` | Clamp priority to 0..3 before `gcAddGObjProcess`; NULL-ep safe |
| `gcAddGObjProcess` | Netmenu: reject bad priority + free process node (no `while(TRUE)`) |
| `syNetRbSnapRescheduleQuakeProcIfActive` | Explicit clamp before bind |

## Verification

- Re-soak: Fox hard knockdown → DownBounceU must not hang; expect either normal quake shake or one-shot `quake_func_run_bad_priority` / `gcAddGObjProcess reject` log then continue.
- Confirm no `om : GObjProcess's priority is bad value` followed by frozen `frame`/`yield_count`.
