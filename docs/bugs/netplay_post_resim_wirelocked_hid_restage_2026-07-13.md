# Post-resim wire-locked HID restage — FC figh with inputs MATCH

**Status:** FIX DEEPENED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-13  
**Sessions:**
- `2017633508` seed `2898785898` — TransmittedHistory miss path (initial fix)
- `220551223` seed `1842112848` — published History miss during epoch hold (deepen)

## Symptom

- Synctest OK / 0 FAIL; no `LOAD_HASH_DRIFT`
- GGPO stick correction exclusive-target completes on both peers
- `FRAME_COMMIT_STATE_DIVERGE` **`figh` only**, `inputs=MATCH`
- Consequential FC recovery / resim storm after the diverge (not the cause)

| Session | FC tick | Seed tick | Stick fork |
|---------|---------|-----------|------------|
| `2017633508` | 520 | exclusive target 510 | Linux post-resim HID 8,49 vs wire 11,82 |
| `220551223` | 840 | exclusive target 753 | Linux post-resim hold-last **12** vs wire / `LOCAL_PUBLISH` **18** |

## Root cause

Not Cross-ISA PASS/CLIFF coll. **Post-resim local input restage forked sim while published digests stayed matched.**

### Class A — Transmitted locked, HID restage overwrites (`2017633508`)

| Peer | P0@510 | Path |
|------|--------|------|
| Linux (local auth) | `LOCAL_PUBLISH` **11,82** before resim (on the wire) | After exclusive-target exit, FuncRead restaged tick 510 from wall-rate HID FIFO → `STICK_SAMPLE` **8,49**; `NoteTransmit` realign then clobbered local transmitted to 8,49 |
| Android (remote) | `REMOTE_PUBLISH` **11,82** from `ledger_wire` | Sims 510 with wire truth |

### Class B — History published during hold, Transmitted never locked (`220551223`)

| Peer | P0@753 | Path |
|------|--------|------|
| Linux (local auth) | `LOCAL_PUBLISH` **sx=18** during `tick_commit blocked (load_fail_hold)` before episode resim | Feel-0 egress `NoteTransmit` only locks `t == sim_tick` on INPUT send; hold window can publish History without a Transmitted row. Resync skipped 753 (Transmitted miss). Post-resim `STICK_SAMPLE@753` **sx=12** (hold-last from 752) |
| Android (remote) | `REMOTE_PUBLISH` **sx=18** `ledger_wire` / `post_queue` | Sims 753 with 18; `STICK_SAMPLE@753` sx=18 |

Live TopN.X then diverges for ~80 frames on PASS until FC@840 (`topn_tx`). Resim storm is FC recovery, not the seed.

## Fix

In `port/net/sys/netinput.c` (`PORT && SSB64_NETMENU`):

1. **`syNetInputTryGetLocalWireLockedSample`** — TransmittedHistory **or** published local History (`source=Local`, `!predicted`, matching tick).
2. **`syNetInputStoreLocalDelayFrameFromLatch`** — restage from that lock; promote History-only hits into Transmitted; do not HID-overwrite or `NoteTransmit`-realign.
3. **`syNetInputRollbackPrepareForResim`** — clear `sSYNetInputPortHwLatchTick` so post-exit FuncRead restages and hits the guard.
4. **`syNetInputRollbackResyncControllersAfterResim`** — re-pin local gameplay/delay/Transmitted from the same lock for `[target, frontier]`, then `PublishFrame` so devices / pl latch match before live resume.

## Verify

Re-soak Android↔Linux with stick GGPO that exclusive-targets past an already-published local tick (including during `tick_commit blocked`):

- Post-resim `STICK_SAMPLE` at exclusive `target` matches pre-resim `LOCAL_PUBLISH` / peer `REMOTE_PUBLISH`
- No `FRAME_COMMIT_STATE_DIVERGE figh` with `inputs=MATCH` from the 510/753-class fork
- Existing feel-0 first-sample path unchanged (no Transmitted and no published History → still HID)

Related: [`netplay_kirby_inhale_post_resim_input_reconcile_2026-07-04.md`](netplay_kirby_inhale_post_resim_input_reconcile_2026-07-04.md), [`netinput_post_resim_published_reconcile_2026-05-19.md`](netinput_post_resim_published_reconcile_2026-05-19.md), [`netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md`](netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md), [`netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md`](netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md) (different classes).
