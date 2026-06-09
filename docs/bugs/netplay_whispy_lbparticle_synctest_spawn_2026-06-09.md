# Netplay Whispy LBParticle spawn with synctest (2026-06-09)

**Symptom:** Dream Land tree/flower GObj anims OK; Whispy wind leaves + ground dust LBParticles never appear when `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`. Works with synctest off.

**Root cause:** Periodic synctest probe runs `syNetRbSnapResetParticlesForRollback()` while `leaves_xf`/`dust_xf` handles can stay non-NULL. Forward blow spawn then skipped broken-shell detection; new structs were not warmed (`lbParticleWarmupGeneratorID`) so drawable count stayed 0. Verify-only repair also skipped clearing dangling xf pointers.

**Fix:**
- `grpupupu.c`: netplay blow entry uses eject+fresh spawn+warmup for leaves/dust; `UpdateBlow` re-respawns broken xf and warms up when drawable==0.
- `netrollbacksnapshot.c`: verify-only Pupupu repair nulls deallocated xf handles; skip synctest probe on guard→GuardOff boundary (`guard_release_boundary_probe`, tick 1085 class).

**Follow-up (same day):** Logs showed dust generators created but nothing drawable — stale `leaves_xf`/`dust_xf` survived `syNetRbSnapResetParticlesForRollback()` when Whispy was not blowing, and `forward_tick` diag never fired.
- `grPupupuWhispyNullParticleXfHandles()`: unconditional null after every particle reset (Dream Land only).
- Eject helpers guard `lbParticleTransformIsAllocated()` to avoid UAF on stale xfs.
- `UpdateBlow`: 30-tick drawable stall forces fresh spawn + 128-tick warmup; blow diag logs first 5 ticks then every 30; clear particle skip IDs 1/2 on blow entry.
- `syNetRbSnapClearPupupuWhispyStaleParticleXf()`: no longer gated on `whispy_status == Blow`.

**Verify:** Kirby vs Yoshi Dreamland soak with synctest — leaves/dust visible during Whispy blow; `forward_tick` shows `leaves_drawable`/`dust_drawable` > 0; no `SYNCTEST_FAIL` at Kirby guard release ticks.

**Render investigation (2026-06-09 follow-up):** Soak logs with rebuild show sim-side success (`leaves_drawable=4–51`, `dust_drawable=3–4`, `max_size>200`) on both host and guest while user still sees no VFX — spawn/snapshot path is ruled out; remaining failure is in `lbParticleDrawTextures` / effect display GObj capture. Added `render_tick` / `render_draw` / `display_gobj` lines under `SSB64_NETPLAY_WHISPY_REPAIR_DIAG=1` to distinguish mask_pass vs drawn vs culled vs tex_miss.
