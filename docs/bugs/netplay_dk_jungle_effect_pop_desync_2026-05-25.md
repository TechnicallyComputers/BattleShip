# Netplay DK Jungle effect-population desync — cosmetic-RNG-gate root cause

**Slug:** `netplay_dk_jungle_effect_pop_desync_2026-05-25`
**Status:** FIX SHIPPED (soak pending)
**Severity:** Critical — silent forward-sim divergence; recovery cannot heal it.

## Symptom

WAN session 2026-05-25 (two matches in one log pair):

| Match | Stage | Outcome |
|-------|-------|---------|
| 1     | `stage=7` Saffron City | Clean: 10713 ticks, 0 rollbacks, 0 hash drift. |
| 2     | `stage=2` **Kongo / DK Jungle** | `LOAD_HASH_DRIFT tick=1920`; `stopping VS session`. |

Match 2 progression (host log around `LOAD_HASH_DRIFT`):

```
FRAME_COMMIT_STATE_DIVERGE validation=2040
  local  figh=0xB7A38201 world=0x27DE4270 rng=0x80AC1889
  peer   figh=0x694649E9 world=0x27DE4270 rng=0x80AC1889
  inp_local=0xED9B07AE  inp_peer=0xED9B07AE    (inputs and world agree)

FRAME_COMMIT_INPUT_AGREE_REANCHOR validation=2040 last_agreed=1920 mismatch=1921 resolved_load=1920

LOAD_HASH_DRIFT tick=1920
  figh=0x4F2D9A90/0x53582B0F    world=0x155B6936 (match)    rng=0xEDA3539D (match)
  map=0x818CA08E (match)        cam=0x6DF88B57 (match)     anim=0x391046A9 (match)
  eff=0x3237D1F2 / 0xE7080B17 / loaded=0x811C9DC5 (FNV empty)

LOAD_HASH_DRIFT — restoring live world and stopping VS session (tick 1920)
```

Notes:

- Inputs, RNG, world, map, camera, animation all agreed peer-to-peer for the entire match.
- **`eff` partition hash captured at tick 1920** was different on each peer (`0x3237D1F2` host / `0xE7080B17` guest). Live re-hash post-load was the FNV empty seed (no live effects survived) — confirming the snapshot apply path could not rebuild the drifted effect population.
- Net effect-GObj alloc imbalance over Match 2: **host 359 / guest 354** (5-GObj asymmetry over 2040 ticks).
- A bisect using `sim_state_tick` showed `gch` (GObj traversal hash) first diverged at tick 465, then in oscillating runs of 18–50 ticks, then `figh` joined at tick 1997. `eff` was only ever spot-checked at load-verify time — hence the silent forward-sim drift.

## Root cause

`port/net/sys/netrollback.c::syNetRollbackIsActive()`:

```c
sb32 syNetRollbackIsActive(void) {
    return (sSYNetRollbackModuleEnabled != FALSE) && (sSYNetRollbackSessionActive != FALSE);
}
```

`sSYNetRollbackSessionActive` is set to `TRUE` for the **entire duration** of a netplay VS session.

`decomp/src/sys/utils.c::syUtilsRandUShortCosmetic()` (and its `Float` / `IntRange` siblings) before the fix:

```c
u16 syUtilsRandUShortCosmetic(void) {
    if (syNetRollbackIsActive() == FALSE) {
        return syUtilsRandUShort();                                  // shared game seed
    }
    return syUtilsRandUShortFromSeed(&sSYUtilsCosmeticRandomSeed);   // per-peer cosmetic seed
}
```

Every callsite in the **effect manager** (`decomp/src/ef/efmanager.c`) and **particle system** (`decomp/src/lb/lbparticle.c`) reaches that cosmetic API through the file-scope macro:

```c
#define syUtilsRandFloat     syUtilsRandFloatCosmetic
#define syUtilsRandIntRange  syUtilsRandIntRangeCosmetic
```

So during a netplay match:

- `syNetRollbackIsActive() == TRUE` the whole time (session active).
- Cosmetic API → per-peer `sSYUtilsCosmeticRandomSeed`.
- That seed is initialised to `syUtilsRandSeed()` once at `syNetRollbackStartVSSession()` (so both peers start equal), then is **never resynced**.
- The shared `rng` partition only hashes the *game* seed (`*sSYUtilsRandomSeedPtr`), so cosmetic-seed drift is invisible to FC.

Once a single effect spawn path consumes the cosmetic seed asymmetrically across peers (off-by-one keyframe, one extra particle lifetime decrement, etc.) the two peers' cosmetic seeds diverge **permanently**. Every subsequent effect's position / lifetime / scale / colour / spawn-or-not decision then diverges, and the `eff` partition (which folds effect position / `anim_frame` / `proc_update` fingerprint / pause-flag) drifts forever.

DK Jungle's TaruCann is the loud reproducer because its animation cycles continuously from tick 0 and feeds particle bursts into `efManager*`. Saffron stayed lucky because its Pokémon-spawn path is rare and never tripped the asymmetry inside the 10k-tick window.

The cosmetic-seed gate keying on `IsActive` was a transcription bug — the actual *intent* of the system (visible in the implementation: separate seed prevents the resim replay from advancing the authoritative shared seed) is satisfied only while a rollback resim is in progress. Forward sim should consume the shared seed (same as offline) so that asymmetric consumption is loud, not silent.

## Fix

### 1. Re-key the cosmetic-RNG gate (root cause)

`decomp/src/sys/utils.c`:

```c
/* Was: extern sb32 syNetRollbackIsActive(void); */
extern sb32 syNetRollbackIsResimulating(void);

u16 syUtilsRandUShortCosmetic(void) {
    if (syNetRollbackIsResimulating() == FALSE) {
        return syUtilsRandUShort();                                  // forward sim: shared seed
    }
    return syUtilsRandUShortFromSeed(&sSYUtilsCosmeticRandomSeed);   // resim: cosmetic seed
}
/* Same change in syUtilsRandFloatCosmetic and syUtilsRandIntRangeCosmetic. */
```

Result:

| Path                         | Old gate (`IsActive`) | New gate (`IsResimulating`) |
|------------------------------|-----------------------|------------------------------|
| Offline                      | shared seed            | shared seed (unchanged)      |
| Netplay forward sim          | per-peer cosmetic seed (silently drifts) | **shared seed (deterministic across peers)** |
| Netplay rollback resim       | per-peer cosmetic seed | per-peer cosmetic seed (unchanged — protects shared seed during replay) |

After the fix, any forward-sim asymmetry in cosmetic RNG consumption will perturb the shared game seed and trip the `rng` partition at the next FC checkpoint instead of silently forking the `eff` blob.

### 2. Per-tick `eff` and `cseed` diagnostics (lead 3 from the analysis)

`port/net/sys/netpeer.c::sim_state_tick` now also logs:

- `eff=0x%08X` — `syNetSyncHashActiveEffectsForRollback()` per tick (was only computed at load-verify time).
- `cseed=0x%08X` — raw value of `sSYUtilsCosmeticRandomSeed` (via new `syUtilsCosmeticRandSeed()` accessor in `decomp/src/sys/utils.h`).

Both also participate in the `hash_transition` log so transitions are easy to bisect.

If the cosmetic gate fix is correct, `cseed` should now stay byte-identical between peers during forward sim and only diverge during resim windows (where the divergence is intentional and bounded).

## Why this wasn't caught earlier

- The `rng` partition only folds the shared game seed (`syUtilsRandSeed()` returns `*sSYUtilsRandomSeedPtr`). The cosmetic seed is a completely separate variable that nothing in the FC hash set has ever covered.
- `eff` is part of the rollback hash set but was only computed at **load-verify** time, not in `sim_state_tick`. By the time a load_drift fires you're already inside a recovery attempt with no agreed-upon snapshot to anchor on.
- The first observable symptom (`gch` divergence) is a downstream effect: when host has an extra particle GObj for a few ticks, `gcPortGcRunAllTraversalFingerprintEx` walks one more node. By the time we noticed the pattern, the cosmetic seed had been drifting silently for hundreds of ticks.

## Verification plan

1. Run a fresh WAN match on DK Jungle (same matchup that failed: Mario vs Fox, 5-min). Expect no `LOAD_HASH_DRIFT`, no `state_diverge=1`, and `gch` partition stable. (Saffron should remain clean.)
2. Tail `sim_state_tick` from both peers and diff the `cseed=` and `eff=` fields. Both should be byte-identical for every tick of forward sim.
3. If any asymmetric cosmetic consumption remains (e.g. an effect path tied to display traversal rather than sim tick), the `rng` partition will diverge instead of `eff` — and the FC machinery will catch it normally instead of silently forking.

## Audit hook

Any RNG callsite that consumes a *per-peer* seed during forward sim is a netplay-desync hazard. After this fix, only one such seed exists (`sSYUtilsCosmeticRandomSeed`) and it's gated to resim-only. Cosmetic API entries:

```
decomp/src/sys/utils.c::syUtilsRandUShortCosmetic
decomp/src/sys/utils.c::syUtilsRandFloatCosmetic
decomp/src/sys/utils.c::syUtilsRandIntRangeCosmetic
```

If new code introduces a per-peer seed, it must either:

- Fold its current value into a partition that's hashed by `sim_state_tick` / FC, OR
- Be gated to a phase that does not affect cross-peer determinism (e.g. behind `syNetRollbackIsResimulating()` for "don't perturb shared seed during replay").

## Related

- [`netrollback_effect_snapshot_presence_phase1_2026-05-25.md`](netrollback_effect_snapshot_presence_phase1_2026-05-25.md) — Phase 1 effect snapshot (presence only, no generic respawn). With the cosmetic gate fix, forward-sim drift is prevented; phase 1's reconcile becomes a safety net rather than the primary mitigation. Phase 2 (per-bank effect respawn) becomes lower priority.
- [`netplay_tap_jump_local_cvar_desync_2026-05-25.md`](netplay_tap_jump_local_cvar_desync_2026-05-25.md) — Same class of bug (deterministic sim reads per-peer unreplicated state). That one was on the fighter side; this is on the cosmetic-RNG side. Both share the same fix pattern: gate the per-peer value to a phase where its asymmetry is acceptable.
- [`netrollback_camera_restore_resim_2026-05-25.md`](netrollback_camera_restore_resim_2026-05-25.md) — Same WAN soak campaign.
