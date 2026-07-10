# Netplay: Ness `dead_gate` witness skew + Sector Z Arwing deck carry

**Date:** 2026-06-05
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android cross-ISA soak
**Status:** FIX SHIPPED (soak pending) for P1 + P3; P0 + P2 verified already-covered.

This entry covers a log-analysis pass over a Sector Z soak (`netplay-trimmed.log`)
that surfaced four candidate issues. Two were real, well-localized bugs and are
fixed here (P1, P3). Two (P0, P2) were investigated and found to be already
handled correctly by existing infrastructure — documented so future sessions
don't re-chase working guards.

---

## P1 — Ness `dead_gate` witness "corruption" (1-tick skew) — FIXED

### Symptom
During Ness `DeadDown` (ticks ~1592–1637) the witness logged 46×:

```
SSB64 NetStatusVars: corrupt dead_gate ... status_id=0 union_wait=0 dead_gate_wait=45
```

with the exact pattern (union always one decrement *behind* the mirror):

```
1592: union_wait=0  dead_gate_wait=45
1593: union_wait=45 dead_gate_wait=44
1594: union_wait=44 dead_gate_wait=43
```

Same window also showed `ring_save_player full_ok=0` (`live_full ≠ blob_full`,
`anim_ok=1`).

### Root cause
**Not** "two decrement paths" (the original hypothesis). The skew is a witness
**observation artifact** inside `ftCommonDeadSetWait`:

```c
/* before */
fp->dead_gate_wait = (s16)wait;     /* (1) mirror updated to NEW value */
ftStatusVarsDead(fp)->wait = wait;  /* (2) accessor evaluation triggers the
                                     *     statusvars witness, which reads
                                     *     union(OLD) vs mirror(NEW) -> skew=1,
                                     *     THEN writes the union */
```

`ftStatusVarsDead()` calls `ftStatusVarsNoteAccess()` →
`syNetplayStatusVarsWitnessNoteAccess()` → `CheckIntegrity()`, which compares
`fp->status_vars.common.dead.wait` against `fp->dead_gate_wait`. Because the
mirror is written first, the accessor's integrity check sees the union still
holding the previous frame's value while the mirror already advanced — a
guaranteed off-by-one **every** dead-countdown frame. The predicted logged
values (`union[N] == mirror[N-1]`) match the soak log exactly.

The same 1-instruction inconsistency window is what fed the `full_ok=0` ring
divergence: capture forces `blob.dead.wait = dead_gate_wait`, so any moment the
union and mirror disagree skews the captured-vs-live full hash.

### Fix
`decomp/src/ft/ftcommon/ftcommondead.c` — write the **union first**, then the
mirror. The accessor-triggered witness then observes `union(OLD) == mirror(OLD)`
(consistent, since the prior `SetWait` left them equal and `ftCommonDeadCheckRebirth`
clears the mirror to 0 on exit while `PRESERVE_NONE` zeroes the union on entry):

```c
ftStatusVarsDead(fp)->wait = wait;   /* union first; witness sees consistent state */
#if defined(PORT) && defined(SSB64_NETMENU)
fp->dead_gate_wait = (s16)wait;      /* mirror catches up */
#endif
```

No new mirror, no accessor added (honors the FTStatusVars Approach-C contract).
Offline is unaffected (mirror line is `PORT && SSB64_NETMENU`-gated; offline does
a single union write as before).

---

## P3 — Sector Z: living fighter loses Arwing deck carry during opponent rebirth — FIXED

### Symptom
User report: "when Mario jumps on the Arwing it doesn't pull him with it."
Log showed `SYNCTEST_SKIP reason=rebirth` (Ness respawning) immediately followed
by the Arwing deck coupling being skipped — Mario stopped inheriting the deck's
platform velocity whenever the other player was in their rebirth window.

### Root cause
`syNetRbSnapRefreshSectorArwingDeckFighterPlatformCoupling()`
(`port/net/sys/netrollbacksnapshot.c`) bailed out **globally** if *any* fighter
was in rebirth scope:

```c
if (syNetRbSnapshotAnyFighterRebirthScopeActive() != FALSE) { return; }
```

So a grounded, living fighter on deck line 1 never got
`mpCollisionGetSpeedLineID(1, &fp->coll_data.vel_speed)` applied while a *different*
player respawned.

### Fix
Drop the global bail; skip only the rebirthing fighter inside the per-fighter
loop (its pose/coll is canonicalized on its own apply path and must not inherit
live deck velocity, but everyone else still gets carry):

```c
if (syNetRbSnapFighterInRebirthScope(fp) != FALSE) { continue; }
```

`syNetRbSnapshotAnyFighterRebirthScopeActive()` remains in use by the synctest
defer path, so no dead-code warning.

---

## P0 — Orphan shield / particle xf UAF (gobj 1011) — VERIFIED ALREADY COVERED

`effect_xf_stale reason=owner_mismatch` and `guard_shield_prune reason=no_fighter`
recur on the shield bubble, but the original SIGSEGV (resolved 2026-06-05, see
[netplay_effect_xf_uaf_pool_reuse](netplay_effect_xf_uaf_pool_reuse_2026-06-05.md))
does not. Audit confirms the root-cause infrastructure is complete:

- **Pool clear on every handoff:** `lbParticleGetTransform()` clears
  `xf->effect_gobj = NULL` on alloc; `lbParticleEjectTransform()` clears on free
  (`lbparticle.c`).
- **Particle-reset xf strip covers all effects incl. NO_STRUCT:**
  `syNetRbSnapStripEffectXfCouplingAfterParticleReset()` walks both Effect and
  SpecialEffect links and nulls `ep->xf` / `effect_vars.{common,dust_light,dust_heavy}.xf`
  and ends xf func procs, before any apply runs.
- **IsLive guard:** `efManagerNetplayEffectXfIsLive()` validates free-list,
  allocated, `users_num`, status, owner back-ref, and particle link **before**
  any xf write; `DefaultProcUpdate`/dust procs eject (never touch a foreign xf).
- **Orphan teardown:** `syNetRbSnapPruneStaleShields()` ends procs, nulls `ep->xf`,
  clears all fighters' effect pointers, then ejects.

The residual `owner_mismatch`/`no_fighter` lines are these guards **firing as
designed** (transient within-resim pool aliasing + shields outliving a KO'd
owner), not a live UAF. No further code change without runtime evidence of *why*
`effect_vars.shield.player` goes out of range — chasing it blind risks regressing
delicately-tuned reconcile code.

## P2 — Rebirth-halo `eff` hash drift — VERIFIED ALREADY COVERED

`eff live=… snap=…` drift during the rebirth halo window cannot be a hard
synctest fail: `syNetRbSnapshotSynctestShouldSkip()` already returns TRUE for
both `dead` and `rebirth` scopes, so the synctest is skipped entirely while any
fighter is respawning. The halo itself is restored symmetrically on every peer
via `syNetRbSnapEnsureRebirthHalos…` (blob-respawn, synth fallback) and pruned by
`syNetRbSnapPruneStaleRebirthHalos()`. The drift is cosmetic and contained to the
skip window.

---

## Verification
- `cmake --build build --target ssb64 -j 8` (netmenu Debug) — links clean.
- `cmake --build build-offline --target ssb64 -j 8` (offline) — links clean
  (confirms the decomp `ftcommondead.c` change is offline-safe).
- Lint clean on both touched files.

## Soak checklist
- Ness KO on any stage: expect **zero** `corrupt dead_gate` lines and
  `ring_save_player full_ok=1` through the dead countdown.
- Sector Z: with one player respawning, the other standing on the patrolling
  Arwing deck should still be carried by the deck velocity.
