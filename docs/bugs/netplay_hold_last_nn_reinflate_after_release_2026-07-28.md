# Hold-last last_nn reinflates after confirmed release → SoftLip / FC input skew

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android client ↔ Linux host, Dream Land Ness ditto, seed `2833555211`  
**Logs:** `/mnt/raid0/Software/BattleShip/logs/soak1-{android,linux}.log`  
**Related:** [dual-slot last_nn hot gap](netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md), [local neutral invent](netplay_resim_local_neutral_invent_tap_fork_2026-07-28.md), [soft onset floor/ahead](netplay_hold_last_soft_onset_floor_ahead_peer_2026-07-26.md)

## Symptom

- Clean session end (no PEER) to tick ~1222, but FC@780 `inputs_agree=0` with P0 `topn_tx` / SoftLip first-pass fork (`0x44CF4DF9` vs `0x44C82275`).
- SoftLip last-per-gut matched after FC heal; first SoftLip writes forked from gut ~762.
- Linux P1: `GGPO @756` pred `(0,0)` → wire `(-13,5)` (onset); `GGPO @772` pred `(-41,7)` → wire `(0,0)` (release).

## Smoking gun

After `LEDGER_REFRESH` release @772 (`-41,7` → `0,0`):

```text
REMOTE_PUBLISH … 772 hold_last -41,7
GGPO … 772 published -41,7 | remote 0,0
LEDGER_REFRESH_COMPLETED_SIM_CORRECT … old=-41,7 new=0,0
HISTORY_AUTH_FIRST_WRITE … 774 pred -41,7 hold_last   ← reinflate
… 775/776 still hold_last -41,7 until another LEDGER_REFRESH
```

`syNetInputTryFillFromLastNonNeutral` allowed `last_nn=(-41,7)` through a StrictConfirmed near-neutral `last_confirmed` because `SlotStickHotRecent` stayed true (dual-hot Fix2 exception). Ahead release invent is retired; tick wire for 774 was not present yet → sustained false hold_last → input digest skew + SoftLip TopN on the cliff lip.

## Root cause

[Dual-slot ping-pong Fix2](netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md) correctly keeps `last_nn` across a **one-tick** confirmed `(0,0)` hole so dual-stick invent does not onset-from-zero. It did not cap how long that exception lasts: durable owner release left `last_nn` live for confirm+2… invents.

## Fix (`port/net/sys/netinput.c`)

In `syNetInputTryFillFromLastNonNeutral`, when `last_confirmed` is near-neutral **and** `RemoteStrictConfirmed`:

- Allow `last_nn` fill only when `tick == last_confirmed.tick + 1` (Fix2 one-tick gap).
- Refuse for the confirm tick itself (`lead==0`) and for `lead>=2` (durable release).

Offline / non-NETMENU unchanged. Ahead release/flip invent stays retired.

## Verify

Rebuild AppImage + Android APK, dual-stick Dream Land:

- After remote release LEDGER_REFRESH, expect **no** `hold_last` / `FIRST_WRITE pred` continuing the pre-release stick at confirm+2….
- Fewer FC@~780 class with `inputs_agree=0` + SoftLip first-pass TopN split from this release reinflate.
- Dual-stick mash: Fix2 one-tick neutral hole should still avoid onset-from-zero storms (lead==1 allow retained).
