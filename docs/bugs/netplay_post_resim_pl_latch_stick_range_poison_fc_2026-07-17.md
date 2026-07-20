# Post-resim pl latch: stick_range poisoned by exclusive-target device → tap fork FC

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-17  
**Session:** `699967527` seed `964803463`

## Symptom

- Synctest OK / 0 FAIL; no `LOAD_HASH_DRIFT`
- GGPO `real_stick` @629 (pred `-9,82` → wire `-14,81`) completes on both peers with matched baseline/post digests
- `FRAME_COMMIT_STATE_DIVERGE` @640 `figh` only, **inputs MATCH**
- FC fighter diag fields matched (no `FIELD_PEER_DIFF`) — fork was outside the old diag set
- Slot light hashes: Android `0xC2778AFE` vs Linux `0x7C740ADA` (P0 Ness `SpecialAirHiStart`)

## Evidence

Exclusive target after resim is 631. Same published sticks on both peers:

| Peer | STICK_SAMPLE @631 | tap_x |
|------|-------------------|-------|
| Android (remote P0) | sx=-21 sy=79 | **1** (deadzone exit) |
| Linux (local P0) | sx=-21 sy=79 | **254** (held) |

Linux `STICK_TAP_WITNESS` @631 post-resim: `prev=(-21,79)` — should have been `prev=(-18,…)` from tick 630 (`sx=-18` is inside the ±20 deadzone). Android continued `tap_x=2…9` through 639; Linux stayed at 254. Counters are folded into `fhash_light` → FC figh diverge with matched TopN.

## Root cause

`syNetInputRollbackResyncControllersAfterResim` wire-lock republishes **local** slots for `[target, frontier]` onto `gSYControllerDevices`, then calls `syNetInputRollbackResyncFighterPlLatchFromControllers`.

`ftMainProcessInput` always does:

```c
pl->stick_prev = pl->stick_range;   /* previous tick */
pl->stick_range = controller;       /* this tick */
```

The resync set `stick_prev` from history[target-1] but **`stick_range` from the already-republished target device**. ProcessInput then copied that target stick into `stick_prev`, erasing the history latch.

- **Linux (local auth P0):** device republished to -21 before latch resync → first live ProcessInput sees prev=-21 → increment (tap stays 254).
- **Android (remote P0):** restore loop skips non-local slots; device still at -18 from last resim publish → ProcessInput sees prev=-18, curr=-21 → reset to 1.

Same class family as [`netplay_post_resim_wirelocked_hid_restage_2026-07-13.md`](netplay_post_resim_wirelocked_hid_restage_2026-07-13.md) / [`netplay_stick_latch_resim_fork_2026-07-03.md`](netplay_stick_latch_resim_fork_2026-07-03.md), but the poison was the latch helper itself after wire-lock PublishFrame.

## Fix

In `port/net/sys/netinput.c` (`syNetInputRollbackResyncFighterPlLatchFromControllers`):

- Seed **both** `pl->stick_range` and `pl->stick_prev` from history[sim_tick-1] (clamped to `I_CONTROLLER_RANGE_MAX`), matching ProcessInput's entry invariant.
- Seed `pl->button_hold` from that prior frame's buttons so exclusive-tick tap/release edges are not suppressed.
- Fallback (no history): still mirror controller into both stick fields (no false edge).

Diagnostics: FC fighter diag now carries `tap_stick_*` / `hold_stick_*` (`SYNETPEER_FRAME_COMMIT_FIGHTER_DIAG_U32S` 16→20) so the next silent light-hash fork logs `FIELD_PEER_DIFF`.

## Verify

Re-soak Android↔Linux with a stick GGPO whose exclusive target crosses `|sx|` 18→21 (or any deadzone exit):

- Post-resim `STICK_SAMPLE` @ exclusive target: both peers `tap_x=1` (or matching counters)
- No `FRAME_COMMIT_STATE_DIVERGE figh` with `inputs=MATCH` from this latch class
- Both peers rebuilt together (FC token size changed)
