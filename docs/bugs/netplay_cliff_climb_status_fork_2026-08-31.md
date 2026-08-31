# Ledge-climb status fork (not Kirby)

**Soak:** 2026-08-31, first with MATCHED builds after the joint-rotate fix.
**Class:** genuine gameplay state divergence, distinct from every Kirby-window class.

## Evidence

FC diverge at validation 1032. Cross-peer blob dumps for player 1 at tick 1031:

| field | linux | android |
|---|---|---|
| `st` | 85 | 86 |
| `mot` | 73 | 74 |
| `tics` | **92** | **7** |
| `hitstat` | 1 | 3 |
| topx / topy | 0xC51A4000 / 0xC3A50000 | 0xC51D1000 / 0x42B40000 |

Player 0 is byte-identical in every field. Only p1 forks.

Transition trace, live passes:

```
android  tick=1024 player=1 from=85 to=86  caller=ftCommonCliffQuickOrSlowSetStatus
linux    (no such transition at 1024)
both     tick=1035 player=1 from=86 to=87
           linux   caller=ftCommonCliffQuickProcUpdate
           android caller=ftCommonCliffClimbQuick1SetStatus
```

Android advanced the ledge-climb chain at 1024; Linux was still in status 85 with
`tics=92` at 1031 (~11 ticks late), then reached 87 by a DIFFERENT caller. So the two peers
walked different code paths through the cliff-climb state machine.

`tics=92` vs `7` also means the divergence is not a one-tick timing skew — Linux had been
sitting in 85 for 92 ticks. The `topy` gap (−330.0 vs +90.0) is a large vertical position
difference: one peer hanging, the other climbed.

## Why this matters

Every instrument built during the Kirby hunt (transition trace, blob dumps, per-joint
dumps) was aimed at the up-B path. This fork is in the common ledge-climb code
(`ftCommonCliff*`), fires with the same tooling, and is the dominant remaining desync in
this soak — the Kirby-window residue is now a joints-only artifact (see below).

Suspect surface: `ftCommonCliffQuickOrSlowSetStatus` chooses quick vs slow climb from
damage/percent and stick state; a fork there splits the whole subsequent chain. The
cliff-catch/climb path also reads `passive_vars` and cliff-hold coupling that the snapshot
must round-trip.

## Same-soak residue, separate class

- Diverge @654: light identical, full differs **only in `joints`** — the residual
  joint class, now with matched builds so it is real signal.
  `blob_joint_split` (jt/jr sums) added to say whether it is translate or rotate.
- Diverge @2881: `figh` and `world` IDENTICAL, `eff` differs — an effect-partition-only
  fork at session end (blade shells), healed.

## Next capture

Ledge-climb repro (hang and climb, both players, ideally while the other is mid-up-B) with
the standard env. The trace already names the callers; add the entry conditions if the
fork proves input-dependent rather than state-dependent.

---

## Reproduced, and the branch is named

Second soak (matched builds, 6037 ticks — longest yet). The user drove ledge grab/climb
during up-B and hit it twice. The joints-only false-positive class is **gone**: every
diverge now carries real light-field differences.

**The branch point, tick 4208 — same tick, same source status, different destination:**

| | Linux (predicting) | Android (p1's OWNER) |
|---|---|---|
| live | `86 -> 87` `ftCommonCliffQuickProcUpdate` (climb) | `86 -> 92` `ftCommonCliffAttackQuick1SetStatus` (**attack**) |
| its own resim | `86 -> 87` (climb) | `86 -> 87` (**climb**) |

The owner's live took ATTACK; the owner's own resim of that tick took CLIMB. Android's
input history for p1 across 4206-4210 is **identical every tick**:
`btn=0x0000 sx=2 sy=84 pred=0`, no downgrade. Stick-up with no buttons means climb — so
live acted on a button the authoritative record does not contain.

**Why:** `ftCommonCliffAttackCheckInterruptCommon` branches on
`fp->input.pl.button_tap & (mask_a|mask_b)` — a DERIVED edge latch, not raw input. The
rollback apply deliberately restores `pl_button_hold` but **not** `pl_button_tap` /
`pl_button_release`; the comment at that site records why (restoring stale blob taps caused
an Android-only Kirby inhale fork at tick 524). So both restoring and not-restoring have
produced forks — the latch's value on the first replayed tick after a load belongs to the
pre-load frontier either way, and any status proc that reads it before
`ftMainProcessInput` re-derives it for that tick sees the wrong edge.

**Not fixed blind.** Candidate directions, in preference order:
1. Re-derive tap/release at load from restored `button_hold` + the authoritative history
   row for the loaded tick — deterministic, uses the record rather than either stale value.
2. Restore tap/release but only for the first replayed tick, then let the normal
   re-derivation take over.
3. Make cliff (and peers of it) read a snapshot-covered input source.

`SSB64_NETPLAY_CLIFF_DIAG=1` (decomp `8512eac89`) logs tap/hold/release, the A|B mask,
whether it fires, and the resim flag at the decision — so the next repro shows the latch
values on both sides of the fork and confirms which candidate is right before code moves.

## Same soak, other classes

- @6035: `figh` and `world` byte-identical, `eff` only — the blade partition, healed.
- @4216: the `rng` partition also forked, downstream of the different action being taken.

