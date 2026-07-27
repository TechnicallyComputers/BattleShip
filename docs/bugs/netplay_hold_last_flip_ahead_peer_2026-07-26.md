# Hold-last flip_ahead → one-frame-early opposite invent → BASELINE kill (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `1387359873` — Android client (lp=1) ↔ Linux host (lp=0); matched builds post [hard-zero release_ahead](netplay_hold_last_release_ahead_hard_zero_peer_2026-07-26.md)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / `BASELINE_UNIVERSE_MISMATCH` (`agree_through_load=1`)

## Symptom

Post release_ahead retirement: **0×** `*_release_ahead`; session dies ~618.

| Check | Result |
|-------|--------|
| `*_release_ahead` | Cleared |
| Soft PEER | `BASELINE_UNIVERSE` load **592** → deepen exhaust → PEER (`figh`+`map`); cascade @595/600/605/609 |
| Hold-last diag | `smash_flip_ahead` / `hold_flip_ahead` still firing |

## Root cause

Android remote P0 invent at **522**: hold `(61,4)`. Ahead opposite-intent peek took owner **tick+1 = 523** `(-76,35)` and applied `hold_last_smash_flip_ahead` → invented flip **on 522**.

Linux owner still sampled `(61,4)` on 522; true flip is **523**. Same class on Linux P1 **@553**: owner `(-41)` on 553, flip `(32,4)` on 554 → `hold_flip_ahead`.

Release_ahead was already retired for the same one-frame-early reason; flip_ahead was the remaining ahead invent path.

## Fix (`port/net/sys/netinput.c` — `FillHoldLastSoftOnsetIfNeeded`)

| Layer | Change |
|-------|--------|
| Ahead peek loop | **Removed** — no release_ahead / flip_ahead invent |
| Tick-wire | Unchanged: release, opposite flip, dash-gate XOR, same-intent mag follow |

## Acceptance

Matched APK + Linux binary:

- 0× `hold_last_*_flip_ahead` / `*_release_ahead`
- Tick-wire `*_flip` / `*_release` / `*_follow` still fire when owner row for **this** tick disagrees
- Soft GGPO OK; no `BASELINE_UNIVERSE` deepen from one-frame-early flip invent (@522 / @553 class)

Rebuild desktop **and** Android APK before re-soak.

## Related

- [`netplay_hold_last_release_ahead_hard_zero_peer_2026-07-26.md`](netplay_hold_last_release_ahead_hard_zero_peer_2026-07-26.md) — ahead release retired (accepted; residual flip)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md) — tick-wire mag follow; ahead must not rewrite
- [`netplay_hold_last_nonsmash_release_flip_peer_2026-07-25.md`](netplay_hold_last_nonsmash_release_flip_peer_2026-07-25.md) — tick-wire release/flip for any analog hold
