# Joint rotate: hashing a union member that isn't live

**Found by:** the funnel of instruments built across the Kirby desync hunt —
partition → hash class (full) → column (joints) → joint (j1) → raw components.
**Class:** false-positive sync divergence. Same shape as the quake
`effect_vars.priority` fold (2026-07-02): a field folded where it is not authoritative.

## The evidence

Cross-peer `blob_j1_raw` at five dump sites, one soak:

```
linux    j1r = 0x00000000, 0x3FC91000, 0x00000000   ->  [0, pi/2, 0]
android  j1r = 0x3FC96800, 0x3AD40000, 0x00000000   ->  [1.5734, 0.0016, 0]
```

- The rotation sits on a **different axis** on each peer — not an epsilon.
- Android's value is **constant across 250+ ticks** while Linux's changes.
- **Every child joint of j1 matches**, every scalar matches, the anim hash matches.

A pose difference at j1 would propagate to its children. It does not. So the drawn pose
agrees and only the `rotate` *member* disagrees.

## Why

`gcDrawDObj` (`decomp/src/sys/objdisplay.c`) walks `dobj->vec->kinds[]`: a kind of `2`
supplies a `GCRotate` out of `dobj->vec->data`, and the DObj's own `rotate` member is then
**not used for drawing** — it holds stale bytes with no determinism guarantee across peers
or ISAs. Both the live fold (`netsync.c`) and the blob capture were hashing it
unconditionally for every joint.

That is why this survived every earlier instrument: nothing was wrong with the simulation.

## Fix

`syNetRbSnapshotDObjRotateIsLive(dobj)` — TRUE when `dobj->vec == NULL` or no `kinds[]`
entry is `2`. Applied symmetrically:

- **Live fold** (`syNetSyncFoldFighterJointContribution`): fold joint rotate only when live.
- **Capture**: store zeros for non-live joints, so both peers store identical bytes.
- **Apply**: skip writing the placeholder back over a vec-driven joint.

Translate is untouched — it is always the DObj's own member. Facing desync detection
(`joints[TopN]->rotate.y == lr*90deg`, the reason rotate is folded at all) is preserved:
TopN is not vec-overridden.

The exported predicate takes `void *` because `netrollbacksnapshot.h` is included ahead of
the decomp object headers in some TUs.

## Expected

The residual Kirby FC diverges — full-only, joints-only, one joint, healing every time —
should stop. Real pose forks still fold through translate, the anim hash, and live-rotate
joints.

---

## IMPORTANT: this change requires BOTH peers rebuilt

The fix alters snapshot semantics, not just hashing:

- capture stores zeros for non-live joint rotates,
- apply skips writing that placeholder back.

A match between a fixed peer and an unfixed peer therefore has genuinely different
post-load state and **will desync by construction**.

The 2026-08-31 verification soak was exactly that mixed match: Linux logged `j1r` as zeros
in all 16 dumps (fix active), Android logged live values in all 15 — including the same
constant `[1.5734, 0.0016, 0]` seen in the pre-fix soak (fix absent). Every joints-column
difference in that soak is explained by the version skew, and no conclusion about the
fix's effectiveness can be drawn from it.

**Rebuild and redeploy the Android APK at 50d65f62 or later before the next soak.** The
`blob_j1_raw` line is the check: both peers must show the same zero/non-zero pattern.

