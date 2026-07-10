# Fox Firefox `decelerate_wait`/`pass_timer` stomped by `speciallw.effect_gobj` union alias (LP64)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom

Soak session `2074030264` (Fox Firefox): `RESULT: FAIL` with `SYNCTEST_FAIL` on
**both peers** at ticks 1229 and 1349, each an `figh` `LOAD_HASH_DRIFT` synctest
probe (guest also `eff` at 1349, a separate quake concern). Cross-peer sim was
otherwise deterministic — the pair `sim_state` matched every tick except the
injected `FORCE_MISMATCH`. Reporter also saw "animation of flames during
firefox" and Fox's Firefox deceleration/velocity misbehaving after rollbacks.

The scan localized the diverging fighter field precisely and identically at both
ticks:

```
fighter_field_diff tag=load_drift tick=1229 player=1 field=fox_decelerate_wait live=0x00000000 blob=0x0000001D
fighter_field_diff tag=load_drift tick=1349 player=1 field=fox_decelerate_wait live=0x00000000 blob=0x0000000B
```

Capture was correct (`FOX_FIREFOX_ANIM_PROBE ... blob_decel=29`), and the four
in-`ApplyFighter` probes all showed the value restored correctly
(`apply_live_post_status_memcpy ... live_decel=29` … `apply_live_post_netplay
... live_decel=29`). But by the verify hash the live value was `0` — with
`anim_frames` (offset 12) and `angle` (offset 8) **preserved** while
`decelerate_wait` (offset 16) and `pass_timer` (offset 20) were **both zeroed**.
That 8-byte hole at union offset 16 is the signature of an LP64 pointer write.

## Root cause

`decelerate_wait` gates deterministic Firefox physics — once it reaches
`FTFOX_FIREFOX_DECELERATE_DELAY` the move applies ground friction / air velocity
deceleration (`ftFoxSpecialHiProcPhysics` / `ftFoxSpecialAirHiProcPhysics`). It
was added to the rollback fighter hash so save/load must round-trip it.

`FTFoxStatusVars` is a union of `specialhi` and `speciallw`:

```
ftFoxSpecialHiStatusVars: launch_delay@0 gravity_delay@4 angle@8 anim_frames@12 decelerate_wait@16 pass_timer@20
ftFoxSpecialLwStatusVars: release_lag@0 turn_tics@4 is_release@8 [pad] effect_gobj@16 (8 bytes) gravity_delay@24
```

On N64 the `GObj*` is 4 bytes and lands at offset 12 (aliasing only
`anim_frames` — the earlier travel-truncation concern). On **LP64 the pointer is
8 bytes and 8-byte aligned**, so it sits at **offset 16–23**, exactly aliasing
`specialhi.decelerate_wait` (16) + `specialhi.pass_timer` (20).

`syNetRbSnapRebindFighterEffectGobjs` (run for every fighter during snapshot
apply / verify finalize) wrote the reflector pointer **unconditionally**:

```c
reflector_gobj = syNetRbSnapResolveCoupledEffectGobj(syNetRbSnapFoxSpecialLwEffectIdFromBlob(blob));
if ((reflector_gobj == NULL) && (syNetRbSnapBlobFoxInReflectorScope(blob) != FALSE)) { ... }
fp->status_vars.fox.speciallw.effect_gobj = reflector_gobj;   /* ungated */
```

For a Fox mid-Firefox (`specialhi`) fighter — and for any non-Fox fighter, whose
union those bytes belong to — the blob has no reflector id (`fox_speciallw_effect_gobj_id`
is only captured in SpecialLw scope) and the fighter is not in reflector scope,
so `reflector_gobj` is `NULL`. Writing that 8-byte NULL zeroed `decelerate_wait`
and `pass_timer`.

Because both peers apply snapshots the same way, the corruption is
**deterministic across peers** (so the cross-peer frame-commit check passes) but
makes save→load **non-idempotent** — which is exactly what the synctest self-check
detects. It also corrupts Fox's post-rollback Firefox deceleration/velocity on
both clients (the observed flame/physics glitch).

## Fix

Gate the reflector-pointer resolve + write on `syNetRbSnapBlobFoxInReflectorScope(blob)`,
mirroring the capture-side gate (which only records `fox_speciallw_effect_gobj_id`
in Fox SpecialLw scope). Outside reflector scope the union belongs to another
overlay and must be left untouched. This is `netrollbacksnapshot.c`
`syNetRbSnapRebindFighterEffectGobjs`. The two other writers of
`fox.speciallw.effect_gobj` (`syNetRbSnapClearCoupledGObjPointersInStatusPassive`,
`syNetRbSnapClearFighterEffectPointerIfMatch`) were already reflector-scope
gated.

## Audit hook

Cross-peer-deterministic `SYNCTEST_FAIL` `figh` `LOAD_HASH_DRIFT` where a
character's per-status `status_vars` field diverges `live=0` / `blob=<real>` in
an 8-byte-aligned window, while adjacent lower-offset fields survive, = an LP64
`GObj*` in the same overlay union being written where an N64 4-byte pointer only
overlapped an earlier field. Verify every write to a `status_vars.<char>.<move>.<gobj>`
pointer is gated to the exact scope that owns that overlay.
