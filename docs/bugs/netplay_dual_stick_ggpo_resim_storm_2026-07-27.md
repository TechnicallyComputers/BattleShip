# Dual-stick GGPO resim storm + FC peer_convergence live-cap hang (2026-07-27)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `1811750376` seed `3560961991` — Android guest ↔ Linux host; Ness ditto stage 6  
**Logs:** `/mnt/raid0/Software/BattleShip/logs/soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / protocol

## Symptom

1. **Storm:** When both peers move joysticks, resim begins multiply (~2.2× GGPO). One-mover windows stay quiet; dual-move @1200–1450 produced ~22 GGPO / ~48 begins (gaps ~7 ticks), polarity-flip invent misses.
2. **Hang:** After Hold-aim stick GGPO chain ~2075–2080, Android stuck:
   ```text
   try_begin_fail stage=fc_debounce mismatch=2078 target=2081
   sim advance blocked (rollback_epoch_cap=2082 source=2)  /* peer_convergence */
   ```
   Linux died on `runway_cap` (`frontier_sim=2082`). Not KO / not jibaku `sanitize_delay`.

## Root cause

### Dual-mover storm

Each peer only GGPO-corrects its **remote** slot. Post-episode stick absorb was scoped to `ResimCorrectionPlayer`, so REPLACE on the *other* remote opened a fresh initiator every ~D ticks. Peer notify mirrored each episode → initiator + follower ≈ 2×. Continuous polarity flips fail `SameAnalogIntent` (correct rewind), so invent hold-last cannot damp the loop.

Absorb already queued deferred REPLACE, but `TryBeginDeferredMismatch` began immediately — absorb did not rate-limit episode frequency.

### FC / epoch_cap hang

After GGPO commit at mismatch=2078, FC recovery stayed pending (core figh agreed; wpn still forked). `TryBeginDeferredStateMismatch` used bare `MismatchAllowedDuringDebounce` while GGPO uses `CorrectionAllowedAtTick` (episode-window bypass). Debounce blocked FC at the same mismatch; `peer_convergence` (source=2, target+slack=2082) blocked live advance → sim never reached `DebounceUntil` → deadlock.

## Fix (`port/net/sys/netrollback.c`)

| Layer | Change |
|-------|--------|
| Stick absorb scope | NETMENU: `StickAbsorbPlayer = -1` (any slot) after episode close |
| Coalesce Begin | NETMENU: local GGPO deferred waits out `StickAbsorbUntil` (`stick_absorb_coalesce`); peer-sym still Begins now |
| Live-cap | NETMENU: no `DeferredCorrectionBlocksLiveAdvance` while stick-absorb coalesce waits (avoid mismatch−1 hang) |
| FC debounce | NETMENU: FC TryBegin uses `CorrectionAllowedAtTick`; on residual deny clear peer_convergence (`fc_debounce_clear_peer_convergence`) |

## Acceptance

Matched APK + Linux netmenu:

- Dual-stick mash: episode rate drops vs pre-fix (~every-7-tick ping-pong); expect `stick_absorb_coalesce` / widened deferred, not 2× begins per GGPO sustained
- No perpetual `rollback_epoch_cap … source=2` after stick GGPO + FC pending
- Spans may widen inside one absorb window (≤~8 NETMENU after metronome fix) — preferred over storm frequency
- Soft GGPO on real polarity flips still allowed after absorb expires

## Related

- [`netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md`](netplay_stick_absorb_peer_convergence_runway_hang_2026-07-27.md) — **follow-on:** absorb coalesce still hung via peer_convergence + dual-hot runway
- [`netplay_stick_absorb_resim_metronome_2026-07-27.md`](netplay_stick_absorb_resim_metronome_2026-07-27.md) — **follow-on:** shorten absorb (no ×2) + hard ledger refresh clears coalesce wait
- [`netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md`](netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md) — **follow-on:** narrow NoteHard so mag REPLACE does not defeat dual-slot coalesce
- [`netplay_stick_storm_cooldown_livecap_deadlock_2026-07-12.md`](netplay_stick_storm_cooldown_livecap_deadlock_2026-07-12.md) — prior live-cap ↔ cooldown deadlock
- [`netplay_stick_lr_baseline_stash_hang_2026-07-12.md`](netplay_stick_lr_baseline_stash_hang_2026-07-12.md) — original same-player stick absorb
- [`netplay_fc_episode_begin_stall_retire_2026-07-26.md`](netplay_fc_episode_begin_stall_retire_2026-07-26.md) — `fc_peer_sym_prefer_began` (still present; not the hang root)
- [`netplay_hold_tracking_clock_sanitize_delay_jibaku_miss_2026-07-27.md`](netplay_hold_tracking_clock_sanitize_delay_jibaku_miss_2026-07-27.md) — separate Hold-clock jibaku miss
