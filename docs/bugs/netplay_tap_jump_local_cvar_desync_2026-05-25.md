# Netplay tap-jump local CVar desync (Mario WalkMiddle/Run → KneeBend fork)

**Slug:** `netplay_tap_jump_local_cvar_desync_2026-05-25`
**Status:** FIX SHIPPED (soak pending)
**Severity:** Critical — deterministic-sim divergence; FC hashes blind to it

## Symptom

Sessions 4 and 5 of the v16 soak. Both peers run forward without rollback; every `FRAME_COMMIT` checkpoint up to `FC@480` passes; inputs replicate byte-perfect; fighter-slot hashes (`fhash_light`/`fhash_full`/`anim_hash`) match — yet at a Mario-controls-port-0 ground-locomotion tick, the *host* transitions to `nFTCommonStatusKneeBend` (jump squat) while the *guest* stays in walk/run. From there the figh/cam/anim/eff hashes diverge each tick, FC@600 fails, and the post-fail load@480 recovery hits `LOAD_HASH_DRIFT`.

| Session | First peer fork tick | Mario (host)              | Mario (guest)             |
|---------|----------------------|---------------------------|---------------------------|
| 4       | 519                  | Run (16) → KneeBend (20)  | Run (16) → stayed Run     |
| 5       | 577                  | WalkMiddle (12) → KneeBend (20) | WalkMiddle (12) → WalkFast (13) |

Both cases the host's Mario jumped; the guest's didn't. Both cases the stick was tilting upward across the same threshold range on both peers from identical replicated inputs.

## What the logs proved (and didn't)

`fork_sim_row tick=577` on both peers shows P0 `pub_btn=0x0000 sx=69 sy=66`, P1 `pub_btn=0x2000 sx=0 sy=0`, `used_pred_remote=0`, host's `local_delayed` value matches guest's `conf`/`wire_ring` value byte-for-byte. **No input divergence.**

`fighter_slot_hash tick=576` matches byte-for-byte on both peers (`fhash_light=0x7328D545`, `fhash_full=0x1DEEFCF7`, `anim_hash=0xD9349879`). **No hashed-state divergence at start of the diverging tick.**

`sim_state_tick` every tick from match start to 577 logs `rb_applied=0 rb_load_fail=0`. **No rollback or recovery before the fork.**

Identical hashed state + identical input + pure forward sim → different `status_id`. By definition there is a scalar the fighter sim reads that the fhash doesn't cover, **or** a non-deterministic code path inside the sim.

## Root cause

`ftcommon/ftcommonkneebend.c:94,140` and `ftcommon/ftcommonjumpaerial.c:271` call:

```c
sb32 tap_jump_disabled = port_enhancement_tap_jump_disabled(fp->player);
```

That helper (`port/enhancements/TapJump.cpp`) reads a local CVar per fighter port:

```cpp
constexpr const char* kTapJumpCVars[PORT_ENHANCEMENT_MAX_PLAYERS] = {
    "gEnhancements.TapJumpDisabled.P1", ..., ".P4"
};
return CVarGetInteger(kTapJumpCVars[playerIndex], 0) != 0;
```

The CVars are **per-peer**, never replicated. If host had `TapJumpDisabled.P1 = 0` (default: tap-jump enabled) and guest had `TapJumpDisabled.P1 = 1` (user toggled it off on their copy), the KneeBend interrupt check inside the locomotion action functions sees a different boolean on each peer:

```c
// ftcommonkneebend.c:99 — ftCommonKneeBendGetInputTypeCommon
if (!tap_jump_disabled && (fp->input.pl.stick_range.y >= FTCOMMON_KNEEBEND_STICK_RANGE_MIN)
                       && (fp->tap_stick_y <= FTCOMMON_KNEEBEND_BUFFER_TICS_MAX)) {
    return FTCOMMON_KNEEBEND_INPUT_TYPE_STICK;  // host fires this
}
// guest with tap_jump_disabled=TRUE skips the whole branch
```

Identical `stick_range.y` (replicated input) + identical `tap_stick_y` (deterministic accumulator) but different `tap_jump_disabled` → different status transition → silent sim fork.

This is the same code path on both peers reading a per-process unreplicated configuration in the middle of the deterministic step. Sessions 4 and 5 reproduced the same bug because both runs had the same TapJump CVar mismatch between the two machines.

## Why fhash didn't catch it sooner

`syNetSyncHashFighterStructLight` (`port/net/sys/netsync.c`) folds `status_id`, `motion_id`, six physics velocities, hitlag/status timers, TopN joint translate, and `coll_data.pos_prev`. It did **not** fold:

- `tap_stick_x/y`, `hold_stick_x/y` — the tap-jump / dash detection counters read in this exact code path
- `coll_data.pos_diff` and `*coll_data.p_translate` — current world position (only `pos_prev`)
- `anim_vel.x/y/z` — animation-induced velocity added to physics

If any of those drift between peers (for any reason — this bug, a future bug, FP noise) the fighter sim reads divergent state but the FC checkpoint sees byte-identical hashes and reports no problem.

## Fix

### 1. Stop reading the local CVar during a netplay VS session (root cause)

`port/enhancements/TapJump.cpp`:

```cpp
extern "C" int syNetPeerIsVSSessionActive(void); // forward decl

extern "C" int port_enhancement_tap_jump_disabled(int playerIndex) {
    if (playerIndex < 0 || playerIndex >= PORT_ENHANCEMENT_MAX_PLAYERS) return 0;
    if (syNetPeerIsVSSessionActive()) return 0;   // baseline N64 behavior, all peers agree
    return CVarGetInteger(kTapJumpCVars[playerIndex], 0) != 0;
}
```

This locks tap-jump to enabled (the N64 baseline = `disabled == 0`) whenever `syNetPeerIsVSSessionActive()` returns TRUE. Offline / 1P / local VS still honor the user's preference. A future replicated-handshake approach can override this if we ever want per-match per-player tap-jump toggles in netplay.

### 2. Widen `fhash_light` so the next instance of this class is loud

`port/net/sys/netsync.c::syNetSyncHashFighterStructLight` now also folds:

- `tap_stick_x`, `tap_stick_y`, `hold_stick_x`, `hold_stick_y`
- `coll_data.pos_diff.{x,y,z}`
- `*coll_data.p_translate` (when non-NULL — the live world position)
- `anim_vel.{x,y,z}`

Cost: ~10 extra FNV folds per fighter per tick. Benefit: any future scalar drift in these fields is immediately visible at the next FC checkpoint instead of silently forking the sim until a downstream effect (status, cam) catches up.

## Verification plan

1. Replay session 5 (`v16` build + same `gEnhancements.TapJumpDisabled.P1` mismatch between peers): expect no fork at tick 577, no `LOAD_HASH_DRIFT` at session end.
2. Confirm tap-jump still toggles correctly in solo / 1P / offline VS via the CVar UI.
3. Soak a fresh netplay match for ≥10 minutes with at least one peer having `TapJumpDisabled.P1 = 1` set: expect no FC mismatches arising from a Mario locomotion-to-jump transition.

## Audit hook

Any code path inside the deterministic sim (decomp `src/ft/`, `src/it/`, `src/wp/`, `src/mp/`, `src/gm/`) that calls a `port_enhancement_*` helper or `CVarGet*` is a latent netplay desync. Greppable candidates today:

```
decomp/src/ft/ftcommon/ftcommonkneebend.c:94,135
decomp/src/ft/ftcommon/ftcommonjumpaerial.c:271
```

— all three are tap-jump and now hit the same gate.

Display-only enhancements (`port_enhancement_hitbox_display_override`) are safe because they only set `display_mode` (rendering), not sim state.

Match-setup-only enhancements (`port_enhancement_cpu_level_9`, `..._neutral_spawns`, `..._skip_results_screen`, `..._boot_to_vs_css`) execute outside the sim tick, so per-peer disagreement is benign for the per-tick FC. They still need to be replicated for parity of *match content* — that's a separate concern from this bug class.

Controller-input enhancements (`..._analog_remap`, `..._c_stick_smash`, `..._dpad_jump`) run in `syControllerUpdateGlobalData` *before* the netplay input snapshot, so the transformed value is what's serialized over the wire — both peers receive the host's already-transformed P1 / their own transformed local. Safe.

## Related

- `netplay_desync_bisect_session4_2026-05-25.md` — session 4 bisect, first identified the symptom but mis-attributed to "an unhashed scalar" without finding the source.
- `netrollback_camera_restore_resim_2026-05-25.md` — same-session symptom of recovery failure (camera reload), separate fix.
- The fhash widening here is the same general idea as `fighter_snapshot_fidelity_2026-05-21.md`'s field-diff oracle, applied at the FC hash layer.
