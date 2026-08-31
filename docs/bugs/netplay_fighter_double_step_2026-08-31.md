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

---

## blob_light_dump soak: the chain is closed

The dumps did their job in one session:

1. `baseline_universe` dumps at tick 953 are **byte-identical across peers** — the
   universes agreed; the fork forms between 953 and the validation at 961, in a dual
   up-B window.
2. Android's `LOAD_HASH_DRIFT tick=907` is **eff-only** (`0x811C9DC5/0x8D6A8DD8`) with
   `class=snapshot_fidelity`: capture of 907 correctly folded NO effects — p1 had just
   entered up-B (`tics=0`), pre-mint — but the load-verify of that blob found a blade.
   The leftover live blade from ticks 908+ survived the load.
3. What kept it alive: **the stage-wide eject refusal** from the churn fix. During a load
   to a pre-mint tick, the slot listing no blade IS the authority — ejecting the leftover
   is the slot speaking, not reconciliation guesswork. The refusal blocked exactly that
   cull, the replay ran with a blade five ticks early, and the blade's presence forked
   p1's light state (is_effect_attach/force-clear inputs) from the peer without that
   history — the cross-peer LIGHT-hash ring fork observed at 2350 in the prior soak.

**Fix:** the refusal is now slot-aware. `syNetRbSnapshotLoad` publishes its slot for the
duration of the apply; the refusal consults it — if the active slot lists a userdata-joint
shell for the owner it keeps refusing (canonical), if it lists none the cull proceeds
(pre-mint tick), and outside loads (no active slot) the refusal stands as before (forward
sweeps stay barred).

The double-step-titled saga thus ends somewhere else entirely: readout artifact on top of
a blade-lifecycle/load-ordering interaction, introduced by one week-old overreach and
found by four generations of falsified witnesses plus one cross-peer dump. The witnesses
stay in the build — they are cheap, silent when healthy, and the next anomaly of this
family will name itself.

---

## Slot-aware refusal validated; the residual fork is FULL-only

Verification soak: **zero `LOAD_HASH_DRIFT`** (was 6+/soak) — pre-mint loads cull leftovers
again, the churn stayed dead, and the session ran 1965 ticks. Two diverges remain
(1951/1959, healed), and the dumps narrow them another level:

- `blob_light_dump` lines **byte-identical cross-peer** at both diverges (all ~25 light
  scalars agree: positions, velocities, counters, stick latch).
- Header hashes: **anim identical, light identical, `blob_fhash_full` differs — player 0
  only** (0x6BB24F4C vs 0x71C3774C @1950; 0xE28C8272 vs 0x3602FA72 @1958).

The fork lives in the full-fold's extras: {hitstatus, invincible_tics, is_hitstun,
is_shield, shield_health, jumps_used, motion_attack_id, vel_jostle_x/z, joint transforms}.
Dual-up-B window again, so vel_jostle (fighter push-apart, computed from the other
fighter each tick) and hit/invincibility state are prime suspects.

New `blob_full_dump` companion line: the 10 full-only scalars individually + all joint
transforms folded to one checksum, so the next diverge distinguishes scalar-vs-joint and
names the field.

Note on the reported "crash": both logs end in clean shutdown with no CRASH/backtrace and
no wedge — whatever was seen on screen (likely the desync kick or a frozen final frame),
the processes exited normally. If it reproduces as a real crash, capture logcat on Android
alongside.

---

## blob_full_dump soak: the column is `joints`

Cross-peer diff at both diverges (1464 both players, 1626 p0): **every scalar matches** —
hitstatus, invincible, hitstun, shield, jumps, motion_attack_id, vel_jostle — only the
`joints` checksum differs. Combined with the identical anim hashes: the skeletons were
posed differently under the same animation state, by the small per-joint deltas the
live-vs-blob diffs have shown all along (e.g. j5_ry 0xBF16F500 vs 0xBF15B200).

Working hypothesis, now testable: joint poses are DERIVED state — computed from anim
frame/wait (already folded) through trig-heavy interpolation whose float paths differ
cross-ISA. The fold quantizes, so epsilons normally vanish; a couple of times per soak an
epsilon straddles a quantization boundary and the full hash forks. That would make this
residual class a false-positive FC diverge on derived presentation state, not a sim fork —
consistent with every occurrence healing cleanly and gameplay staying correct.

`blob_joint_dump` added (per-joint subhashes, 8 per line): the next diverge names the
joint indices. If they are scattered mid-skeleton joints with epsilon deltas, the fix is
structural and deliberate: remove (or coarsen) raw joint transforms in the FULL fold —
they add nothing sync-authoritative over the anim fold — applied symmetrically to
`syNetRbSnapHashFighterBlobFull` and `syNetSyncHashFighterSlotFull`. If instead one
specific joint (17/TopN — blade-adjacent) dominates, the blade attach machinery goes back
under the lens.

