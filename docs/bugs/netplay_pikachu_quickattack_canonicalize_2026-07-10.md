# Netplay: Pikachu Quick Attack sim-state canonicalize harden

**Date:** 2026-07-10  
**Scope:** `PORT && SSB64_NETMENU`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom class

Quick Attack (Up+B) already had `syNetplayCanonicalizePikachuQuickAttackSimState`, but physics /
MPColl / TopN were only grid-snapped when `floor_flags & MAP_VERTEX_COLL_PASS`. Zip travel and
Start→Zip catch-up could leave cross-ISA vel/pose forks (same class as Ness jibaku before full
sim-state canonicalize). Synctest no longer defers QA ticks, so residual `figh`/`anim` drift is
visible.

## Fix

`port/net/sys/netplay_sim_quantize.c` — `syNetplayCanonicalizePikachuQuickAttackSimState`:

- Always quantize fighter physics, MPColl, and TopN/root translate in QA + QA FallSpecial scope.
- During zip: quantize all joint translate+rotate; snap base-joint pitch and force vanilla zip
  scale constants onto the shared grid.
- Status-var backups (`vel_*_bak`, stick_range) unchanged via existing
  `syNetplayQuantizePikachuQuickAttackStatusVars`.

`port/net/sys/netrollbacksnapshot.c` — after gated QA catch-up on fighter apply, call
`syNetplayCanonicalizePikachuQuickAttackSimState` (Ness jibaku apply pattern).

Also: scrub early-return for Pikachu Thunder SpecialLw statuses so `speciallw.thunder_gobj` is not
zeroed by attackair/dead/rebirth memset (QA / Ness PK Thunder pattern).

## Test plan

- [ ] Re-soak Pikachu vs any with synctest + `FORCE_MISMATCH` mid Quick Attack zip/start.
- [ ] No `figh`-only FC diverge / `LOAD_HASH_DRIFT` on QA travel ticks attributable to TopN/vel.
- [ ] Control: QA still transitions Start→Zip→End under resim catch-up gate.
