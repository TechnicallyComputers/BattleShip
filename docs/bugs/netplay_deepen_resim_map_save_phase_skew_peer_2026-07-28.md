# Deepen resim SavePostTick phase skew → map-only PEER (inputs agree)

**Date:** 2026-07-28  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** ROOT CAUSE IDENTIFIED (`PORT && SSB64_NETMENU`, fix pending)  
**Soak:** Android client ↔ Linux host, Dream Land Ness ditto, seed `116880861`  
**Logs:** `/mnt/raid0/Software/BattleShip/logs/soak1-{android,linux}.log`  
**Kill:** `PEER_SNAPSHOT_DIVERGE` @load **3366** — `figh` match, **`map`+`anim`**, `agree_through_load=1`  
**Related:** [post-resim live save without battle](netplay_post_resim_live_save_without_battle_map_skew_2026-07-16.md), [light exclusive poison](netplay_light_exclusive_frontier_poison_load_2026-07-28.md), [Pupupu ground_fold / Whispy](netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md)

## Symptom

Session survived hold_last reinflate / FC SoftLip storms to ~3366, then Android:

```text
RESIM_BASELINE_MISMATCH … failed_load=3366 peer map=0x2AC235CF | local map=0x887BB4D8 — PEER_SNAPSHOT_DIVERGE
PEER_DIVERGE_DIFF partition=map
PEER_DIVERGE_DIFF partition=anim
agree_through_load=1 class=replay_determinism
```

Live `sim_state_tick` `mph` matched through 3365 when both logged. FC `inputs_agree=0` count for the session: **0**.

## Smoking gun — not Whispy content, ring phase

`pupupu_ground` at the first ring DIFF (and at kill) differs only by **one wait/blink tick**:

| Tick | Linux ring (after broken deepen) | Android ring (after good deepen) |
|------|----------------------------------|----------------------------------|
| 3366 | `ww=112 blink=28` `map=0x2AC235CF` | `ww=111 blink=27` `map=0x887BB4D8` |
| 3367 | `ww=111 blink=27` `map=0x887BB4D8` | (ahead) |

`Linux[t] == Android[t-1]` for Pupupu counters — classic **+1 map phase skew**. Flowers/`fl_b` matched (`5/15`); blink/wind_wait alone explain `ground_fold`.

## Timeline (deepen pair around 3353)

1. **Trigger (not map):** `BASELINE_UNIVERSE_MISMATCH` @load **3353**, **map matched** (`0x569FE9E3`), **figh diverged** (P0 status 29 vs 33 SoftLip cliff). `inputs_agree=1` → state deepen.
2. Peers run deepen `load=3353 mismatch=3354 target≈3366` **twice** (asymmetric ownership / target width).
3. **Good deepen path** (Linux #1, Android #2):
   - `map_hash_save tick=3353` = load state (`blink=40`)
   - `map_hash_save tick=3354` = advanced (`blink=39` / `0xD91230CC`)
   - Exclusive complete `post mph=0x2AC235CF` → live `SavePostTick(3366)=0x887BB4D8`
4. **Broken deepen path** (Android #1, Linux #2):
   - **No** load-tick save; first save is `tick=3354` with **load** state (`blink=40` / `0x569FE9E3`)
   - Entire `[3354, exclusive)` stream is labeled **+1** vs true post-battle state
   - Complete `post mph=0x566E7F62` → live `SavePostTick(3366)=0x2AC235CF` (true-3365)
5. After the second round, **Linux keeps the broken ring** and announces baseline `map=0x2AC235CF` @3366; **Android has the good ring** `0x887BB4D8` → deepen exhaust PEER.

## Root cause (contract)

Resim contract: after `LoadPostTick(load)`, `SavePostTick(T)` for `T > load` must capture state **after** a battle step that completed `T`.  

Broken deepen violates that: first `SavePostTick(mismatch)` commits **pre-advance load world** under the mismatch index. Downstream exclusive frontier + baseline compare then disagree by one Pupupu phase even when fighters later hash-match and inputs agree.

This is the same *family* as [post-resim live save without battle](netplay_post_resim_live_save_without_battle_map_skew_2026-07-16.md) (label post-(T−1) as T), but the onset here is **inside deepen/resim SavePostTick**, not the live Await/SamePass path — and it is **asymmetric across peers** when one deepen attempt takes the good path and the other the bad path.

SoftLip figh @3353 is only the deepen *stimulus*. Patching Whispy blink / flower / map hash with more context would not fix the mis-tagged ring.

## Contract-effective fix (do not band-aid PEER)

Prefer invariants over “if Dream Land / if blink Δ=1 allow deepen”:

1. **Resim save gate:** During `ResimPending`, refuse `SavePostTick(T)` for `T == mismatch` (or any `T > load`) unless a post-load battle completion for `T` is proven (same spirit as `AllowLivePostBattleSave` / `live_battle_sim_ran`). If `GetTick` was bumped to `mismatch` without `gcRunAll`, do not save.
2. **Load-slot identity check (diag → hard):** If saving `T > load` and live Pupupu `(wind_wait, blink)` (or full `hash_map`) still equals the load slot, log `RESIM_SAVE_PHASE_STALE` and skip/defer — never publish that slot as baseline.
3. **Baseline phase detect:** If peer/local map disagree but `peer_map == local_map_at(load-1)` / Pupupu blink Δ=1 with figh match, classify `PEER_MAP_PHASE_FORK` and re-deepen from a proven load rather than immediate `PEER_SNAPSHOT_DIVERGE` (recovery aid; primary fix remains (1)).

Do **not** widen SoftLip suppress, ignore map in baseline, or special-case seed/tick windows.

## Verify

Re-soak Dream Land dual-stick after the save gate:

- Every deepen: first `map_hash_save` at `mismatch` must show `blink = load_blink - 1` (Wait countdown), never equal to load blink.
- No sustained `Linux[t]==Android[t-1]` Pupupu stream after deepen.
- SoftLip figh @~3353 may still FC-recover; map baseline at exclusive frontier must match when `agree_through_load=1`.
