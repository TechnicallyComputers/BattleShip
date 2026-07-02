# ShieldPose figatree files misclassified — permanent joint-token corruption on Guard/GuardOff

**Date:** 2026-07-01
**Status:** ✅ **FIX IMPLEMENTED** (`lbreloc_bridge.cpp`, all builds — not netplay-only), soak pending
**Area:** `port/bridge/lbreloc_bridge.cpp` (`portRelocIsFighterFigatreeFile`)

## Symptom

`soak2-linux.log`: recurring `RelocPointerTable: invalid/stale token` errors
(`caller=decomp/src/lb/lbcommon.c:894`) whenever a fighter shields, plus user-visible
corruption: the shield bubble renders clipped/off, and — most strikingly — **Kirby's
model joints spin/permanently corrupt after he shields**.

Example error:

```
[error] RelocPointerTable: invalid/stale token 0x000040A0 (token_gen=0x000 slot_gen=0x000
index=16544 max=13715, caller=.../decomp/src/lb/lbcommon.c:894, miss_count=1)
```

Decoded: `token_gen=0` (always rejected by `decodeToken()`) and `index=16544 > max=13715`
(never registered this session) — this is not a stale-but-once-valid token, it's raw,
never-tokenized data sitting in a slot `lbCommonAddFighterPartsFigatree` unconditionally
tries to resolve as a token.

## Diagnosis

Traced Kirby's (`fkind=8`) figatree-bind trail in `soak2-linux.log` for a specific
per-fighter joint table (`root=0x7f34b7181df8`, backing his Guard/GuardOff motion). It was
stable at `walked=24 bound=18` for 7 consecutive binds, then **permanently dropped to
`bound=16`** (2 joints silently and permanently lose their animation binding) right at a
rollback verify/restore pass that also restored Kirby's status `154→10` (GuardOff exiting
to Wait):

```
figatree-bind fkind=8 walked=24 bound=18 root=0x7f34b7181df8 ...   (x7, stable)
  ... rollback verify/restore: A_apply_status_restore -> apply_after -> B_apply_fighter_end ...
figatree-bind fkind=8 walked=24 bound=16 root=0x7f34b7181df8 ...   (permanent from here on)
```

Added instrumentation to `lbCommonAddFighterPartsFigatree` (figatree pointer address +
digest of the walked words, plus `motion_id`/`status_id`/`player`) to distinguish "wrong
source pointer" from "same pointer, mutated bytes" — see the diagnostic block added in this
change for future bisection if a similar corruption resurfaces elsewhere.

### Root cause

`ftMainSetStatus`/`ftMainRefreshFigatreeVisual` (`decomp/src/ft/ftmain.c`) resolve a
fighter's figatree two ways:

```c
if (motion_desc->anim_desc.flags.is_use_shieldpose)
{
    fp->figatree = (void*)((intptr_t)motion_desc->anim_file_id + (uintptr_t)fp->data->p_file_shieldpose);
}
else if (motion_desc->anim_file_id != 0)
{
    lbRelocGetForceExternHeapFile(motion_desc->anim_file_id, (void*)fp->figatree_heap);
    fp->figatree = fp->figatree_heap;
}
```

Both results are handed to `lbCommonAddFighterPartsFigatree` (`decomp/src/lb/lbcommon.c`)
identically — it's the same figatree-format per-joint animation-token array either way. The
non-shieldpose path loads through `reloc_animations/FT*`, which
`portRelocIsFighterFigatreeFile()` (`port/bridge/lbreloc_bridge.cpp`) correctly recognizes.
The shieldpose path reads directly from each character's persistent `*ShieldPose` intern
file (`reloc_fighters_main/KirbyShieldPose`, `FoxShieldPose`, …, registered via
`ftManagerSetupFilesKind` → `lbRelocGetStatusBufferFile`) — a completely different path
prefix that `portRelocIsFighterFigatreeFile()` never checked for:

```cpp
static bool portRelocIsFighterFigatreeFile(u32 file_id)
{
	static const char sFighterAnimPrefix[] = "reloc_animations/FT";
	static const char sFighterSubmotionPrefix[] = "reloc_submotions/FT";
	static const char sSCExplainMainPath[] = "reloc_scene/SCExplainMain";
	...  // no ShieldPose check
}
```

Consequence when a `*ShieldPose` file loads through `portRelocLoadFileFromBytes`:

1. `figatree_reloc_words` is never populated (`is_fighter_figatree` false), so
   `portRelocFixupFighterFigatree`'s u16-halfswap fixup for non-pointer figatree words
   never runs on it.
2. `port_aobj_register_halfswapped_range` is never called for the file's memory range, so
   the `AObjEvent32` byte-order walker doesn't know this heap needs the same
   native/halfswapped bookkeeping as a genuine figatree file.

Either gap can leave some of a `*ShieldPose` file's non-chain joint words in the wrong byte
order or misclassified. `lbCommonAddFighterPartsFigatree` then reads one of those words as a
raw (never-relocated-as-a-pointer) value and hands it to `PORT_RESOLVE`, which correctly
rejects it (`gen=0`, out-of-range index) — the joint falls back to `AOBJ_ANIM_NULL`
permanently, since `*ShieldPose` is a persistent intern-buffer file (loaded once for the
life of the match) and nothing ever re-derives it. This is the same general hazard class as
[netplay_aobj_restore_double_unhalfswap_resim](netplay_aobj_restore_double_unhalfswap_resim_2026-06-27.md)
(shared/interned figatree-family data being mishandled around byte-order), but via a
different, previously-unaudited code path — file **classification**, not a rollback-restore
force-pass — and it affects **every character's Guard/GuardOff shieldpose motions**, not
just Kirby's; Kirby's specific data pattern happened to be the one that visibly manifested
in this soak.

Note this is **not netplay-specific** — `portRelocIsFighterFigatreeFile` runs for every
build (offline included) since it gates the load-time fixup for any `*ShieldPose` file.

## Fix

Recognize `*ShieldPose` files as figatree files in `portRelocIsFighterFigatreeFile`
(`port/bridge/lbreloc_bridge.cpp`), via a suffix check (`reloc_fighters_main/` holds many
non-figatree files too — `*Main`, `*MainMotion`, `*Special1-4` are motion-descriptor/status
tables, not figatree-format payloads, so a prefix-only match would be too broad):

```c
static bool portRelocIsFighterShieldPoseFile(const char *path)
{
	static const char sShieldPoseSuffix[] = "ShieldPose";
	...
	return std::strcmp(path + (path_len - suffix_len), sShieldPoseSuffix) == 0;
}
```

OR'd into `portRelocIsFighterFigatreeFile`'s existing checks. This gives `*ShieldPose`
files the same halfswap fixup + `AObjEvent32` range registration that
`reloc_animations/FT*` files already get, closing the gap regardless of which of the two
omitted behaviors was the actual corruptor.

Also added (diagnostic, `#ifdef PORT`, always-on like the existing figatree-bind log):
`lbCommonAddFighterPartsFigatree` now logs `player`/`motion`/`status`/`table_ptr`/
`table_digest` alongside the existing `walked`/`bound` counts, so any future occurrence of
"`bound` drops for a given `root`" can be bisected immediately: `table_ptr` changing means a
mis-selected source view (stale `motion_id`/`status_id`); `table_ptr` identical with
`table_digest` changing means the shared bytes were mutated in place.

## Verify

Built clean (`build-netmenu`, `ssb64` target). Re-soak the dual-shield-spam repro (Kirby
especially) with this build: expect `RelocPointerTable: invalid/stale token` from
`lbcommon.c:894` to stop appearing, and Kirby's figatree-bind `bound` count to stay stable
across Guard/GuardOff cycles instead of permanently dropping. If a similar drop still
occurs, the new `table_ptr`/`table_digest` fields in the `figatree-bind` log line will show
whether it's a different-source-pointer bug or genuine in-place corruption of the (now
correctly classified) ShieldPose heap.

## Related

- [netplay_aobj_restore_double_unhalfswap_resim](netplay_aobj_restore_double_unhalfswap_resim_2026-06-27.md) — same "shared/interned figatree-family data mishandled around byte-order" hazard class, different code path (rollback restore force-pass vs. load-time classification).
- [netplay_guard_shield_sentinel_relink](netplay_guard_shield_sentinel_relink_2026-07-01.md) — the shield-spam watchdog hang fixed in the same soak session; unrelated mechanism (GObj linked-list double-unlink), same repro (dual shield spam).
- [netplay_guard_shield_attach_refresh_diag](netplay_guard_shield_attach_refresh_diag_2026-07-01.md) — separate, still-unresolved shield-bubble visual clipping issue; not a figatree/joint corruption bug.
