# Resim local MakeLocalFrame invents (0,0) → tap fork / SoftLip / PEER deepen

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android client ↔ Linux host, Dream Land Ness ditto, seed `106015840`  
**Logs:** `/mnt/raid0/Software/BattleShip/logs/soak1-{android,linux}.log`  
**Related:** [light exclusive frontier poison](netplay_light_exclusive_frontier_poison_load_2026-07-28.md), [post-resim gap hold](netplay_post_resim_local_publish_gap_branch_deferred_2026-07-26.md), [stick latch resim](netplay_stick_latch_resim_fork_2026-07-03.md), [FC yield join](netplay_fc_yield_commit_behind_frontier_2026-07-28.md)

## Symptom

- SoftLip TopN matched through gut **546**; first X/status fork @**547** (P0 Fall vs KneeBend).
- FC@546: `INPUT_SKEW_ESCALATE_BYPASS`; fighter fields **only** `tap_stick_*` / `hold_stick_*` (status/motion match).
- FC recovery seals + deepen succeed; kill is `PEER_SNAPSHOT_DIVERGE` @load **544** `class=replay_determinism` (figh+map+anim; world/rng/item match; `agree_through_load=1`).
- Yield/seal join plumbing OK (`keep_fc_arm=1`, `COMPATIBLE_APPLY`).

## Smoking gun

Light GGPO `mismatch=539 target=541` (`owner=local_light`). During resim of exclusive-span tick **540**:

| Peer | Role for P0 | `STICK_SAMPLE` @540 |
|------|-------------|---------------------|
| Linux host | local | **`sx=0 sy=0`** `pred=1` (`publish_frame` mint) |
| Android guest | remote | **`sx=-75 sy=42`** hold_last prediction |

Linux then:
```text
POST_RESIM_GAP_HOLD_LAST player=0 tick=540 from=539 sx=-75
LIGHT_EXCLUSIVE_FRONTIER_INVALIDATE tick=541
```
History was rewritten **after** the poisoned sim. Live @541 re-enters `|sx|≥20` → Linux `tap_x=1…` while Android stayed at `tap_x=254` → FC field diffs @545 → SoftLip status fork @547 → deepen exhaust PEER @544.

`LIGHT_EXCLUSIVE_FRONTIER_INVALIDATE` only clears exclusive **541**; mid-span **540** was saved under local `(0,0)` and live tap state already forked.

## Root cause

`syNetInputMakeLocalFrame` resim miss path invented hard `(0,0)` when sealed/history rows were absent. Remote slots invent hold_last of the prior stick; local slots did not. Light episodes routinely resim past the last feel-0 `LOCAL_PUBLISH`, so the host applied neutral while the guest applied hold_last for the same player.

`POST_RESIM_GAP_HOLD_LAST` already knew the correct donor — it ran too late for the sim that wrote ring + tap counters.

## Fix (`port/net/sys/netinput.c`)

Under `PORT && SSB64_NETMENU`, on MakeLocalFrame resim miss:

1. Walk donors like Resync gap pin (gameplay `t-1`, history `t-1`, last gameplay tip, `last_published`, 32-tick walkback).
2. Invent predicted local hold_last (`RESIM_LOCAL_GAP_HOLD_LAST` log).
3. Last-resort `(0,0)` marked `is_predicted=TRUE` so later feel-0 can promote.

Offline / non-NETMENU unchanged.

## Verify

Rebuild **AppImage and Android APK**, re-soak same shape (dual-stick Dream Land Ness):

- Expect `RESIM_LOCAL_GAP_HOLD_LAST` instead of local `publish_frame … sx=0 sy=0` on light exclusive ticks.
- No FC@~546 with only tap/hold field diffs after a light heal.
- SoftLip TopN should not permanent-fork immediately after that light episode; no deepen-exhaust PEER from this class.
