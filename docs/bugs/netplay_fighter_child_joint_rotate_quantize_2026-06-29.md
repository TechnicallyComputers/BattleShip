# Netplay — fighter child-joint rotate/scale left un-quantized mid-match (cross-ISA `figh` desync)

**Date:** 2026-06-29
**Status:** Fix implemented (`PORT && SSB64_NETMENU`, soak pending)
**Area:** `port/net/sys/netplay_sim_quantize.c` (`syNetplayCanonicalizeFighterSimState`)

## Symptom

Cross-ISA (Android aarch64 host vs Linux x86_64 guest) Peach's Castle DK vs Link soak.
Player grabs with DK (cargo carry — Link Shouldered on DK's back) and runs around.
A rollback resim fires during the carry and the session ends in a genuine cross-peer
`FRAME_COMMIT_STATE_DIVERGE` on `[figh]` with **identical inputs** (`fc_div_inputs_match=1`),
later escalating to `PEER_BASELINE_RESYNC_STORM` / `VS_SESSION_END`. The drift scanner reports:

```
[FAIL] tick 600: diverged=figh inputs=MATCH (genuine cross-ISA determinism failure)
```

Field drill-down (`SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG=1` + `SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1`)
at the diverging tick: `status_id` and `motion_id` **match** cross-peer for both fighters, but
`fhash_full` and `anim_hash` differ — i.e. the divergence is in the **joint pose (rotations)**,
not control flow. A host-only `SYNCTEST_FAIL` at tick 509 showed the same fields with a
**live (quantized) vs blob (raw)** mismatch (`live=0xBFC91000` on-grid vs `blob=0xBFC90FD8` off-grid,
≈ -π/2), which flipped a landing-status decision.

## Root cause

`fhash_full` / the frame-commit `fighter_digest` fold **every** joint's translate **and rotate**
(`syNetSyncFoldFighterSlotFullContribution`, `netsync.c`). Cross-ISA safety for those folds relies on
the live forward-sim pose being snapped to the shared `1/65536` grid before it is hashed/captured.

There were two in-sim quantization sites for child-joint pose, and **between them they left a hole**:

1. **`gcPlayDObjAnimJoint`** (`objanim.c`) calls `syNetplayQuantizeDObjAnimPose` (translate+rotate+scale)
   — but **only for a joint whose figatree track actually advances that frame.**
2. **`syNetplayCanonicalizeFighterSimState`** (`netplay_sim_quantize.c`), the per-fighter canonicalize run
   at the accepted sim boundary for *all* fighters, quantized the **root** rotate (`root_dobj->rotate`)
   but for **child joints only `syNetplayQuantizeDObjTranslate` (translate) — never rotate or scale.**
3. **`syNetplayCanonicalizeFighterIntroJointPose`** does cover child rotate+scale, but its guard
   (`syNetplayFighterInIntroSimScope || ...AppearSimScope`) makes it a **no-op outside intro/appear.**

So a joint that is **held static or posed by coupling rather than by its own advancing track** —
exactly the DK heavy-throw/cargo-carry pose and the Shouldered captive (posed via the grab coupling
matrix, not `gcPlayDObjAnimJoint`) — kept a **raw, un-quantized rotate** during normal gameplay. That
raw value carries cross-ISA float epsilon, is folded into `fhash_full`, copied verbatim into the
snapshot blob (which is intentionally captured raw — see
[`netplay_sector_z_intro_rebirth_quantize`](netplay_sector_z_intro_rebirth_quantize_2026-06-02.md)),
and thus forks the frame-commit `fighter_digest` cross-peer. The synctest fail at 509 is the same hole
seen from the save/restore side: apply re-canonicalizes (quantizes) the live pose while the captured
blob stayed raw → `live != blob`.

The Ness PK-Thunder fix ([`netplay_ness_pkthunder_hold_quantize`](netplay_ness_pkthunder_hold_quantize_2026-06-02.md))
worked around this for Ness only by re-quantizing the blob at capture; that band-aid reintroduces
`full_ok=0` oracle noise and doesn't generalize. The correct fix is to close the **live-sim** hole so
the captured-raw blob is already grid-aligned (oracle stays clean, blob stays raw, FC digest converges).

## Fix

In `syNetplayCanonicalizeFighterSimState`, change both child-joint loops from translate-only to the
full pose:

```c
-   syNetplayQuantizeDObjTranslate(fp->joints[ji]);
+   syNetplayQuantizeDObjAnimPose(fp->joints[ji]);   /* translate + rotate + scale */
    syNetplayQuantizeDObjAnimScalars(fp->joints[ji]);
```

`syNetplayQuantizeDObjAnimPose` is the same function the per-frame `gcPlayDObjAnimJoint` hook and the
intro pass already use, so this is consistent with the established regime — it just applies to **every
fighter in every scope at every accepted sim boundary**, covering joints whose tracks don't advance.
Quantization is idempotent, so for already-animating joints it's a no-op; the intro pass becomes
redundant for children (harmless).

Self-gated by `syNetplaySimQuantizeActive()` (netmenu + VS/resim only); offline never compiles this TU.

## Verification

- `build-netmenu` builds clean.
- Re-soak cross-ISA DK-cargo carry: expect no `FRAME_COMMIT_STATE_DIVERGE [figh]` / no synctest
  pose flip at the carry; `fhash_full`/`anim_hash` should agree cross-peer through the carry window.
- Bisect: `SSB64_NETPLAY_SIM_F32_QUANTIZE=0` on both peers should reproduce the pose drift.

## Audit hook

A cross-ISA `figh`-only `FRAME_COMMIT_STATE_DIVERGE` with **identical inputs** and matching
`status_id`/`motion_id` but diverging `fhash_full`/`anim_hash` = a **joint pose (rotate)** that is
hashed/folded but only quantized in-sim when its track advances. Any DObj field folded into a rollback
hash must be quantized at the sim boundary for *all* fighters/scopes, not only on the animating path.

## Related

- [`netplay_sector_z_intro_rebirth_quantize_2026-06-02.md`](netplay_sector_z_intro_rebirth_quantize_2026-06-02.md) — why the blob is captured raw (oracle parity)
- [`netplay_intro_facing_quantize_2026-06-02.md`](netplay_intro_facing_quantize_2026-06-02.md) — earlier child-rotate gap, intro-scoped
- [`netplay_ness_pkthunder_hold_quantize_2026-06-02.md`](netplay_ness_pkthunder_hold_quantize_2026-06-02.md) — Ness-only blob band-aid this generalizes past
- [`netplay_aobj_interp_quantize_resim_2026-06-26.md`](netplay_aobj_interp_quantize_resim_2026-06-26.md) — same "quantize live where it mutates" principle
