# Netplay: Pupupu Open→Blow phase skew → FRAME_COMMIT rng + map diverge — 2026-07-12

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-12  
**Seed:** `3628321978` (session `1172627293`, Android client ↔ Linux host, Dream Land Kirby ditto)

## Symptom

Long healthy mash (LOADSAFE promote, synctest OK, ep2 stick GGPO @2372 completed) then:

- `FRAME_COMMIT_STATE_DIVERGE @2400` **`diverged=rng`**, `inputs=DIFFER` in the scan (first `STICK_SAMPLE` still matched through 2399 — input skew is secondary / FC hist window)
- Android `rng=0x7B31FD41` vs Linux `0x21E58576` (Linux still on the long-stable game seed)
- `pupupu_ground`: Android `status=4` Blow @2399 (`wind_dur=251`); Linux Blow @2400
- Recovery: `PEER_BASELINE_MAP_DRIFT` → deeper exhaust **map-only** → Linux `PEER_SNAPSHOT_DIVERGE` @load 2396 (figh/world/rng/anim matched; `ground_fold` forked) → `VS_SESSION_END`

Not a classic resim storm (`resync_storm=0`, only a few episodes); the “storm” is cascading map baseline deepen after the Whispy phase fork.

## Root cause

1. **GGPO @2372 left stale eyes `map_gobj[0]` anim** — Pupupu ground blob restores FSM scalars only. Pre-rewind live still had mid-blink eyes leftover. After load `blink=2` but `MapGobjAnimFrameEnded(eyes)==FALSE` → resim never decremented blink (forward had `2→1→0`). Both peers exited episode with blink stuck at 2 until leftovers drained.

2. **Open→Blow gates on mouth `map_gobj[1]` anim end** — `grPupupuWhispyUpdateOpen` burns **gameplay** `syUtilsRandIntRange` for `whispy_wind_duration`. Cross-ISA near-zero leftovers (fighter-width `256/65536` snap too tight for Whispy) → Android Blow one tick early → FC `rng` fork + `ground_fold` / map hash drift.

3. **Blink mouth Stretch** (netplay path) can restart mouth while still Open, amplifying Open→Blow desync when one ISA thinks mouth ended.

Related: [`netplay_pupupu_whispy_blink_rng_fc_2026-07-12.md`](netplay_pupupu_whispy_blink_rng_fc_2026-07-12.md) (lockout / ForcedCosmetic reseed), [`netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md`](netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md) (map anim harden + fail-closed map diverge).

## Fix

Under `PORT && SSB64_NETMENU`:

1. **After Pupupu ground apply** — `syNetRbSnapRefreshPupupuWhispyMapAnimAfterLoad`: pin eyes ended when `eyes_status==-1`; apply pending `UpdateGObjAnims`; wide-snap non-mouth map gobjs.
2. **Wider map anim end snap** — `syNetplaySnapMapGobjAnimFrameToEndIfNearZero` (`4096/65536`); `MapGobjAnimFrameEnded` / BeforeSim harden use it (fighters keep `256/65536`).
3. **Skip blink mouth Stretch** under rollback-active blink path (eyes blink only).

## Verify

Re-soak Dream Land through a stick GGPO during Whispy Open and into first Blow after Go:

- No FC `diverged=rng` from asymmetric `wind_dur` roll
- `pupupu_ground` Blow onset tick matches cross-peer
- No map-only `PEER_SNAPSHOT_DIVERGE` deepen chain after Blow
- Resim after stick correction: blink should continue decrementing (not freeze at a positive wait for many ticks)
