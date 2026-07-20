# Intro Wait advance / FC deadlock (Appear freeze)

**Date:** 2026-07-18  
**Status:** FIX DEEPENED (`PORT && SSB64_NETMENU`, re-soak)

## Not the synctest skip

`SYNCTEST_SKIP reason=intro_wait` only skips hash compare during Wait. It does **not** block FuncRead, Advance, or ingress. Freezes were AdvanceAllowed + Android `recv` blackholes at FC cadence.

## Symptom (latest soaks)

| Log | Block | Pattern |
|-----|--------|---------|
| ~00:52 | `intro_wait_frontier` | next=247 hr=243 intro_cap=246; recv frozen |
| ~01:06 | `wire_need` | next=248 hr=243 need=244; 555× at sim 247; recv=252 stuck |

Admission `P=100%`. Linux recovers when `hr` moves; Android `INPUT recv=0` after the stall.

## Fix

Intro Wait = force-neutral deterministic playback:

1. **AdvanceAllowed during Wait** — only `hr==0` blocks; **no** `wire_need` / `runway_cap` until post-Go.
2. **Defer frame-commit** mint/send while `game_status==Wait` (no FC@120/240 during Appear).
3. ICE: always `mmIceEnsureIoResumed` on ingress; pump on advance-hold.
4. Keep force-neutral Wait inputs (`netplay_intro_wait_force_neutral_input_2026-07-12.md`).

## Files

- `port/net/sys/netinput.c` — `syNetInputRollbackSimAdvanceAllowed`
- `port/net/sys/netpeer.c` — FC defer + ICE pump
- `decomp/src/sc/sccommon/scvsbattle.c` — advance-hold ingress pump

## Re-soak

- No `wire_need` / `intro_wait_frontier` during Appear.
- `FRAME_COMMIT_DIAG sent=0` until after Go (then cadence resumes).
- Appear through GO without multi-second freezes.

**Follow-up (same day):** skipping `wire_need` for all of Wait can leave `hr` frozen into Go → hard hang when pacing returns. See [`netplay_post_go_wire_need_hang_2026-07-18.md`](netplay_post_go_wire_need_hang_2026-07-18.md).
