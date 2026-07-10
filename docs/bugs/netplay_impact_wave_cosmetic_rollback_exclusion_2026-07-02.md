# Netplay: Firefox ImpactWave excluded from rollback hash + snapshot (cosmetic)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-07-02
**Builds:** `build-netmenu` + `build-offline` link clean.

## Symptom

Soak2 session `212585156` (host=`soak2-android.log`, guest=`soak2-linux.log`):

- Deterministic eff-only `SYNCTEST_FAIL` at **tick 1950 on both peers** (`load_hash_drift=1`,
  `eff=0x79652E53/0x2A6FF7DB`, all other partitions identical).
- Host `SIGSEGV` (`fault_addr=0x70000000c8`) at end of session, preceded by ~30 ticks
  (2010–2041) of `effect_xf_stale ... proc=DefaultProcUpdate reason=owner_mismatch
  effect_gobj_id=1011` spam.

`RESULT: FAIL`, drift tick on both peers = 1950.

## Root cause

The prior fix (`netplay_impact_wave_respawn_quake_alias`) correctly stopped masquerading the
Firefox `efManagerImpactWaveMakeEffect(..., index=4, ...)` shell as a `pri=4` quake and gave it a
dedicated `SYNETRB_EFFECT_RESPAWN_IMPACT_WAVE` capture/respawn/match path. That fixed the missing
animation, but pulled ImpactWaves fully into the id-keyed effect snapshot — where they don't fit:

`eff_fold_diag tag=capture tick=1950 count=3` — three live effects, **all sharing recycled
`gobj_id=1011`**:

| idx | respawn        | quake_pri (=impact_wave.index) | anim_frame | note        |
|-----|----------------|--------------------------------|------------|-------------|
| 0   | 12 IMPACT_WAVE | 4                              | 0x41300000 | shell A     |
| 1   | 1 QUAKE        | 2                              | 0x41300000 | real quake  |
| 2   | 12 IMPACT_WAVE | 4                              | 0x3F800000 | shell B     |

The snapshot canonicalizes effects by `gobj_id`, and `syNetRbSnapEffectGobjIdCollisionAllowsCoexist`
only permitted QUAKE+QUAKE and IMPACT_WAVE+QUAKE — **not** IMPACT_WAVE+IMPACT_WAVE. Two same-id
ImpactWaves therefore deduped down to one blob on save:

`eff_fold_diag tag=verify tick=1950 count=2` — only the quake + one ImpactWave restored, and that
survivor's `impact_wave.index` came back **0** instead of 4. Live fold hashed 3 effects, save/load
could only represent 2 → deterministic eff mismatch on both peers (non-idempotent save/load, so the
synctest oracle fires locally on each peer, not a cross-peer input divergence).

Respawning ImpactWaves through `efManagerImpactWaveMakeEffect` during rollback verify/enforce also
churned the recycled-id (1011) effect pool. That stranded an **unrelated** particle effect's
`LBTransform` (`proc=DefaultProcUpdate`, xf `owner_mismatch`) — the `DefaultProcUpdate` particle is
*not* the ImpactWave (which uses `efManagerImpactWaveProcUpdate` and owns no xf) but shares the
churned pool id. The stale-xf guard ejected it every tick from 2010–2041, then a dangling
deref went to `SIGSEGV`.

## Why exclusion is the right fix

ImpactWaves are short-lived, purely cosmetic VFX: `efManagerImpactWaveMakeEffect` sets only
`impact_wave.index/alpha/decay`, `alpha` decays over ~11 frames, and `efManagerImpactWaveProcUpdate`
carries no gameplay state. They are spawned deterministically from fighter state, so both peers
create them identically in forward sim. There is nothing for rollback to reconcile:

- Excluding them from the **eff hash** on both peers means neither peer counts them → no cross-peer
  eff divergence and no local capture-vs-verify mismatch. Any real desync still shows up in the
  fighter/world/item/rng partitions (ImpactWaves are strictly downstream of fighter state).
- Excluding them from the **snapshot** means rollback never respawns/canonicalizes them, so the
  recycled-id pool churn (and the stranded-xf `SIGSEGV`) disappears. The live shells are never
  ejected by effect-enforce (it skips hidden effects) and simply finish their natural ~11-frame
  decay; the presentation reconcilers keep the visual aligned.

This mirrors the existing `SYNETRB_YOSHI_EGG_LAY_HATCH_COSMETIC_BANK_ID` precedent.

## Fix

`port/net/sys/netrollbacksnapshot.c` — `syNetRbSnapEffectHiddenFromRollback` now returns TRUE for
Firefox ImpactWaves (`syNetRbSnapLiveEffectIsImpactWave`), in addition to the Yoshi-egg-hatch
cosmetic. This single chokepoint is consulted by `syNetRbEnumerateActiveEffectsSorted` (the eff
hash fold in `netsync.c`) and every capture/enforce/eject/match pass, so ImpactWaves drop out of
the entire rollback effect pipeline consistently. A forward declaration of the predicate was added
near the other snapshot forward decls.

The presentation-layer helpers (`syNetRbSnapReconcileFoxFirefoxImpactWavesFromSlot`,
`syNetRbSnapEjectOrphanFoxFirefoxImpactWaves`) are unaffected: the reconciler becomes a no-op when
no ImpactWave blobs exist, and orphan cleanup still runs.

The dedicated ImpactWave capture/respawn/match path from
`netplay_impact_wave_respawn_quake_alias` is now **dormant** (ImpactWaves are never enumerated, so
they are never classified as `SYNETRB_EFFECT_RESPAWN_IMPACT_WAVE`, captured, matched, or respawned).
It is left inert rather than reverted to keep this change minimal and low-risk; a follow-up cleanup
can remove it once this soak confirms determinism.

## Verify

- `build-netmenu` `ssb64` target: links clean.
- `build-offline` `ssb64` target: links clean (change is `SSB64_NETMENU`-gated).
- Soak pending.
