# Netplay AObj interpolation state unquantized in live sim — joint drift / spin after resim

**Date:** 2026-06-26
**Status:** ✅ RESOLVED (`PORT && SSB64_NETMENU`) — necessary-but-not-sufficient precursor to the resim joint-spin fix; validated as part of the soak-confirmed chain (2026-06-27). The remaining spin after this fix had a distinct root cause — a figatree EVENT32 double-un-halfswap during rollback restore/resim; see [netplay_aobj_restore_double_unhalfswap_resim](netplay_aobj_restore_double_unhalfswap_resim_2026-06-27.md), now RESOLVED.
**Scope:** netmenu only; offline build preprocesses the new call out
**Repro:** `debug-resimtest.env` (`SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH=1`, `INJECT_TICK=520`), cross-ISA pair (ARM Android follower + x86 Linux host). Visible symptom: a fighter joint (DK's leg) rotates continuously / on a different track after the resim instead of resuming its pre-resim animation.

## Symptom

After a rollback resim, a fighter's joint animation visibly diverges — a leg that keeps rotating instead of returning to the fixed pre-resim behavior. No crash, and rollback reports success (`resim complete ... rollbacks=1`, snapshot `anim_ok=1`, no follow-on mismatch/rollback). The divergence is therefore *not* caught or corrected by rollback.

## Log evidence

In a `FORCE_MISMATCH @ 520` soak, the per-tick rollback `anim` hash (`sim_state_tick anim=`) **matched cross-peer at match start** (tick 0 ring `anim=0x69B6B9A8` on both Android and Linux; tick 1 `anim=0x44C639A8` on both) but was **completely forked well before the injected mismatch**:

| tick | Android `anim` | Linux `anim` |
|------|----------------|--------------|
| 0    | `0x69B6B9A8`   | `0x69B6B9A8` (match) |
| 1    | `0x44C639A8`   | `0x44C639A8` (match) |
| 518  | `0x06952593`   | `0xE2D13469` (forked) |
| 520  | `0x77EE8D93`   | `0xEB5EAC69` (forked) |

`figh`/`world` stayed in sync; only the animation hash forked, and it never triggered a rollback because mismatch detection keys on `fhash_light`, not the anim hash. This is the **Tier 2B** outcome predicted in `netplay_joint_anim_desync_bisect_2026-05-23.md` (per-tick `gcPlayDObjAnimJoint` arithmetic, after the Tier-1 AObj chain-rebuild fix).

## Root cause

The rollback anim fold (`syNetSyncFoldFighterAnimJointContribution`, `port/net/sys/netsync.c`) hashes each joint's AObj cubic-interpolation node state — `length_invert`, `length`, `value_base`, `value_target`, `rate_base`, `rate_target` — using the **raw, non-quantizing** `syNetSyncHashF32`.

Two asymmetries made that state diverge and never reconcile:

1. **Live sim never quantizes the AObj node fields.** `gcParseDObjAnimJoint` sets `value_*`/`rate_*`/`length_invert` from the figatree stream (incl. `1.0/payload` and `(value_target-value_base)/payload` divisions) and `gcPlayDObjAnimJoint` accumulates `aobj->length += dobj->anim_speed` every frame (`decomp/src/sys/objanim.c`). The existing in-sim hooks quantized only the DObj **output pose** (`syNetplayQuantizeDObjAnimPose`) and the anim **scalars** (`syNetplayQuantizeDObjAnimScalars`) — never these six AObj fields. So the interpolation state carried full-precision, cross-ISA-divergent values frame to frame.

2. **The snapshot *does* quantize those exact fields** on capture and apply (`syNetRbSnapCaptureAObjNode` / `syNetRbSnapApplyAObjNode`, `port/net/sys/netrollbacksnapshot.c`, all six on the 1/65536 grid). So a snapshot-restored replay starts each joint from a *quantized* AObj state that differs from the *unquantized* live trajectory — even on the same machine. At the resim boundary the joint's interpolation track shifts and, because nothing reconciles the anim hash afterward, it keeps advancing on the divergent track: the continuously rotating leg.

The stale comment at the rotate fold in `netsync.c` claimed rotate was "quantized ... again here" in the hash; it is not — `syNetSyncHashF32` hashes raw bits. Output-pose cross-ISA safety came solely from the in-sim `syNetplayQuantizeDObjAnimPose`, and the AObj interpolation inputs had no equivalent.

## Fix

Canonicalize the live AObj node chain on the same grid the snapshot uses, at the point the chain is advanced.

- `port/net/sys/netplay_sim_quantize.{c,h}`: new `syNetplayQuantizeDObjAObjChain(DObj *dobj)` — self-gated on `syNetplaySimQuantizeActive()`, quantizes `length_invert`/`length`/`value_base`/`value_target`/`rate_base`/`rate_target` for every node in `dobj->aobj`, mirroring `syNetRbSnapCaptureAObjNode` exactly.
- `decomp/src/sys/objanim.c` `gcPlayDObjAnimJoint`: call it right after `syNetplayQuantizeDObjAnimPose(dobj)` inside the existing `#if defined(PORT) && defined(SSB64_NETMENU)` block. `gcApplyDObjAnimJointPoseAtFrame` inherits it via its internal `gcPlayDObjAnimJoint` call.
- `netsync.c`: corrected the stale rotate-fold comment.

Result: live forward-sim joints become bit-identical to a snapshot-restored chain, removing the resim-boundary discontinuity, and the cross-ISA anim hash stops forking. Only joints with `anim_wait != AOBJ_ANIM_NULL` (actively animating — the ones that visibly drift) are touched; frozen joints don't advance.

## Verification

- `cmake --build build-netmenu --target ssb64` — clean (pre-existing `ALIGN` redefinition warnings only).
- `cmake --build build-offline --target ssb64` — clean (new call gated out).
- Pending: cross-ISA soak with `FORCE_MISMATCH @ 520` — confirm the `anim` hash tracks cross-peer through the fork window and DK's leg resumes its pre-resim animation after resim.

## Audit hook

Silent `anim`-hash fork (matches at match start, diverges mid-match) with `figh`/`world` in sync and no rollback = animation determinism gap, not a snapshot/topology bug. Any AObj/DObj field folded into a rollback hash must be quantized in the **live sim** wherever it mutates, not only in the snapshot capture/apply path — otherwise a resim perturbs it. Consider also folding the anim hash into mismatch detection so residual forks self-correct.

## Related

- [`netplay_joint_anim_desync_bisect_2026-05-23.md`](netplay_joint_anim_desync_bisect_2026-05-23.md) — Tier-1 AObj chain rebuild; this is the predicted Tier-2B follow-up.
- [`netplay_cross_isa_determinism_2026-05-27.md`](netplay_cross_isa_determinism_2026-05-27.md)
