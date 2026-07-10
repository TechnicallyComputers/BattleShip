# Netplay: Ness PK Thunder wave VFX eff-only FRAME_COMMIT cross-ISA drift

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), soak2 session `38366538` / seed `3309559813`  
**Match:** Captain Falcon (P0) vs Ness (P1), PK Thunder air hold (`status=233`)  
**Status:** FIX IMPLEMENTED (re-soak pending)

## Symptom

```
FRAME_COMMIT_STATE_DIVERGE validation=525
  local  figh=0xDA00E8F0 world=0xD46D73E7 item=0x811C9DC5 rng=0x92C5CCFC eff=0xF93FEF50
  peer   figh=0xDA00E8F0 world=0xD46D73E7 item=0x811C9DC5 rng=0x92C5CCFC eff=0x9F47EF50
  inp_local=inp_peer (MATCH)
```

`figh`, `world`, `item`, `rng` all match — genuine **eff-only** cross-ISA drift during Ness **SpecialAirHiHold** (PK Thunder). Synctest probes pass (17 OK); session stops on frame-commit validation.

## Root cause

Single folded effect: Ness PK Thunder wave shell (`gobj_id=1011`, `respawn=5`, `parent_id=1000`, joint 5, `gcPlayAnimAll`).

`syNetSyncFoldSingleEffectGObj` always hashed `gobj->anim_frame`. During hold, `gcPlayAnimAll` can advance **0/1/2 times per sim tick** cross-ISA (and forward vs resim on the same peer). Soak2:

| tick | Linux anim (forward) | Android anim (forward) |
|------|----------------------|------------------------|
| 520  | `0x41880000`         | `0x41880000` (match)   |
| 521  | `0x41A00000`         | `0x41980000` (first off-by-one) |
| 524  | `0x41B80000`         | `0x41B00000`           |

Frame-commit @525 compared Linux tick-524 eff hash vs Android tick-523 hash (`0xF93FEF50` vs `0x9F47EF50`). PK Thunder **weapon** sim (`wpn`) already matched — wave VFX is presentation-only in hold.

Unrelated to PSI Magnet / Psychic Magnet (hidden cosmetic, `effect_count=0`).

## Fix

| Layer | Change |
|-------|--------|
| **Eff hash fold** | For PK wave shell (`syNetplayLiveEffectIsNessPKWave`): fold `status_total_tics` instead of `anim_frame`; skip world translate (unchanged) |
| **Identity** | `syNetplayLiveEffectIsNessPKWave` + `syNetplayFighterInNessPKWaveSimScope` — joint-5 repin (mirror Psychic Magnet TopN path) |
| **Snapshot blob** | Still captures `anim_frame` for verify round-trip / presentation; only live hash fold changes |

## Re-soak pass criteria

Session `38366538` class: no `FRAME_COMMIT_STATE_DIVERGE` with `fc_div_fields=[eff]` during PK Thunder air hold; `netplay-scan-drift.py` RESULT PASS.
