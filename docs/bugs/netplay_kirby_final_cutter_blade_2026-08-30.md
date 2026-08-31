# Kirby Final Cutter: blade vanishes mid-jump and never returns

**Symptom (player):** "the up+B special on kirby is glitching, it's causing the blade kirby
carries to disappear mid jump and stop rendering."
**Status:** root cause identified with log evidence; fix proposed, not yet implemented.

## The blade is an effect, and it is not in the snapshot

The blade is not part of Kirby's model and not the cutter *weapon* (that is the landing
shockwave, `wpkirbycutter.c`). It is an **effect GObj**, created by
`efManagerKirbyCutterDraw/Up/DownMakeEffect` from `ftKirbySpecialHiUpdateEffect`
(`ftkirbyspecialhi.c`), driven by `motion_vars.flags.flag2` on specific animation frames.

Rollback snapshots capture effects only when they carry a recognised respawn class. The
full set is:

```
FOX_REFLECTOR  IMPACT_WAVE  NESS_PK_WAVE  NESS_PSYCHIC_MAGNET  PIKACHU_THUNDER_SHOCK
QUAKE  REBIRTH_HALO  SHIELD  USERDATA_JOINT  YOSHI_EGG_ESCAPE  YOSHI_EGG_LAY
YOSHI_SHIELD  NONE
```

**There is no Kirby Final Cutter blade class.** Confirmed empirically: across the up-B
windows in this soak the snapshot recorded `effect_count=0` at essentially every tick.

Instead the blade relies on a bespoke side mechanism — `effect_protect` /
`effect_eject reason=kirby_finalcutter_blade` / `effect_attach_restore reason=kirby_finalcutter`
plus `syNetRbSnapForceClearKirbyFinalCutterBlades()` calls in the decomp — where every other
character's persistent effect gets a real capture/restore class.

## The failure, in the log

Player 1's up-B, host log (status 256 = `SpecialHi`, 257 = `SpecialHiLanding`):

```
effect_protect        tick=1472 player=1 status=256   (x8)
effect_attach_restore tick=1472 player=1 status=256   (x2)
effect_attach_restore tick=1475 player=1 status=256
effect_eject          tick=1475 player=1 status=256   <-- ejected MID-ASCENT
effect_eject          tick=1501 player=1 status=256   <-- still mid-move
effect_eject          tick=1509 player=1 status=256
effect_eject          tick=1512 player=1 status=257   <-- landing, legitimate
```

After the eject at 1475 there is **no further restore** — only repeat ejects — so the blade
is gone for the remaining ~37 ticks of the move. That is the reported glitch exactly.

The mechanism is not uniformly broken, which is why it is intermittent: the earlier eject
at tick 1130 (also status 256, during a resim) *was* followed by ~28 `effect_attach_restore`
events and recovered. The tick-463 eject at status 257 is a normal end-of-move teardown,
not a fault.

## Why no desync is reported

`is_effect_attach` is deliberately excluded from the fighter sync hash (`netsync.c`, "Do not
fold is_effect_attach"). So blade state can differ between peers without tripping any
detector — and it does here. On the same move the guest ejects at 1501 and again at **1547
with `status=10` (Wait)**, i.e. a stale blade cleaned up long after the move ended, while
the host had already destroyed it at 1475. Divergent blade lifetimes, invisible to sync
validation, visible on screen.

## Proposed fix

Give the blade a real respawn class — `SYNETRB_EFFECT_RESPAWN_KIRBY_CUTTER_BLADE` — so it is
captured and restored like the Fox reflector, Yoshi egg, and rebirth halo, and retire the
bespoke protect/eject/attach-restore path for it.

That is the structural fix, and it is the same shape as the accessor migration doctrine: the
existing mechanism is a mirror of state that should simply be snapshotted. It cannot be
done as a one-liner, since it needs a capture predicate (`syNetRbSnapLiveEffectIsKirbyFinalCutterBlade`
already exists and can classify it), the joint/DObj attach data in the blob, and a mint path
on restore.

Interim, much smaller: `ftKirbySpecialHiUpdateEffect` only re-creates the blade when
`motion_vars.flags.flag2` is 2/3/4, which are consumed on specific animation frames. A
rollback landing after those frames can never re-mint. Re-arming the flag when the fighter
is in `KirbyFinalCutterScope` with no live blade would recover the visual without touching
the snapshot layer. This treats the symptom, so prefer the respawn class if scope allows.

---

## Fix (2026-08-30): protect + re-mint

The mid-move eject's exact source was found: `syNetRbSnapEjectAllNonCanonicalEffectsForVerify`
ejects every live effect not in the slot's canonical set, and — unlike
`syNetRbSnapEjectHiddenCosmeticEffectShellForVerify` — consulted **none** of the verify
protects. The blade can never be canonical (it is excluded from capture), so every
verify-only repair pass killed it regardless of owner scope. That is also why the bug was
intermittent: the pass runs only when the verify repair stage is armed.

Three port-side changes (`netrollbacksnapshot.c`, no decomp edits):

1. **Protect** — the non-canonical verify eject now honors the same four protects as the
   hidden-cosmetic path (Yoshi egg lay shell, Captain kick flame, Kirby cutter blade, Ness
   PK wave). The blade is the load-bearing case; Ness hold-only `respawn=NONE` shells
   shared the not-canonical exposure.
2. **Evidence preserved** — the blade eject no longer clears `is_effect_attach` for an
   owner still mid-move (in cutter scope, non-Landing). The restored flag is the truth for
   "a blade should exist at this tick"; clearing it made any wrongful eject permanent.
   Landing and out-of-scope owners keep the full clear.
3. **Re-mint** — the reconcile pass (runs on every load and every forward frame) now
   re-creates the carried Draw shell via `efManagerKirbyCutterDrawMakeEffect()` when the
   owner is in scope (non-Landing), `is_effect_attach` is set, and no blade lives.

Sync safety: the blade is excluded from the rollback fold and `is_effect_attach` is
excluded from the fighter hash, so a mint on one peer cannot diverge sync state.
`motion_vars.flags` (which are folded) are not touched — that is why the mint calls the
effect factory directly instead of re-arming `flag2`. Transient Up/Down slash shells for an
already-replayed window are not reconstructed; only the carried blade, which is the
player-visible loss.

**Expected next soak:** any `effect_eject reason=kirby_finalcutter_blade` with an in-scope
status should be followed by `effect_remint reason=kirby_finalcutter` within a frame — and
with the protect in place, the mid-move ejects themselves should mostly disappear.

The full respawn-class migration (capture the blade like the Captain punch flame) remains
the end-state if this compensating pair ever proves insufficient, but the protect closes
the actual hole and the re-mint covers every other eject source.

---

## Soak 2026-08-30 (dual Kirby): the re-mint is refuted. Reverted.

The vanish fix held — the player confirms the blade stopped disappearing, and 36–38
`effect_remint` events per peer show the recovery working in the simple case. But a match
with **both** players as Kirby, both mid-Final-Cutter (p0 status 256, p1 status 257),
wedged permanently at tick 3851: inputs flowing, frames pumping, sim blocked in
`tick_commit blocked (load_fail_hold)` until manual shutdown, identically on both peers.

The log shows the mechanism:

- Ejects **continued despite the verify protect** — so a second, still-unidentified pass
  destroys blades in the dual-Kirby case (in-scope owners, status 256/257, always
  `gobj_id=1011`).
- Eject and remint chased each other **across players**: `eject player=0` followed by
  `remint player=1` at 3769/3784/3795/3803. With two blades and two Kirbys, owner
  attribution (joint user_data resolution) goes ambiguous, so one player's eject leaves
  the other "owning nothing" with attach set → remint → next pass ejects again.
- `fc_recovery=1` resim episodes repeated with the same `mismatch_tick=3835` and never
  converged; the frame-commit seed diff shows player 1's whole skeleton and
  `anim_frame` (live 26.0 vs blob 11.0) diverged from the ring.

The design error: **hash-excluded is not replay-safe.** The remint runs inside load/verify
passes and creates GObj allocations the ring's forward pass never recorded, and the
attach-preservation kept `is_effect_attach` TRUE on paths the original pass did not take —
and that flag feeds real sim control flow in `ftKirbySpecialHiUpdateEffect` (StopEffect vs
force-clear vs mint) even though it is excluded from the fold. Injected state → replay can
never match the ring → frame-commit never validates → permanent hold.

**Reverted:** the reconcile re-mint and the eject's attach-preservation.
**Kept:** the non-canonical verify protect (edit 1) — it fixes the original single-Kirby
vanish at its measured source and injects nothing.
**Added:** `caller=` (dladdr) + `resim=` on the blade-eject log, so the next dual-Kirby
soak names the unprotected eject path instead of leaving it to inference. If that path can
be protected the same way, the vanish stays fixed in the dual case too — by *not
destroying* the blade, never by re-creating it.

Also noted for follow-up: the wedge exposed that a permanent `load_fail_hold` has no
working escape (`BATTLE_SIM_HOLD` never armed, no watchdog fired for ~15 s until manual
shutdown). Whatever holds live advance indefinitely should eventually trip the existing
hold-escape machinery; that gap is pre-existing and independent of the blade.

---

## Soak 2026-08-30 (caller attribution): the second eject path is named

The `caller=` instrumentation answered the question in one soak:

```
29x resim=0 caller=syNetRbSnapForceClearKirbyFinalCutterBlades+0x121   (linux)
25x resim=0 caller=syNetRbSnapForceClearKirbyFinalCutterBlades+0x20c   (android)
```

The mid-move ejects come from the **decomp side**: the `PORT && SSB64_NETMENU` force-clear
calls in `ftKirbySpecialHiUpdateEffect` (ftkirbyspecialhi.c). The failing sequence:

1. A rollback heal restores `is_effect_attach = FALSE` from a blob captured before the
   blade's mint frame — correct sim state for that tick.
2. The blade shells themselves are NOT restored or destroyed by the load (they are
   excluded from the snapshot), so pre-rollback shells live on.
3. The update-effect path sees attach FALSE with shells live → force-clear, no re-mint
   (that branch exists precisely to tear down orphans). If the replay window has already
   passed the ACMD mint frame, nothing recreates the blade → vanish mid-animation.

**It is no longer only cosmetic.** The same soak shows `RESIM_BASELINE` exchanges where
the peers' fighter hashes disagree while world/rng/map agree (tick 4080: figh
`0x2F4B4A90` vs `0x85099290`, everything else identical) — a fighter-only cross-peer
divergence, consistent with shell-existence differing between peers and driving divergent
force-clears of fighter-attached pointers. The user-observed "divergent heal" resim is
this: fighter-hash baselines disagreeing across peers, downstream of untracked shells.

## Conclusion: do the respawn-class migration

Compensating machinery has now been tried in both directions (re-mint: wedged the match;
protect-only: leaves the decomp's own force-clear path stranding blades and lets shell
state fork the fighter hash). The end-state named at the top of this doc is the actual
fix: give the blade a real snapshot respawn class so shells are captured, restored, and
destroyed in lockstep with the fighter state that governs them — attach flag and shells
can then never disagree, on one peer or between peers. That is a deliberate milestone
(capture predicate exists; needs blob fields for the joint attach and a slot-driven mint
like Captain punch), not a same-day patch — two of those have already been refuted here.

---

## 2026-08-31: the migration is in. Blades are snapshot state.

The dual-Kirby soak (both players entering up-B at 1773/1774) ended the match at
validation 1801 with fighter AND world hashes forked, force-clear churn running through
every resim of the window (`effect_eject ... resim=1 caller=syNetRbSnapForceClearKirbyFinalCutterBlades`).
That was the last argument for compensations. The end-state named at the top of this doc
is now implemented:

- **Capture + fold**: in-scope cutter blades are no longer excluded — a shared predicate
  (`syNetRbSnapJointFxSlotRespawnable`) admits the Captain punch flame and owner-in-scope
  cutter blades through BOTH symmetric filters (`EffectHiddenFromRollback`,
  `LiveEffectExcludedFromRollbackHash`), so the shells are enumerated, captured as
  `USERDATA_JOINT` blobs (fighter identity + joint index), folded into the eff hash, and
  treated as canonical by every verify pass. Shell divergence is now VISIBLE and
  healable instead of silently forking downstream fighter state.
- **Restore + mint**: `syNetRbSnapMakeUserdataJointEffectForFighter` now takes the blob
  and mints the right shell for a Kirby in cutter scope: joints[17] → Draw (the carried
  blade); TopN → Down in AirHiFall, Up otherwise — matching how the ACMD schedules them.
  A mid-life Trail streak re-minting as Draw is a few-frame cosmetic approximation.
- **Out of scope unchanged**: post-Landing orphans and NULL-parent shells stay excluded
  and keep the old teardown path; the reconcile/eject/protect machinery remains as belt-
  and-suspenders until a green dual-Kirby soak justifies trimming it.

**Validation soak**: dual-Kirby up-Bs, ideally overlapping, with the effect diag on.
Expect: no mid-move `effect_eject` with an in-scope owner (shells now reconcile against
the slot), `effect_respawn kind=USERDATA_JOINT` lines minting blades on rollback loads,
no attach/shell ping-pong, and — the real test — no figh/world fork in dual-cutter
windows. If the eff partition throws NEW transient diverges, that is the design working
(divergence surfaced and healed) unless it flaps repeatedly on one tick.

