# Netplay: Whispy positive blink hold → +1 map diverge (2026-07-15)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-15  
**Session:** soak1 `197856492` seed `2369140563` (Android client ↔ Linux host)

## Symptom

Five healthy stick GGPOs (loads 418…616, synctest OK). Then silent live play until GGPO @812 exposes **map-only** `PEER_SNAPSHOT_DIVERGE` @811:

- `figh` / `world` / `rng` / `anim` / `cam` match; `kin` match
- `map` Android `0x94FFBAE2` vs Linux `0x55E9B265` (`ground_fold` only)
- Drift scan / FC `state_diverge=0` — map fork never killed the session live
- Linux fail-closed (`map-only deeper exhausted`); not a deepen/resim storm

## Root cause

After lockout exit, both peers reseeded `blink=266` at tick **615** (seed×tick mix OK). Vanilla / prior netplay path still **gated positive blink decrement** on `map_gobj[0]` eyes `anim_frame` ended. The eyes blink started at the `-10` fire left a cross-ISA leftover:

| Tick | Linux blink | Android blink |
|------|-------------|---------------|
| 606–614 | lockout `-1…-9` | match |
| **615** | **266** (reseed) | **266** |
| 616–623 | **hold 266** | **hold 266** |
| **624** | **265** | **266** ← first fold fork |
| …811 | permanent +1 | |

Same leftover class as leave-zero / lockout, but those paths already skipped the anim gate; **positive waits after reseed still gated**.

## Fix (`decomp/src/gr/grcommon/grpupupu.c`)

Under `syNetplayRollbackSemanticsActive()`, when `whispy_eyes_status == -1`, **always** decrement `whispy_blink_wait` — no `MapGobjAnimFrameEnded` gate for lockout, leave-zero, or positive waits. Eyes anim still plays for presentation; countdown (hashed into `ground_fold`) is authoritative.

## Verify

Re-soak Dream Land stick mash through a Whispy `-10` reseed:

- After reseed, `pupupu_ground blink=` should decrement in lockstep (no multi-tick hold then +1 skew).
- Prefer matching `mph` / no map-only baseline DIVERGE with matched figh/rng.

Related: [`netplay_pupupu_whispy_blink_zero_hold_map_diverge_2026-07-13.md`](netplay_pupupu_whispy_blink_zero_hold_map_diverge_2026-07-13.md), [`netplay_pupupu_whispy_blink_rng_fc_2026-07-12.md`](netplay_pupupu_whispy_blink_rng_fc_2026-07-12.md), [`netplay_pupupu_whispy_blink_cosmetic_stream_map_diverge_2026-07-13.md`](netplay_pupupu_whispy_blink_cosmetic_stream_map_diverge_2026-07-13.md), [`netplay_baseline_state_deepen_exhaust_fail_closed_2026-07-15.md`](netplay_baseline_state_deepen_exhaust_fail_closed_2026-07-15.md).
