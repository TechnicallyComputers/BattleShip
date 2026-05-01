# Master Hand fight — endgame SIGSEGV handoff (2026-05-01)

## What's known

- After fixing the okutsubushi/okupunch/drill animation bug
  ([`docs/bugs/boss_event32_cache_invalidation_2026-05-01.md`](bugs/boss_event32_cache_invalidation_2026-05-01.md)),
  user reports the game crashes (SIGSEGV) on **completing** the Master
  Hand fight — i.e. when the boss is defeated and the post-fight
  transition fires.
- This is a separate, pre-existing issue, not introduced by today's
  fix.  The user has seen it before today.
- A full pre-crash log captured during the okutsubushi-fix verification
  run is at:
  `logs/boss-okutsubushi-FIXED-2026-05-01.log`
  (5.6 MB, 34739 lines, includes per-frame boss telemetry plus walker
  diagnostics).  Should contain the run leading up to the boss's death
  state if the user fought to completion.

## Where to start the investigation

Probable code paths involved in boss defeat → transition:

- `src/ft/ftchar/ftboss/ftbossdeadleft.c`,
  `ftbossdeadright.c`, `ftbossdeadcenter.c` — boss death state procs.
- `ftBossCommonUpdateDamageStats` (`ftbosscommon.c:217`) — branches to
  `ftBossDeadLeftSetStatus` / `ftBossDeadRightSetStatus` when
  `percent_damage >= 300`.
- `sc1PGameBossDefeatInitInterface` — called from the same trigger.
- `sc1pgameboss.c` — scene-level boss-defeat handling, victory pose,
  scene transition.
- `sc1pstageclear.c` — stage-clear transition (the "VICTORY" screen
  sequence after Master Hand goes down).

## Useful instrumentation already on this branch

The boss telemetry block in `src/ft/ftmain.c` (`portBossTelemetry`,
removed in the cleanup commit but easy to re-add) was load-bearing for
diagnosing the okutsubushi bug.  The shape is good for this
investigation too — per-frame joint state + status_id + vel_air for
fkind=12.  Re-introduce if needed.

The UNHANDLED-opcode log in `gcParseDObjAnimJoint`
(`src/sys/objanim.c:777`) now includes `raw_u32=0x...` so any
animation-stream-related crash will surface its byte pattern.

## Status flow we know works

From the fix run's log, the boss cycled through statuses:
0xdd (Wait), 0xde (Move), 0xe4-0xeb (various attack-prep / aerial
forms), 0xf0-0xf8 (the attack states including okutsubushi=0xf7),
0xfc (Appear).  No 0xf9 / 0xfa / 0xfb (DeadLeft / DeadCenter /
DeadRight) appeared in this log — the user killed the run before the
boss died.

So the next investigator's first job is: run with telemetry enabled,
play through to defeat, capture the death-status frames + the scene
transition log lines, then look for the crash signature there.
