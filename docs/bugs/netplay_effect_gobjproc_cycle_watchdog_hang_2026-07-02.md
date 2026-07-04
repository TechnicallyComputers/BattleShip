# Interface/effect process-queue cycle — WATCHDOG HANG in gcRunAll traversal

- **Date:** 2026-07-02
- **Build scope:** `PORT && SSB64_NETMENU`
- **Status:** FIX IMPLEMENTED (soak pending)

## Symptom

soak2 sessions `279353420`, `352797257` (and `1182615431` before them): determinism is fully clean —
`RESULT: PASS`, `LOAD_HASH_DRIFT=0`, `SYNCTEST_FAIL=0`, `fc_item_div=0`, `fc_rng_div=0`, only
`intro_wait` skips remain. But **both peers deterministically WATCHDOG HANG** shortly after the
last sim tick (host `max_sim_tick=2685`), then the watchdog SIGABRTs them:

```
SSB64: WATCHDOG HANG since_frame=3001ms since_yield=3001ms ... scene=22(VSBattle) frame=3465
... (repeats, frame/yield never advance)
gcRunGObjProcess+0x10d
gcRunAll+0xd5
ifCommonBattleUpdateInterfaceAll+0x7f
scVSBattleFuncUpdate+0x2a2
SSB64: !!!! CRASH SIGABRT   pc=gcRunGObjProcess+0x10d  x0=0x7f48b8276ed8
```

The first pass suspected an effect GObj (`id=1011 link=6`) from the pre-diagnostic crash context,
but the next soak's unconditional hang snapshot corrected that: the last normal traversal object was
`id=1016 link=11 parent_id=1018`, i.e. `nGCCommonKindInterface` on
`nGCCommonLinkIDInterface` with a `nGCCommonKindPublic` (audience reaction) proc parent. The newer
backtrace also stopped in `gcPortGcRunAllTraversalFingerprintEx`, whose first loop walks
`gGCCommonLinks[i]` by `link_next`, proving this is a **common-link cycle**, not a process-queue
`priority_next` cycle. This is the same class as the 07-01 dual-shield hang
([netplay_guard_shield_sentinel_relink](netplay_guard_shield_sentinel_relink_2026-07-01.md)):
`gcRunAll` / diagnostics never terminate because a freed/sentinel GObj's free-list `link_next` got
spliced into a live common-link chain. It only surfaced now that the `ftMainPlayAnim` NULL-TransN
SIGSEGV and the earlier effect SIGSEGVs are fixed, so the soak survives long enough to reach it.

## Analysis so far

- `gcPortGcRunAllTraversalFingerprintEx+0xf4` / `+0xfb` lands in the `gGCCommonLinks[i]` walk, before
  the later `sGCProcessQueue` walk. The diagnostic itself was hanging because that walker had no cycle
  guard.
- `hang_gobj id=1016 link=11 parent_id=1018` decodes to HUD/interface + audience/public, not effect
  VFX. `nGCCommonLinkIDInterface` churn after bumper collision / KO / stock-HUD updates is the current
  lead.
- `id=1011 link=6` remains normal effect-kind churn, not a leak — `gobj_link_audit ef6` stayed bounded
  at 3–5 in the earlier soak.
- Static analysis still cannot pin the corrupting eject/relink site. The cycle is created earlier and
  only manifests when the diagnostic or `gcRunAll` later walks the damaged chain.

## Diagnostic wired (this change)

`syNetplayResimReplayHangDiagLogHangSnapshot` prints exactly the culprit — `hang_gobj id link kind
func_run proc_kind proc_pri parent_id proc_func proc_func_id ...` — and `port_watchdog` already
calls it on every hang. But the snapshot **and** the per-proc culprit recording early-returned
unless `SSB64_NETPLAY_RESIM_REPLAY_HANG_DIAG` was set in the environment, which soak captures don't
set — so every hang printed stale zeros / nothing.

First diagnostic change: changed `port/net/sys/netplay_resim_replay_hang_diag.c` so that:

- `syNetplayResimReplayHangDiagNoteGcRunGObj` and `...NoteGcRunGObjProcessBegin` record the
  last-run GObj / gobjproc fields **unconditionally** (cheap field stores, no logging). Verbose
  per-frame replay logging stays gated behind the env var, so there is **no new log spam**.
- `syNetplayResimReplayHangDiagLogHangSnapshot` dumps **unconditionally** (it only runs from the
  watchdog after >3s of no progress — zero hot-path cost).

Next soak that reproduces the hang will emit:

```
SSB64 Netplay: RESIM_REPLAY_HANG_DIAG hang_gobj id=.. link=.. kind=.. func_run=.. proc_kind=.. proc_pri=.. parent_id=.. proc_func=.. proc_func_id=.. ...
```

This identified `id=1016 link=11 parent_id=1018`, proving the strong lead is interface/public GObj
lifecycle rather than effect VFX.

Second diagnostic change: changed `decomp/src/sys/objman.c` / `objman_gcport.h` so that:

- `gcPortGcRunAllTraversalFingerprintEx` caps each common-link / process-queue traversal at
  `GCPORT_TRAVERSAL_LINK_LIMIT` (2048 nodes), so the hash diagnostic cannot be the thing that spins
  forever on a corrupt list.
- New `gcPortSnprintGcRunAllTraversalCycleDiag` uses bounded tortoise/hare checks over both
  `gGCCommonLinks[]` and `sGCProcessQueue[]`.
- `syNetplayResimReplayHangDiagLogHangSnapshot` now prints:

```
SSB64 Netplay: RESIM_REPLAY_HANG_DIAG hang_cycle common_cycle link=.. head=..:g.. meet=..:g.. steps=.. heads=L..:g..,...
```

or `proc_cycle ...`, directly naming the cyclic chain and a bounded head sample.

## 2026-07-03 root cause update

The next Firefox + Castle-bumper soak (`session=78270984`) proved the bumper determinism fixes held
(`RESULT: PASS`, 19/19 synctest OK, no frame-commit divergence), then hung on the first KO. The new
diagnostic named the real corrupted list:

```text
RESIM_REPLAY_HANG_DIAG hang_cycle proc_cycle pri=3 head=...:p1000 meet=...:p1011 steps=3
```

The sampled backtrace sat in `gcPlayAnimAll` / `syNetplayQuantizeDObjAnimPose`, but the bounded cycle
probe shows that was repeated work from a cyclic `sGCProcessQueue[3]`, not a DObj tree loop. The KO
logs immediately before the hang show rebirth-halo reclaim ejecting and re-minting effect GObjs,
including an EF pool audit repair:

```text
gcEjectGObj ... id=1011 ... gpr_head=...
gcEjectGObj ... id=1011 ... gpr_head=0x0 obj=0x0
EfManager: pool_audit mismatch site=rebirth_halo_reclaim ... -> repair
gobj_alloc ... id=1011 link=6
REBIRTH_GATE ... halo_ensure ... reclaimed=1
```

The process-cycle node's parent was the recycled `id=1011` effect while the queue sample also included
interface `id=1016`, matching the earlier interface/public lead but moving the actual invariant break to
the shared `GObjProcess` priority queue.

Root cause: `GObjProcess` free-list nodes retained stale `priority_next` / `priority_prev` links. If
rebirth/effect repair double-ended a process or recycled a free-list node whose priority links still
pointed into `sGCProcessQueue`, `func_80007784` blindly trusted those stale links and spliced through
live queue nodes. A later reinsert could then create a priority-queue cycle.

Fix: harden `objman.c`'s process ownership boundary for netmenu builds:

- `gcSetGObjProcessPrevAlloc` now unlinks by bounded queue scan and clears `priority_next` /
  `priority_prev` before pushing a process onto the free list.
- `gcGetGObjProcess` clears stale priority links on allocation and tries to detach the process from any
  live queue by pointer identity before reuse.
- `gcLinkGObjProcess` performs the same pre-link detach/clear, catching recycled nodes even if their
  priority was overwritten before cleanup.
- `func_80007784` no longer trusts `priority_prev` / `priority_next`; it scans all priority queues for
  the process pointer and unlinks from the actual queue head, then clears the priority links. Stale links
  on a non-member are logged and cleared instead of being used for blind splicing.

Offline builds keep the vanilla queue operations.

## Next step

Re-run the first-KO rebirth-halo soak. Expected: no `proc_cycle` watchdog hang after `REBIRTH_GATE
halo_ensure`; if any stale process node is encountered, the log should show
`gobjproc_priority_stale_links_cleared` / `gobjproc_priority_unlink_repaired` and the frame should
continue.

## Verification

- `build-netmenu` and `build-offline` link clean; no linter diagnostics on touched files.
