# Netplay: resim replay skips per-tick canonicalization → Castle bumper cross-ISA drift → `figh,item,rng`

**Date:** 2026-07-02
**Scope:** `port/net/sys/netrollback.c` (resim replay loop). `PORT && SSB64_NETMENU`, active VS/resim only.
**Status:** FIX IMPLEMENTED (soak pending — needs a cross-ISA Android/Linux pair to confirm).
**Class:** resim-vs-forward-sim determinism gap. The accepted forward path grid-snaps movable sim state each tick before snapshotting; the resim replay path did not, so a rollback reproduced un-snapped state that drifts cross-ISA away from the canonicalized forward baseline.

## Symptom

`soak2` cross-ISA pair (`session=357459419`, Android host / Linux guest), Firefox + Peach's Castle bumper + attacking the enemy fighter:

```
FRAME_COMMIT_STATE_DIVERGE validation=600 [figh,item,rng]  (inputs MATCH)  x2 (first=600, again 2040)
RESULT: FAIL
pair: first sim_state mismatch tick=523 fields=figh,item,anim
pair: sim_state item-only skew tick=522 expected (FORCE_MISMATCH inject=520)
```

No watchdog hang, no SIGSEGV this run. Diverging fighter fields at the commit are player 1 (Fox,
`status=230` SpecialAirHi setup) `topn_tx`/`topn_ty` — world position — not animation
(`FRAME_COMMIT_FIGHTER_FIELD_PEER_DIFF ... field=topn_tx/topn_ty`).

## Root cause

The FORCE_MISMATCH at tick 520 triggers a rollback: load tick 519 (baseline digest matched on both
peers), then resim 519→522. Immediately after (`sim_state_tick`), `figh` diverges at 523 and never
recovers; RNG then splits — **host drew RNG at ticks 533 and 539, the guest drew none** (guest holds
`rng=0x6322AC14` from 524→601, host advances `0x6322AC14 → 0x7DB3653B @533 → 0xAECAC172 @539`). That is
a position-dependent bumper/hit collision branch firing on only one peer.

Why the position diverged in the first place:

- **Accepted forward ticks** canonicalize (grid-snap) fighters/items/camera in
  `syNetRollbackAfterBattleUpdate` (`syNetplaySimQuantizeActive()` → `syNetplayCanonicalizeActiveFightersForNetplay()`
  + `syNetRbSnapshotCanonicalizeActiveItemsForNetplay()`) **before** `syNetRollbackSavePostTick`. So the
  committed forward history is on the shared `1/65536` grid — the 06-29 fix
  (`netplay_castle_bumper_item_resim_diverge`) that keeps the one movable Castle item (the bumper,
  `itgbumper.c`) deterministic.
- **The resim replay loop** (`syNetRollbackRunResimReplay*`, the `while (t < target)` in `netrollback.c`)
  ran `scVSBattleFuncUpdateBattleSimOnly()` and then **saved snapshots and collected episode hashes with
  no canonicalization**. `AfterBattleUpdate` early-returns while a resim is pending, so its canonicalize
  never fires for replayed ticks.

Result: a resim reproduces the movable bumper (and any fighter in contact with it) **un-snapped**. On
arm64 vs x86_64 the un-quantized bumper physics lands on different sides of a grid cell, the resim
snapshot at each replayed tick diverges from the canonicalized forward snapshot, and the cross-peer
commit at 600/2040 diverges in `item` (bumper position), `figh` (Fox pushed by the diverged bumper),
and `rng` (one-sided hit).

## Fix

In the resim replay loop, immediately after `scVSBattleFuncUpdateBattleSimOnly()` and **before** the
per-tick snapshot save / hash collection, run the same canonicalize pass the accepted path uses, gated
by `syNetplaySimQuantizeActive()`:

```c
if (syNetplaySimQuantizeActive() != FALSE)
{
    syNetplayCanonicalizeActiveFightersForNetplay();
    syNetRbSnapshotCanonicalizeActiveItemsForNetplay();
}
```

Now resim replay produces on-grid state at the same point relative to save/hash as accepted forward
ticks, so both peers' replay is bit-identical to their canonicalized forward sim. Canonicalization is
idempotent (snapping an on-grid value is a no-op), so this cannot perturb already-converged state; it
only removes the un-snapped drift that was unique to the replay window. `PORT && SSB64_NETMENU`,
active VS/resim only; offline links unchanged (`netrollback.c` is netplay-only and the block is behind
`syNetplaySimQuantizeActive()`).

## 2026-07-02 soak result (`session=1597726994`) — figh coupling fixed; residual is a bumper **animation-phase** desync

The resim canonicalize landed and **fixed the fighter coupling**: first sim_state mismatch moved from
`figh,item,anim @523` to **`item`-only @522**, and the tick-600 commit dropped from `figh,item,rng` to
`item,rng`. `figh`/`world`/`rng` now replay bit-identical through the resim.

The residual `item,rng` divergence is **not** cross-ISA float and **not** velocity drift — it is a
constant **3-tick animation-phase offset** of the Castle bumper:

- Both peers resim from an identical baseline (`item=0xA6AD3140 @519`); post-resim `figh`/`world`/`rng`
  match exactly, only `item` differs (host `0x8EAC9B20` vs guest `0x99F58509`).
- The per-tick `item` hash sequences are **byte-identical but offset by 3 ticks**: `host item[t] ==
  guest item[t-3]` (e.g. host 525 = guest 522 = `0x510282C6`; host 600 = guest 597 = `0xBCA8D75A`).
- `item_field_diff @599`: bumper `vel=(0,0,0)` while `pos` sweeps the stage (`-1046.5 → -418.25` X over
  the match) — so bumper motion is **animation/figatree-driven, not physics**. `count=1` both peers.
- `hit_anim_length` off-by-one (host 64935 / guest 64936) is the free-run phase tell.
- Asymmetry: on the **host (follower)** the first post-load baseline mismatched (`live item=0x0DC8675A`
  ≠ slot `0xA6AD3140`) and was corrected by the **peer-baseline re-sync**; the **guest (initiator)**
  matched on the first load. So the host's local snapshot **apply is non-idempotent for the bumper** —
  the peer-baseline fixup restores the item *hash* but leaves the bumper's animation/ProcUpdate *phase*
  ~3 ticks off, and it never re-converges (item stays diverged 522→600, again at 840).

### Next step (separate, deeper)

Chase the bumper **apply phase fidelity**, not float quantization: the Castle bumper's snapshot
apply/identity path (`syNetRbSnapFindLiveCastleBumperGObj`, `syNetRbSnapEjectStaleCastleGBumperShellsBeforeItemApply`,
`syNetRbSnapEnsureCastleBumperAfterParticleReset`, recycled id-1013 matching) restores position but not
the exact animation phase, so re-applying the tick-519 slot on the follower yields a different live
bumper than the slot it came from. The fix is to make the bumper apply reproduce the animation-frame /
figatree timer bit-exactly (capture + restore the driving anim phase), so a rollback load is idempotent
and the follower's resim replays the bumper in-phase with the initiator. Tracked as its own item, since
it is orthogonal to the resim-canonicalize gap fixed here.

## Soak procedure to confirm

Re-run the Android/Linux Castle pair with Firefox + bumper + a hit exchange (`FORCE_MISMATCH` on).
Confirm `figh`/`item`/`rng` stay converged across the whole post-resim window (no
`FRAME_COMMIT_STATE_DIVERGE`), and that RNG advances identically on both peers around any bumper/hit
tick (no one-sided draws).

## Related

- [`netplay_castle_bumper_item_resim_diverge`](netplay_castle_bumper_item_resim_diverge_2026-06-28.md) —
  the 06-29 accepted-tick item canonicalize this extends to the resim path; its 07-02 note first saw the
  Firefox-hit escalation to `figh,item`.
- [`netplay_castle_bumper_hit_anim_length_free_run_fold`](netplay_castle_bumper_hit_anim_length_free_run_fold_2026-07-02.md) —
  the earlier `hit_anim_length` fold fix (now bit-identical cross-peer, ruled out here).
- [`netplay_peach_castle_bumper_rollback`](netplay_peach_castle_bumper_rollback_2026-05-30.md) — same object.
