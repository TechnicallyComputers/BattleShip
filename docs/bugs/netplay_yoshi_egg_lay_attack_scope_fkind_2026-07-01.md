# Netplay — `yoshi_egg_lay_attack` synctest skip misfires on Fox Firefox charge (missing `fkind` guard)

**Date:** 2026-07-01
**Status:** Fix implemented (`PORT && SSB64_NETMENU`, superseded by synctest un-skip)
**Area:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Soak2 session `1339981196`: `SYNCTEST_SKIP reason=yoshi_egg_lay_attack` fired 146 times across a
match the user confirms had **no Yoshi character and no Kirby copy-ability use** — just Fox and
Kirby, with Fox charging Firefox repeatedly. Cross-referencing the exact skipped ticks (e.g. 510-512)
against `fighter_slot_hash` shows Fox at `status=232` on every one — `nFTFoxStatusSpecialHiHold`
(Firefox charge), not anything Yoshi-related.

## Root cause

Fighter special-move status IDs are **not globally unique**. Every character's status enum
continues from the same shared base:

```c
// decomp/src/ft/ftchar/ftfox/ftfox.h
nFTFoxStatusAttack100Start = nFTCommonStatusSpecialStart,   // +0
...
nFTFoxStatusSpecialHiHold,                                  // +9  (Firefox charge)

// decomp/src/ft/ftchar/ftyoshi/ftyoshi.h
nFTYoshiStatusAppearR = nFTCommonStatusSpecialStart,        // +0
...
nFTYoshiStatusSpecialNCatch,                                // +9  (egg-lay catch)
```

`nFTFoxStatusSpecialHiHold` and `nFTYoshiStatusSpecialNCatch` are both `nFTCommonStatusSpecialStart
+ 9` — numerically identical. `decomp/src/ft/ftmain.c` confirms this by design
(`status_struct_id = status_id - nFTCommonStatusSpecialStart` indexes a *per-fkind* descriptor
table). `syNetRbSnapStatusInYoshiEggLayAttackScope` compared a raw `status_id` against the Yoshi/
Kirby-copy-Yoshi status lists with **no `fkind` check**:

```c
static sb32 syNetRbSnapStatusInYoshiEggLayAttackScope(s32 status_id)
{
	return ((status_id == nFTYoshiStatusSpecialN) || ... );
}
```

so any fighter landing on the same local offset as one of those Yoshi/Kirby-copy statuses (Fox's
Firefox charge, offset +9, being the concrete collision found) gets misclassified as
`yoshi_egg_lay_attack` regardless of actual `fkind`. `syNetRbSnapFighterInYoshiEggLayScope`
(the *victim* side — `nFTCommonStatusCaptureYoshi`/`nFTCommonStatusYoshiEgg`) is unaffected: those
are genuinely global `nFTCommonStatus*` values, not per-fighter-local offsets.

This is a purely coincidental miswiring, not a masked desync — Firefox is otherwise stable this
soak (no `SYNCTEST_FAIL`, no `FRAME_COMMIT_*` diverge). But it means every Firefox charge got an
extra, mislabeled synctest skip layered on top of whatever else runs during that window, silently
losing verification coverage the same way the (now-removed) `fox_firefox` blanket skip once did.

## Fix

`syNetRbSnapStatusInYoshiEggLayAttackScope` now takes `fkind` and only matches the Yoshi status
list when `fkind == nFTKindYoshi`, and the Kirby-copy-Yoshi status list when
`fkind == nFTKindKirby` (Kirby's copy-ability statuses are Kirby-local enum values, so they need the
same kind of guard for the same reason). Both callers (`syNetRbSnapFighterInYoshiEggLayAttackScope`
via `FTStruct::fkind`, `syNetRbSnapBlobInYoshiEggLayAttackScope` via
`SYNetRbSnapFighterBlob::fkind`) updated to pass it through.

## Verify

Built clean (`cmake --build build --target ssb64`). Re-soak with Fox charging Firefox repeatedly and
no Yoshi/Kirby-copy-Yoshi in the match: `yoshi_egg_lay_attack` should no longer appear in
`netplay-scan-drift.py`'s skip-reason breakdown. Worth auditing sibling scope checks in this file for
the same missing-`fkind` shape while here.
