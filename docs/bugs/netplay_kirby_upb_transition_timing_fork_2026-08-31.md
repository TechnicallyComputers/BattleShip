# Up-B entered 4 ticks apart across peers: the grab bug's class, new instance

**Soak:** FC diverges at 761 (and downstream 3554), figh-only — rng/world/item/eff all
matched, inputs matched.

## The fork, tick by tick

Player 1 (Kirby, android = input owner):

| | android (owner) | linux (predictor) |
|---|---|---|
| status 257 → 12 | tick 737 | tick 737 (agree) |
| **12 → 256 (up-B)** | **tick 747** | **tick 751 — 4 ticks late** |

Linux has resim episodes loading at exactly 746 and 747 (`load 747 → target 751`): the
corrected input for the activation tick arrived, the resim replayed the window — and the
replay did not produce the transition; it only appeared once live sim resumed at 751.

The FC field diff at 760 is the 4-tick shift wearing different clothes:
`fold_status_total_tics` 43 vs 39, `fold_vel_air_y` **rising on one peer and falling on
the other** (the same arc, sampled 4 ticks apart), positions ~50 units apart, and
tap/stick-latch fields differing downstream.

## Ruled out

- **Input divergence** — `inp_local == inp_peer` at the 761 validation.
- **RNG** — the rng partition matched at every validation this soak.
- **Tap/stick latch snapshot coverage** — `tap/hold_stick_*` and the `input.pl` latch are
  captured and applied (that class was fixed 2026-07-03,
  `netplay_stick_latch_resim_fork`); the latch diffs at 760 are downstream of the shift.

## Same class as the grab connect loss

`docs/bugs/netplay_grab_resim_connect_loss_2026-08-29.md`: live sim executes a status
transition; the resim replay of the same tick does not reproduce it; the divergence then
stands. There it was `166 → 167` (grab connect); here it is `12 → 256` (up-B activation).
Two instances of one mechanism — something a transition's precondition reads is not
faithfully part of the replayed state.

## Instrumentation (this commit)

The SetStatus trace (already hooked into `ftMainSetStatus` for every transition) gains an
env-configurable band: `SSB64_NETPLAY_SETSTATUS_TRACE=min-max` (e.g. `256-262` for the
Kirby special band) or `all`, alongside the always-on grab band. Each line carries the
tick, from/to, dladdr-resolved caller, and `resim=`.

**Next capture:** both peers with `SSB64_NETPLAY_GUARD_GRAB_DIAG=1` and
`SSB64_NETPLAY_SETSTATUS_TRACE=250-280`. At the next such fork, the predictor's log
answers directly: does the resim pass of the activation tick log the transition
(`resim=1`) at all? If yes, from which caller and at which replayed tick; if no, the
activation's precondition state is what the snapshot is missing — compare the owner's
live `caller=` against the predictor's silence the way `search_catch` pinned the grab.

---

## Capture run (2026-08-31, both banners armed): instrument validated; fork did not reproduce

First run with the band trace live on both peers (`setstatus trace band=250..280 (armed)`
on each — the previous silent-Linux mystery was a stale binary or env delivery, settled by
the banner). Result:

- **All 8 band transitions matched byte-for-byte across peers** — same tick, same
  from/to, same caller (Android resolves real offsets, e.g.
  `ftKirbySpecialHiSetStatus+0x44`, Linux `+0x0`). Three complete up-Bs, including the
  `259 → 257` ProcMap landing edge, in perfect agreement. The up-B timing fork did not
  reproduce this session.
- One FC diverge at validation 581, and it is a **different class**: player 0 in the
  guard band (status 154 — outside the traced 250–280 window), `status_total_tics` off
  by exactly 1, joint deltas one anim frame apart, and — decisively — **input digests
  DIFFER** at the fold (`inp_local=0x66AECF74 inp_peer=0x72065091`), with a
  `FRAME_COMMIT_LATE_MINT` immediately preceding. That is input-skew territory, not the
  identical-inputs replay-determinism class this doc tracks. Unresolved this session
  only because Android quit right after the validation, before the next cycle could show
  whether recovery healed it.

**Next runs: use `SSB64_NETPLAY_SETSTATUS_TRACE=all`** — the guard band sat outside the
250–280 window, so the 581 entry-timing offset has no trace. `all` costs a handful of
lines per transition and covers every future band without guessing.

