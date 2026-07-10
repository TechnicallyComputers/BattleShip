# Netplay: grab/throw release-boundary effect verify drift — 2026-06-27

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Scope:** PORT netmenu rollback synctest probe-skip / resim load-anchor selection
**Slug:** `netplay_grab_throw_release_eff_drift_2026-06-27`

## Symptom

`scripts/netplay-scan-drift.py` on a host/guest soak pair (android host, linux guest)
returned `FAIL` on a single tick:

```
[FAIL] tick 2857: diverged=eff -> UNRESOLVED (no follow-up) [synctest probe]
synctest: 22 OK, 1 FAIL ... SYNCTEST_FAIL tick 2857
drift ticks on BOTH peers: 389, 2857
```

(Tick 389 is a separate, benign `cam`-only `presentational-only` WARN — see
[netrollback_sector_map_gobj_flags](netrollback_sector_map_gobj_flags_2026-06-27.md).)

The drift line itself is `eff`-only; every other partition round-trips exactly:

```
LOAD_HASH_DRIFT tick=2857 figh=.../.. world=.../.. item=.../.. wpn=.../.. map=.../..
                rng=.../.. cam=.../.. anim=.../.. eff=0xBBBC6734/0x811C9DC5
```

- Snapshot captured a non-empty effect fold (`0xBBBC6734`).
- Live re-hash after the verify load = `0x811C9DC5` = the FNV-empty seed (same
  empty sentinel documented in `netplay_dk_jungle_effect_pop_desync_2026-05-25`
  and the Phase 38 Class A note in `netplay_quake_effect_verify_drift_2026-06-07`).
- **Both peers produce bit-identical `snap`/`live` hashes** — this is *not* a
  peer-to-peer desync. It is an internal save→emergency-restore→reload→rehash
  fidelity hole that reproduces deterministically on both ISAs.

## Why the existing fragile-probe scopes missed it

The synctest scheduler probes `completed_tick - 1`. The log shows two adjacent
invocations around the grab:

- `completed=2857`: `SYNCTEST_SKIP reason=grab_coupling_probe probe=2856` — slot
  2856's fighters were still in Catch/Throw, correctly skipped.
- `completed=2858`: probes 2857. By 2857 the grab had **released** — fighters had
  left the Catch/Throw status range and `catch_gobj_id`/`capture_gobj_id` were 0,
  so `syNetRbSnapBlobInGrabThrowSynctestFragileScope` returned FALSE and
  `grab_coupling_probe` no longer fired. The verify ran → `eff` drift →
  `SYNCTEST_FAIL tick=2857`.

The other two effect scopes also miss this tick:

- `effect_count_transition_probe` only fires when `slot->effect_count !=
  prev->effect_count`. The lingering grab/throw VFX is present and **stable**
  across 2856→2857, so the count does not change.
- the former `transient_effect_probe` only fired when *every* captured effect
  was `respawn_kind=NONE`. The grab VFX coexists with a respawnable effect
  (e.g. a shield bubble), so the transient-only condition did not apply.

The residual grab/throw spark cannot survive the emergency→slot verify load, so
the live fold collapses to the empty seed even though `gobj_link_audit` still
reports `ef6=6` link-6 GObjs (the resim re-spawns fresh shells the fold does not
count).

## Fix

Add a release-boundary fragile probe, modeled exactly on the existing
`syNetRbSnapshotSynctestProbePupupuWhispyPostBlowFragile` "first tick after X
ends" precedent:

`port/net/sys/netrollbacksnapshot.c::syNetRbSnapshotSynctestProbeGrabThrowReleaseBoundaryFragile(probe_tick)`
returns TRUE when:

1. the probe slot has effects (`effect_count > 0`),
2. **no** fighter in the probe slot is grab/throw-coupled (otherwise
   `grab_coupling_probe` already owns the tick), and
3. **some** fighter in the previous slot *was* grab/throw-coupled.

Wired into `syNetRbSnapshotSynctestShouldSkipProbeTick` immediately after the
`grab_coupling_probe` block with reason `grab_coupling_release_boundary_probe`.

Because `syNetRbSnapshotIsLoadAnchorFragile` reuses `ShouldSkipProbeTick`, this
also makes real rollback resim **walk its load anchor back** off the grab-release
boundary instead of anchoring on a tick whose grab VFX can't be reconstructed —
so the fix is not merely silencing a diagnostic; it improves anchor selection.

This does not touch offline (it lives inside the `#ifdef PORT` region of a
netmenu-only TU) and does not alter gameplay determinism: both peers already
agreed at this tick, so no live behavior changes.

## Verify

1. Re-run `scripts/netplay-scan-drift.py --label host ... --label guest ...` on a
   fresh grab-heavy soak — expect tick 2857 (and the grab-release class generally)
   to appear in the skip histogram as `grab_coupling_release_boundary_probe`
   instead of `SYNCTEST_FAIL`, and `RESULT: PASS`.
2. Confirm grabs/throws still play and resolve normally in forward netplay.
3. Watch for `RESIM_ANCHOR_FRAGILE_WALKBACK ... reason=grab_coupling_release_boundary_probe`
   during real rollbacks over a grab — the anchor should resolve cleanly.

## Follow-up (not done here)

The lower-priority alternative is a proper respawn adopter for the grab/throw
spark so the effect actually round-trips (mirroring the quake / shield /
userdata-joint adopters in `netplay_quake_effect_verify_drift_2026-06-07`). Skip
is the in-framework, low-risk mitigation consistent with the repeated treatment
of the `0x811C9DC5` empty-eff symptom; promote to a respawn adopter only if a
grab VFX is ever shown to be gameplay-relevant.

## Audit hook

An `eff`-only `LOAD_HASH_DRIFT` with **identical peer hashes** where live folds to
`0x811C9DC5` (FNV empty) = a captured effect that cannot round-trip the verify
load. If it sits one tick after a fighter-state window ends (grab release, guard
release, Whispy blow end…), the cheap fix is a "first tick after X" boundary
probe-skip; the expensive fix is a per-kind respawn adopter.
