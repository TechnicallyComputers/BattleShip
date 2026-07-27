# Retire Pause `pause_defer` / `fc_pause_defer` — immediate confirmed-input resim (2026-07-26)

**Status:** FIX ACCEPTED (`PORT && SSB64_NETMENU`)  
**Soak (accept):** seed `3232640437` — pause/unpause mash reliable; lag/jitter deferred to invent phases  
**Bucket:** `REPLAY_DETERMINISM` / protocol  
**Policy sibling:** [`netplay_ness_pk_defer_retire_input_resim_2026-07-26.md`](netplay_ness_pk_defer_retire_input_resim_2026-07-26.md)

## Motivation

Ness jibaku TryBegin defer retirement (soak `41294254` → post-fix `173056225`) showed the right shape: short constant resim (max span 5), no mega-span predict under lifted live-cap. Remaining Class-A TryBegin gate was Pause/Unpause:

- `syNetRollbackDeferResimForPauseTransition` blocked GGPO Begin (`pause_defer`) and FC recovery (`fc_pause_defer`) while `game_status` was Pause or Unpause.
- Same anti-pattern as Ness: refuse rewind while live can advance → span growth / stale predict if a REPLACE lands mid-transition.

Idle on soak `173056225` (no pause mash), but still the last move/world-status Begin defer in `netrollback.c`.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| TryBegin | `DeferResimForPauseTransition` → always `FALSE` (GGPO + FC call sites keep log stage names for compatibility) |
| Live-cap | Unchanged: pending correction caps at `mismatch-1` (no pause lift) |
| Offline | Original Pause/Unpause defer retained under `#else` (non-`SSB64_NETMENU` PORT) |

Offline binary unchanged. If pause-edge hashes diverge under immediate burst, fix pause snapshot/world couple — do not re-add TryBegin defer.

## Acceptance

Matched APK + Linux, mid-match pause mash during active GGPO traffic:

- No perpetual `try_begin_fail stage=pause_defer` / `fc_pause_defer`
- Mid-pause REPLACE → `resim begin` in the same short window (span ≪ 20)
- After return to Go: soft-stable / no PEER storm attributable to pause edge
- Prefer short resim over SoftLip hardening

## Related

- [`netplay_ness_pk_defer_retire_input_resim_2026-07-26.md`](netplay_ness_pk_defer_retire_input_resim_2026-07-26.md) — template policy
- Class-B follow-up: [`netplay_fc_episode_begin_stall_retire_2026-07-26.md`](netplay_fc_episode_begin_stall_retire_2026-07-26.md)
