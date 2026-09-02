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

---

## Post-migration soak (dropping animation, resim storm): verify machinery vs the slot

The migration worked mechanically (42 mints from slot blobs on the predicting peer) but a
mint→eject churn appeared: freshly minted in-scope blades destroyed within the same repair
pass, ~25 log lines after their mint, 26 of 37 ejects from callers dladdr printed as `?+0x0`
— the deployed build is optimized and the generic eject paths are inlined, so runtime
symbolization is structurally useless there. The churn dropped the visible blade and fed
the resim storm.

Also in this soak, and distinct: figh-only FC diverges (1165/1173) with the eff partition
CLEAN, where the setstatus trace (armed, `all`) shows **every transition matching across
peers and across live/resim** — while `status_total_tics`, `hold_stick` latch counters,
`vel_air`, and position are all coherently **+2 ticks ahead of the ring blob**. That is not
transition timing: the fighter was stepped two extra times relative to the ring. A
double-step source somewhere in the resim/frontier machinery — new class, needs its own
capture; the blade churn's 89-episode storm is the likely amplifier, so the churn fix
comes first.

Two changes:

1. **Context tags replace dladdr for eject attribution.** Every decider sets
   `sSYNetRbBladeEjectContext` ("generic_eject", "reconcile_out_of_scope",
   "force_clear_sim", "verify_hidden_cosmetic") around its call; the eject log prints
   `ctx=`. Works identically in optimized builds.
2. **Verify-stage refusal.** An owner-in-scope blade may only be destroyed during a
   verify stage or resim by `force_clear_sim` — the decomp-driven teardown that resim
   must replay faithfully. Every other context is pre-migration reconciliation guesswork;
   the eject executor refuses it (`effect_eject_refused` log), and the slot ensure/apply
   owns the shell set.

---

## Stress soak 2026-09-01 (up-B spam + ledge grabs): one blade lost, and we know why

Blade accounting over 2013 ticks of deliberate abuse:

| | linux | android |
|---|---|---|
| in-scope kills REFUSED (`generic_eject`) | 87 | 41 |
| allowed, `force_clear_sim` (the sim's own teardown) | 3 | 2 |
| allowed, other | 1 (`reconcile_out_of_scope`, status 10 -- out of scope, correct) | **1 (`generic_eject`, tick 392, player 1, status 256 -- IN SCOPE)** |
| slot mints | 7 | 3 |

128 attempted kills of in-scope blades were refused. Exactly ONE got through on an in-scope
owner, and by design: the slot-aware refusal allows a cull when the ACTIVE LOAD SLOT lists
no shell for that owner, i.e. the load target predates the blade's mint. That check is what
fixed the pre-mint LOAD_HASH_DRIFT class.

The residual visual cost is the tension that remains: the cull is slot-correct, but if the
replay window that follows never re-crosses the ACMD mint frame, nothing re-creates the
blade and it stays gone for the rest of that move. That is the "animation dropping" still
seen -- now ~1 occurrence per 2000 ticks of up-B spam, against 37 ejects per session before
the refusal existed.

Closing it needs an ACMD-driven re-arm rather than another snapshot heuristic: the
inference route (re-mint from the snapshot layer) already wedged a match on 2026-08-30.

---

## 2026-09-01: refusal slot-consultation REMOVED — third local-state-in-sim-path instance

Session 2026-09-01 (REAL-DELAY, window 3) ended in `PEER_SNAPSHOT_DIVERGE`
(load_tick 1760, both peers) with the blade at the center. The mechanism:

1. `syNetRbSnapBladeEjectRefusalHoldsForOwner` consulted
   `sSYNetRbSnapActiveLoadSlot` — a **peer-local** variable — so whether a live
   in-scope blade survived an eject depended on which slot happened to be
   active on that peer at that moment. Same anti-pattern as the cosmetic-RNG
   replay routing and the stone `unk_0x2` window, third instance.
2. The refusal kept blades alive past their sim deaths; the `id_collision`
   enforce (which **bypasses** the refusal — its tick-1762 eject succeeded
   during active refusals) provided the only mortality, at each peer's own
   load times.
3. Blade lifetimes forked → the blade's interactions forked the **fighters**
   (`figh` mismatch at load 1762/1761/1760, map/world equal) → baseline
   deepening exhausted → session teardown.

**Fix:** the slot consultation is deleted. The refusal is now a pure function
of sim state: owner in cutter scope + context ≠ `force_clear_sim` → refuse,
identically on both peers, live or resim. This also closes the "slot lists no
shell → allow the cull" hole — load-time canonicalization goes through
`slot_effect_enforce`, which never reached this function, so the hole only ever
served the pre-mint case recorded above (~1 cull per 2000 ticks, the residual
blade-drop). Both symptoms should retire together.

`SSB64_NETPLAY_KIRBY_BLADE_TRACE` now defaults ON (=0 disables) — the class
ends sessions and three soaks ran unarmed.

**Open question for the next armed soak:** why slot capture held
`effect_count=1` during dual-blade play (both gobjs carry class id 1011 —
capture-side scoping vs the id-keyed dedup in `slot_effect_enforce` needs the
trace to disambiguate).

---

## 2026-09-01 (later): the baseline was lying — capture omitted out-of-scope blades

First soak with the trace armed answered the open question. FC@843 was
figh-only with `eff` EQUAL; the setstatus trace showed the same transitions on
both peers (one ran 825's `10→256` in a resim, the other live). The structural
finding came from reading the capture path with that in hand:

`syNetRbSnapJointFxSlotRespawnable` required **owner in cutter scope**, and
`EffectHiddenFromRollback` classifies non-respawnable userdata-joint FX as
hidden. So a live blade lingering across the owner's move-end (`257→10`, trail
still fading — exactly this soak's t=819/825 transitions) was **silently
omitted from every slot spanning that tick**. The slot is the baseline; a
baseline missing a real live effect is the precondition for every enforce
mis-kill and pre-mint cull in this file — and it answers the dual-blade
`effect_count=1` mystery: the first Kirby to finish dropped his lingering
blade from all slots.

**Fix (three gates widened in lockstep — capture, fold, mint):**
1. `JointFxSlotRespawnable`: a live blade is capturable regardless of owner
   status. Owner scope remains the *eject-refusal* gate only.
2. `LiveEffectIsKirbyCutterBladeInScope` (the eff-fold carve-out): scope
   requirement removed — otherwise a now-captured out-of-scope blade would
   fall into the generic fold and hash `anim_frame` (the cross-ISA hazard).
   Fold stays the PK-wave treatment (owner status identity), deterministic
   for any owner status.
3. `MakeUserdataJointEffectForFighter`: blade branch keyed on `fkind`
   (Kirby/NKirby) instead of cutter scope, so a load to a lingering-blade
   tick can re-mint what the slot now correctly carries.

Both-peers rebuild required (fold semantics changed).

---

## 2026-09-01 (root cause, PROVEN): both blades share gobj_id 1011

The fold-path columns settled it — and refuted my own hypothesis first:
`bladefold=1` on **every** line of both logs (872/785, zero `bladefold=0`), so
the peers were never taking different fold paths.

The real evidence was in the same tick being captured twice on one peer with
*different* results:

```
linux tick 1360 pass 1:  bladefold=1 own_p=0 own_st=256 own_tt=23   hash=0x61B153EA
linux tick 1360 pass 2:  bladefold=1 own_p=1 own_st=256 own_tt=16   hash=0xBBA05CA5
android      (both):     bladefold=1 own_p=1 own_st=256 own_tt=16   hash=0xBBA05CA5
```

The eff fold described **a different fighter's blade between two passes of the
same tick** — intra-peer nondeterminism. FRAME_COMMIT then compared linux's
first-pass value against android's, and diverged.

**Cause:** both Kirbys' blades carry `gobj_id = 1011`.
`syNetRbSnapEffectGobjIdCollisionAllowsCoexist` had no rule for two
USERDATA_JOINT effects, so they were treated as a recycled-id collision and one
was dropped — from the fold *and* the capture (`reason=gobj_id_duplicate`) —
with the survivor decided by effect-list link order, which changes across a
rollback. This is why `effect_count` never exceeded 1 during dual-blade play,
across every soak in this file.

**Fix:** two USERDATA_JOINT effects coexist. They are not a recycled-id
collision — they are simultaneously live, distinct state. The blob carries
`fighter_gobj_id` and the joint index, and blob↔live matching already
discriminates on owner player + joint, so two blobs sharing a gobj_id
round-trip. Capture and fold share this predicate, so both are fixed
symmetrically at the single chokepoint.

This also reframes the earlier entries: the enforce mis-kills, pre-mint culls,
and lifetime drift were all *downstream* of a baseline that could only ever
hold one of the two live blades. Both-peers rebuild required (fold semantics).

---

## 2026-09-01 (follow-on): coexisting blades broke blob↔live pairing

The coexist fix worked — `count=2` appeared **325 times on both peers**
(identical), `count=3` too, and both `own_p=0` and `own_p=1` blades folded. The
dual-blade blindness is gone and `PEER_SNAPSHOT_DIVERGE` went to 0.

It exposed the next layer: `LOAD_HASH_DRIFT` 8 → 68 (linux) / 46 (android),
`eff=0x8D270B8F/0xB7665C33` at tick 422 with `effect save effect_count=2`
followed immediately by two `slot_effect_enforce_id_collision` ejects. Capture
was right; the *load* could not reproduce it.

Cause: three places resolved a blob to a live effect **by gobj_id**, which is
ambiguous now that two live blades share id 1011.

1. `syNetRbSnapFindLiveUserdataJointEffectForBlob` returned the first identity
   match with no exclusion of shells already claimed by an earlier blob — so
   every blade blob resolved to the *same* shell.
2. `syNetRbSnapEnsureUserdataJointEffectsFromSlot` used `gcFindGObjByID`, which
   returns the first gobj with that id — same shell for both blobs, so the
   second blade never received its pose.
3. The reconcile resolver's USERDATA_JOINT branch guarded with
   `ReconciledEffectGobjIdListed` (**id**) while the quake branch directly above
   it correctly used `reconciled_gobj_ptrs` (**pointer**) — so once blade A
   claimed id 1011, blade B's own shell was rejected as "already reconciled".

With one blob unmatched, `slot_effect_enforce` saw a live effect whose id was
canonical but whose pointer was not, and ejected the real second blade as an
id collision.

**Fix:** `...ForBlobExcept(slot, blob, except, except_count)` (mirroring
`FindLiveQuakeEffectForBlobExcept`); site (2) resolves by blob identity with a
per-pass claim list and falls back to the id lookup only when identity finds
nothing and the id is unclaimed; site (3) switches to pointer exclusion.
Pairing is now one blob ↔ one shell, keyed on owner player + joint.

---

## 2026-09-02: enforce's id-collision test is no longer valid evidence

Soak after the pairing fix — the class keeps improving but is not closed:

| | 2 soaks ago | prev | now |
|---|---|---|---|
| PEER_SNAPSHOT_DIVERGE | 2 / 2 | 0 / 0 | 0 / 0 |
| enforce id_collision ejects | 146 / 102 | — | **20 / 16** |
| FC diverge partition | eff | eff | **figh only (eff EQUAL)** |
| LOAD_HASH_DRIFT | 8 | 68 / 46 | 88 / 48 (901 ticks) |

The eff partition now agrees at frame-commit; both remaining FC diverges are
figh-only. What survives is intra-peer eff drift (capture vs post-load), and it
is squarely a multi-blade problem — drift rate by capture count:

```
count=1:  2/129 = 1.6%
count=2: 14/127 = 11%
count=3:  5/60  = 8.3%
```

Single-blade ticks round-trip exactly (tick 511: capture and verify both
count=1, hash 0x9857F8C8 identical).

**Cause:** `slot_effect_enforce`'s test — *id is canonical but this pointer was
not claimed* — stopped being proof of a stale duplicate the moment coexisting
effects were allowed to share a gobj_id. Both Kirbys' blades are id 1011, so a
legitimately live second blade whose blob merely failed to pair on that pass is
indistinguishable from a stale one, and ejecting it destroys real state.

**Fix:** for coexist-capable joint FX, require positive evidence of staleness —
keep the shell when it still matches a blob in the slot. Genuine extras match no
blob and are ejected exactly as before. This is deliberately the conservative
direction: a missed eject leaves a cosmetic shell alive, a wrong eject destroys
hashed state.

---

## 2026-09-02 (later): the self-diagnosing dump refuted the fix that shipped with it

First soak with `slot_eff_dump` + `drift_live`. The instrumentation paid for
itself immediately — one grep at a drift tick, no inference:

```
slot_eff_dump tag=drift tick=403 effect_count=1
slot_eff_dump tag=drift tick=403 idx=0 valid=1 gobj_id=1011 own_p=1 joint=17 ...
eff_fold_diag tag=drift_live tick=403 count=2 hash=0xAE53D0D7
```

The SLOT held **one** blade (`own_p=1`); the post-load live set held **two**.
That is the opposite of the assumption behind the previous entry's fix. The
surplus shell is not a real second blade whose blob failed to pair — it is a
**stale blade carried back from the frontier with no blob at all**, and the
exemption added that morning was keeping it alive.

Compounding it: `syNetRbSnapLiveEffectMatchesAnyBlobInSlot` does not consider
whether the blob it matched was already claimed, so with one blob and two live
shells **both** shells "matched" and both were exempted.

Capture is authoritative and self-consistent — fold counts and slot saves agree
exactly (36 × count=1, 138 × count=2, 32 × count=3) — and blob↔shell pairing is
fixed (b5c47db4). So an unclaimed live shell whose id is canonical is genuinely
surplus. **Exemption reverted**; the id-collision eject stands.

Method note worth keeping: the two preceding blade commits were reasoned from
capture-side data alone and one of them was wrong in its premise. The dump
turned the next question into a single grep. Instrument before inferring when a
class has already cost more than two speculative fixes.
