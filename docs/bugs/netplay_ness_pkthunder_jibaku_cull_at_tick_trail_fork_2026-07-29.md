# Netplay: jibaku `cull_at_tick` trail fork (empty vs 4 weapons)

**Date:** 2026-07-29  
**Scope:** `PORT && SSB64_NETMENU`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Soak:** session `1845234634` seed `1617866830` (Android guest ↔ Linux host, Ness ditto)

## Symptom

Hold gravity/jibaku trigger matched after overlay/clamp fixes. At jibaku defer expiry:

- Android: `weapon_count` 5→**0** at tick 1414 (`gcEjectGObj` ×5 id=1012).
- Linux: `weapon_count` 5→**4** (head-only eject); `jibaku_post_cull` `weapons_after=4` every tick through 1429.
- `PEER_SNAPSHOT_DIVERGE` load=1418: `figh` MATCH, `wpn` empty vs non-empty + `cam`, `class=replay_determinism` → stop VS.

FC@1430 showed stick-age field noise (`tap_stick_y` 8 vs 6) with inputs agreeing — secondary to the durable `wpn` fork.

## Root cause

1. Jibaku schedules `DeferPKCullUntilTick = now + 2`. While defer is armed, live catch-up only partial-culls (keep head).
2. At expiry, `RunLiveJibakuCatchUpAll` called `CullAllOrphanPKThunderLive`, which **skips** mass-cull for fighters in jibaku catch-up preserve scope (intentional during the defer window — Head often already Collide/Destroy).
3. Android head destroy cascaded the trail ring before orphan cull (`weapons_before=0`). Linux ejected the head only; four trails remained, and orphan cull never tore them down.

## Fix

`port/net/sys/netplay_ness_pkthunder_gate.c`:

| Path | Change |
|------|--------|
| `syNetplayNessForceTeardownAllPKThunderForPlayer` | On defer expiry, `CullOwned(..., NULL)` for the defer player (clear `pkthunder_gobj` only in PK/jibaku status). |
| `syNetplayNessRunLiveJibakuCatchUpAll` | Call force teardown when `had_pending_defer` before orphan cull / wave prune. Diag `action=force_teardown`. |

Offline / non-netmenu unchanged. Defer window preserve during ticks `now < cull_at` unchanged.

## Follow-ups (not this patch)

- `ness_jibaku_stick_protect` was Linux-only in this soak (asymmetric GGPO stick replace) — separate symmetry pass.
- Durable `cam` soft-diverge from ~1401 while `wpn` still matched — camera hash hygiene.

## Test plan

- [ ] Re-soak Ness Up+B → Air jibaku; at `cull_at_tick` both peers `weapon_count=0` and `action=force_teardown` with `weapons_after=0`.
- [ ] No `PEER_SNAPSHOT_DIVERGE` `wpn` empty-vs-nonzero immediately after jibaku defer.
- [ ] Control: Hold (no jibaku) still keeps Head+Trails (`hold_skip=1`).
- [ ] Control: synctest over `cull_at_tick-1` still preserves deferred ring ([defer synctest](netplay_ness_pkthunder_jibaku_defer_synctest_2026-07-15.md)).
