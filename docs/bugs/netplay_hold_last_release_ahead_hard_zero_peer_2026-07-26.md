# Hold-last release_ahead hard (0,0) → false zero → BASELINE soft PEER (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `1402597419` seed `3625921901` — Android client (lp=1) ↔ Linux host (lp=0); matched APK + Linux binary (post soft-NearNeutral fix)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / `BASELINE_UNIVERSE_MISMATCH` (`agree_through_load=1`) mid-session; session end was separate map-only Whispy PEER

## Symptom

Post [soft NearNeutral](netplay_hold_last_release_ahead_soft_neutral_peer_2026-07-26.md): soft-neutral early release cleared; session lasts ~1713 ticks.

| Check | Result |
|-------|--------|
| Soft NearNeutral class | Cleared |
| Soft PEER mid-session | `BASELINE_UNIVERSE_MISMATCH` load **744** `class=replay_determinism` |
| Scan earliest | `TURN_DASH_FORK@430` (soft-healed via `branch_deferred_same_stick`) |
| Session end | **map-only** PEER ~1702 — Whispy Open→Blow 2-tick skew (Dream Land); figh matched — separate from this invent class |

## Root cause

Linux remote P1 invent at **744** held smash `(62,5)`. Ahead saw owner **tick+1 = 745** as hard `(0,0)` and applied `hold_last_smash_release_ahead` → invented **zero on 744**.

Android owner still sampled `(62,5)` on 744 (`pred=0`); true release is **745**. Same pattern at **500**: owner `(56,31)` on 500, hard zero on 501 → Android `smash_release_ahead` zeros 500 one frame early.

Soft-NearNeutral narrowing left **hard-zero tick+1 release_ahead** enabled; that path is still one-frame-early invent.

## Fix (`port/net/sys/netinput.c` — `FillHoldLastSoftOnsetIfNeeded`)

| Ahead rule | Change |
|------------|--------|
| Hard `(0,0)` or NearNeutral ahead | **Stop** scan — keep hold (no `*_release_ahead` invent) |
| Tick-wire NearNeutral / hard release | Unchanged (authority for this sim tick) |
| Opposite-intent flip_ahead | Unchanged |

`smash_release_ahead` / `hold_release_ahead` should no longer fire from the ahead peek loop.

## Acceptance

Matched APK + Linux binary:

- 0× `hold_last_*_release_ahead` in dual-stick dash-dance soaks
- Tick-wire `*_release` still zeros when owner row for **this** tick is neutral
- Soft GGPO OK; no `BASELINE_UNIVERSE` deepen from one-frame-early hard-zero invent (@744 / @500 class)

Whispy map-only PEER (~1702) is out of scope for this invent fix — track separately if it persists after release_ahead retirement.

**Residual (soak `1387359873`):** 0× release_ahead; kill from `smash_flip_ahead` @522 — [`netplay_hold_last_flip_ahead_peer_2026-07-26.md`](netplay_hold_last_flip_ahead_peer_2026-07-26.md).

Rebuild desktop **and** Android APK before re-soak.

## Related

- [`netplay_hold_last_release_ahead_soft_neutral_peer_2026-07-26.md`](netplay_hold_last_release_ahead_soft_neutral_peer_2026-07-26.md) — soft NearNeutral (accepted; residual hard zero)
- [`netplay_hold_last_release_ahead_skips_continue_peer_2026-07-26.md`](netplay_hold_last_release_ahead_skips_continue_peer_2026-07-26.md) — same-intent stop + tick+1 gate
- [`netplay_dead_stick_ggpo_resim_rng_whispy_blow_2026-07-20.md`](netplay_dead_stick_ggpo_resim_rng_whispy_blow_2026-07-20.md) — prior Whispy map/rng family (session-end residual here)
