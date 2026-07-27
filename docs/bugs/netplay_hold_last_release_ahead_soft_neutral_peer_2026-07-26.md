# Hold-last release_ahead soft-NearNeutral → false zero → BASELINE kill (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `656649663` seed `2960897149` — Android client (lp=1) ↔ Linux host (lp=0)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / `BASELINE_UNIVERSE_MISMATCH` (`agree_through_load=1`)

## Symptom

Post [release_ahead skips continue](netplay_hold_last_release_ahead_skips_continue_peer_2026-07-26.md): same-intent skip + tick+1-only release_ahead in binary; session dies ~1365.

| Check | Result |
|-------|--------|
| Prior fixes | Same-stick deferred EQ; unequal StickReplace @404; no skip-over-continue false zero @1664-class |
| Soft recovery | Large `class=protocol` scan churn; P0 rematches through ~880 |
| Kill | `BASELINE_UNIVERSE_MISMATCH` load **879** → deepen exhaust → soft PEER (`resim_seal_wait`); `VS_SESSION_END` ~1366 |
| Fork surface | P1 **LandingLight(31)** vs **SquatWait(29)** @878–880 (P0 light/full hashes match @879) |

## Root cause

Linux remote P1 invent at **870** held `(-12,-19)`. Ahead scan saw owner **tick+1 = 871** as `(-8,-4)`, which is `NearNeutral` (GGPO predict deadband ≤14) but **not** a hard zero.

`hold_last_hold_release_ahead` rewrote **870 → (0,0)** one frame early. Android owner still sampled `(-12,-19)` on 870 (`pred=0`). Soft settle to `(-8,-4)` belongs on **871**.

P0 Y mag invent `(-84→-50)` @870 also GGPO'd, but P0 slot hashes rematched by 873–880. Aggregate `figh` diverge at load 879 is the P1 landing/squat fork after the false zero.

Prior fix correctly stopped scanning past same-intent analog and limited release_ahead to tick+1, but still treated **soft NearNeutral** as a full release.

## Fix (`port/net/sys/netinput.c` — `FillHoldLastSoftOnsetIfNeeded`)

| Ahead rule | Change |
|------------|--------|
| Release_ahead | Only **`SticksHardZero`** at **`tick+1`** |
| Soft NearNeutral ahead | **Stop** scan — keep hold (do not invent `(0,0)`) |
| Tick-wire NearNeutral release | Unchanged (authority for this sim tick) |

Same-intent break / opposite flip_ahead unchanged.

## Acceptance

Matched APK + Linux binary:

- No `hold_last_*_release_ahead` from tick+1 soft sticks like `(-8,-4)` while current tick still has gameplay mag
- Soft GGPO OK; 0× deepen PEER from this soft-neutral early release class

**Residual (soak `1402597419`, matched builds):** hard `(0,0)` at tick+1 still fired `smash_release_ahead` (@744 / @500) — [`netplay_hold_last_release_ahead_hard_zero_peer_2026-07-26.md`](netplay_hold_last_release_ahead_hard_zero_peer_2026-07-26.md) retires all ahead release invent.

Rebuild desktop **and** Android APK before re-soak.

## Related

- [`netplay_hold_last_release_ahead_skips_continue_peer_2026-07-26.md`](netplay_hold_last_release_ahead_skips_continue_peer_2026-07-26.md) — same-intent stop + tick+1 gate (residual: soft NearNeutral)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md) — tick-wire mag follow; ahead must not rewrite from future decay
- [`netplay_seal_pack_latch_turn_dash_soft_nz_2026-07-20.md`](netplay_seal_pack_latch_turn_dash_soft_nz_2026-07-20.md) — hard-zero vs soft_nz elsewhere in the stack
