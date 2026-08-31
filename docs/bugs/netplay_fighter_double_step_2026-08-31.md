# Fighter double-step: live state creeps ahead of the ring

**Status:** event witness armed; source not yet named.
**Class:** independent of the blade (recurred with the churn fixed), currently the primary
Kirby-window desync driver.

## Evidence across three soaks

| soak | drift | fields moving together |
|---|---|---|
| pre-audit | +2 (p0) | `status_total_tics` 12v10, `hold_stick_y` 17v15, `vel_air`, pos, joints |
| churn soak | +2 (p0) | same signature |
| post-refusal | **+3** (p0) | `status_total_tics` 10v7, `hold_stick_y` 27v24, `pl_stick_prev`, joints ~3 frames |

Each time: **every status transition matches** across peers AND across live/resim (proven
by the armed `all`-band trace), the rng partition is clean, and the eff partition follows
rather than leads. The fighter's whole per-tick update — counter, stick latch, physics,
anim — ran extra times relative to the ring blob of the same tick. Accumulative: incidents
add up (+2 → +3). Not caused by the blade eject/mint churn (fixed and verified absent in
the third soak: 0 refusals needed, 2 ejects, resims 89 → 21 — the diverge still came).

## Witness (this commit)

`ftMainProcUpdateInterrupt` owns the `status_total_tics++`; its existing
`syNetFighterPhaseOnInterruptVeryStart` hook now counts invocations per
(player, authoritative tick) — mirroring the increment's own `is_control_disable` guard —
and logs at the exact second non-resim update of one tick:

```
SSB64 NetFighterPhase: DOUBLE_STEP player=N tick=N status=N total_tics=N rollback_active=N frame=N
```

Always on in netmenu VS (no env needed). Resim passes are excluded — replay legitimately
revisits ticks; the observed drift is in the live/forward direction.

## Next soak

Any `DOUBLE_STEP` line names the tick and frame of an incident; correlate with the
surrounding lines (resim episode boundaries, load/verify stages, frontier rescues) to name
the caller. If diverges occur with NO witness line, the model is wrong — the extra
stepping would then be inside resim passes (a tick replayed twice within one episode) and
the witness gets extended there.

---

## First witness soak: all 70 hits at resim boundaries — witness refined

Every `DOUBLE_STEP` (32 linux / 38 android) sat within 25 lines of a `POST_RESIM_LIVE
sim==target` / `resim complete` — i.e. the correct GGPO re-execution of a tick whose
original run was discarded by the rollback load. The v1 witness could not distinguish that
from a real leak.

v2 is load-generation-aware: `syNetFighterPhaseNoteRollbackLoad()` (called from the resim
initial-load site) voids the per-slot counts, so a hit now means a tick ran twice at
resim=0 **within one generation** — no load in between, no legitimate explanation. If the
next drifted FC diverge arrives with v2 silent, the extra stepping is inside flagged resim
(a tick replayed twice within one episode) and the witness extends there.

## Same soak, blade attribution paid off

`ctx=` names the remaining vanish: 34 live-forward `generic_eject` kills of in-scope
blades (plus 18 correctly refused at verify). The zombie/dead sweeps run on forward frames
too — outside the stage-limited first refusal. The refusal now covers ALL stages: only
`force_clear_sim` may destroy an owner-in-scope blade, anywhere. Out-of-scope teardown and
orphan sweeps unaffected. If blades still vanish with zero `effect_eject` lines, the
remaining suspect is game-side effect-pool eviction (`efManagerMakeEffectForce` recycling
the oldest shell under dual-up-B effect pressure), which routes through `gcEjectGObj`, not
these paths — that would need its own decision since pool eviction is sim behavior.

Also validated this soak: the peer-silence forfeit attributed the RIGHT player
(forfeiter=1, the vanished peer; result posted 204).

---

## v2 soak: silent witness, +1 drift on BOTH players — the model inverts on schedule

v2 (load-generation-aware, live-only) recorded **zero** hits, and the FC diverge at 1075
shows the drift shrunk to exactly **+1 `status_total_tics` on both players**
(13v12 / 18v17). Pre-committed conclusion applies: the extra stepping is not an unexplained
live re-run — it is one whole tick executed twice per episode, split across the resim flag.
The precise suspect: **the resim walk executes the target tick at `resim=1`, then
`POST_RESIM_LIVE` hands the same tick to live, which runs it again at `resim=0` — same
generation, no load between.** +1 per episode, both fighters, invisible to a witness that
only counts one side of the flag.

v3 counts every execution per (load generation, tick), with a flags field recording which
combination fired: `flags=3` (one flagged + one unflagged) is the walk-overruns-target
case named directly; `flags=2` twice would be an in-walk repeat; `flags=1` twice an
unexplained live repeat. The generation bump moved from the initial-load log site to
`syNetRbSnapshotLoad` itself — the chokepoint every load (including mid-episode extension
reloads) passes, so legitimate extension re-replays stay unflagged.

Blade status same soak: eject contexts fully converged — 32 `force_clear_sim` + 1
out-of-scope, zero snapshot-machinery kills of in-scope blades, 57 refusals holding the
sweeps off. The blade layer is now behaving; the ±1 double-step is the remaining Kirby
sync driver.

---

## v3 soak: silent again — and the real fork finally surfaced in the ring hashes

v3 (all executions, per load generation) recorded **zero** hits across a 2889-tick session
with 2 diverges. Within any load generation, no fighter tick ran twice, flagged or not.
**The double-step hypothesis is dead** — the accumulated evidence now reads differently:

- The live-vs-blob "+N `status_total_tics`" that named this doc is (at least partly) a
  READOUT ARTIFACT: the field-diff harness compares live state at validation-processing
  time against the snap-tick blob — live has legitimately advanced, so every dump carries
  a uniform one-tick offset (this soak: anim 13.0v12.0 on every joint, +1 on every
  counter, both peers identical).
- The genuine fork was in the header hashes all along: at validation 2351, the two peers'
  RINGS disagree on **player 1's blob LIGHT hash** (0x491C2DCE vs 0xB0665792) with the
  FULL hash following and the ANIM hash **identical**. Player 0's blobs match perfectly.
  A narrow, non-skeletal field fork inside the light fold: status/physics/stick-latch/
  TopN/hitlag territory.

**New instrument:** `blob_light_dump` — one line at each diverge dumping the light-fold's
scalar inputs from the BLOB (fixed order, fold-hashed floats). Cross-peer offline diff of
the two lines names the forked field exactly, the way the rng walk names rng forks.

Next soak: `grep blob_light_dump` both logs at the first diverge, diff the two player-1
lines, and the differing column is the bug's address.

