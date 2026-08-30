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

