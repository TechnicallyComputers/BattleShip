# Netplay Mario fireball empty throw after latch (mid-throw retry)

**Date:** 2026-06-05  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `scripts/netplay-trim-logs.py`

## Symptom

Post–effect-xf-stabilization soak (Sector, Mario vs Ness): no SIGSEGV, but Mario fireballs sometimes
vanished early, failed to appear, or did not hit Ness at close range. Trim log showed long
`fireball_spawn skip=latched` runs with `weapon_count=0` while still in SpecialN (`status=223`).

## Root cause

| Layer | Issue |
|-------|--------|
| **flag1 latch** | Phase 5b.1 latched on any `flag1 != 0` without checking for a live owned fireball. After rollback/cull/hit removed the ball, spawn helper returned `skip=latched` for the rest of the throw → empty anim. |
| **Hand duplicate cull** | `syNetRbSnapCullOwnedFireballsNearPose` used spawn-time hand position and 60-unit radius, culling in-flight balls still near Mario when spamming B at close range. |

## Fix

| Area | Change |
|------|--------|
| **flag1** | `flag1 && owned → skip=latched`; when latched but `!syNetRbSnapFireballOwnedByFighter`, clear latch (`skip=latch_clear`) and `path=retry` once if `anim_frame < 25`. |
| **Dup cull** | Cull only fireballs within 30 units of the **live** hand joint (`syNetRbSnapFireballNearFighterHand`), not stale spawn pose / in-flight projectiles. |
| **Trim script** | `collect_fireball_spawn_summary` — path/skip counts, latch_clear/retry/emergency+anim suspects. |

## Verification

Soak with `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`: one `path=anim` or `path=retry` per B press; no long
`skip=latched` with `weapon_count=0` mid-SpecialN; `path=retry` only after `skip=latch_clear`.

## Follow-up 2026-06-05 — retry over-fired into a close-range fireball stream

### Symptom
"Still has double fireball bug whenever standing close to target." Fresh Sector soak
(`host.log`/`guest.log`, byte-identical → no desync) showed, per Mario `SpecialN` throw
at point-blank range, the cycle **every** throw-window frame (≈17→24):

```
fireball_spawn skip=latch_clear ... anim_frame=17.0
fireball_spawn path=retry ...
weapon save ... weapon_count=0      <- ball gone again next tick
```

Worst case (tick 721): **1 `path=anim` + 8 `path=retry` = 9 fireballs from one B press**.
`cull_stale` never fired — at retry time there is no owned ball to cull (it already left),
so this is a *serial* re-spawn, distinct from the earlier *coexisting*-balls double.

### Root cause
The original "retry once" had no once-guard. `flag1` (the latch) is set to `1` **only after**
a ball is actually made, so the `flag1 != 0 && !owned` branch is *always* entered after a
successful delivery. The branch cleared `flag1` and re-spawned whenever no ball was owned,
gated only by `anim_frame < 25` — i.e. the entire active throw. At close range the thrown ball
clears the 60-unit hand radius / hits the point-blank target and despawns within one frame, so
`syNetRbSnapFireballOwnedByFighter` is FALSE on every subsequent frame → clear latch → retry →
spawn → repeat. `syNetRbSnapFireballOwnedByFighter` cannot distinguish "launched and consumed"
(legitimate) from "rollback lost a ball that should still be in flight" (the only case retry was
meant to serve).

### Fix
`port/net/sys/netrollbacksnapshot.c`, `decomp/src/ft/ftchar/ftmario/ftmariospecialn.c`,
`decomp/src/ft/ftchar/ftkirby/ftkirbycopymariospecialn.c`:

- Restrict the recovery retry to **resim only** (`syNetRollbackIsResimulating()`). In live
  forward sim the weapon lifecycle is authoritative — a now-gone latched ball left legitimately,
  so no re-spawn → **exactly one fireball per throw at any range.**
- Cap the retry at **once per throw** via `flag2` (a previously unused motion flag for this
  status; snapshotted, so rollback-safe), reset alongside `flag1` at throw entry in both
  `ft*SpecialNInitStatusVars`. This bounds the resim recovery so it can never stream.
- In-flight balls legitimately lost to a rollback are restored by the weapon snapshot
  (fireballs are snapshotted weapons), and missed-anim spawns during resim are covered by the
  `path=emergency` path — neither needs the forward-sim retry.

Offline is unaffected (decomp edits are `PORT && SSB64_NETMENU`-gated; the vanilla accessory only
reads `flag0`). Netmenu + offline builds link clean.

### Soak check
Point-blank Mario B spam: one `path=anim` per press, **no** `path=retry` in forward sim,
`weapon_count` peaking at 1 per Mario fireball; any `path=retry` lines should appear only inside
resim bursts and at most once per throw.

## Related

- [`fireball_spawn_phase5b_2026-05-22.md`](fireball_spawn_phase5b_2026-05-22.md)
- [`fireball_double_spawn_2026-05-22.md`](fireball_double_spawn_2026-05-22.md)
- [`mario_fireball_empty_throw_2026-05-20.md`](mario_fireball_empty_throw_2026-05-20.md)
