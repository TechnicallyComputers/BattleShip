# Netplay: Pupupu Whispy blink lockout → FRAME_COMMIT rng — 2026-07-12

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom (soak1 `1739730420`, seed `4192058273`)

Android client / Linux host, Dream Land (`gkind=6`), Kirby ditto. Pair OK. Synctest 4 OK / 0 FAIL / 330 intro skips. No `LOAD_HASH_DRIFT`.

Symmetric `FRAME_COMMIT_STATE_DIVERGE @600` **`diverged=rng` only**, **inputs MATCH**, figh/world/item/eff agree.

```
Android local rng=0x37636156  game_seed=0x8C725AD7
Linux   local rng=0x1EA32021  game_seed=0xCB75AA3E
rng_hash_walk archived_tick=592 count=1 seed_after=0xCB75AA3E  (both peers)
```

## Root cause

1. **`grPupupuWhispyUpdateBlink`** decrements `whispy_blink_wait` only while `map_gobj[0]->anim_frame` is ended. After a blink fires at wait `0`, wait stays at `0` during the eyes anim, then counts `-1 … -9`, and at `-10` reseeds via **`syUtilsRandIntRange` (gameplay LCG)** and may stretch the mouth.

2. Cross-ISA leftover on eyes `anim_frame` can flap **above** the near-zero snap while wait is in `(-10, 0)`. One peer freezes at **`blink=-9`** (gate closed) and **never consumes** the `-10` reseed; the other hits `-10` and advances `gSYMainRandom`.

3. Soak evidence (first `pupupu_ground` per tick): both sides roll `blink=244` at 592 on one timeline, then Android’s FC path stays at `blink=-9` through 600 while Linux holds `244`. Fighter hashes still match because blink is presentation; FC’s `rng` partition does not.

## Fix

Under `PORT && SSB64_NETMENU` + `syNetplayRollbackSemanticsActive()`:

1. **Post-blink lockout** — while `-10 < whispy_blink_wait < 0`, keep decrementing even if eyes `anim_frame` is not ended (still require `whispy_eyes_status == -1`). Positive waits and the `0` hold during eyes anim still require `syNetplayMapGobjAnimFrameEnded(map_gobj[0])`.

2. **ForcedCosmetic reseed** — blink wait init + `-10` reseed use `syUtilsRandIntRangeForcedCosmetic`. Wind wait/duration remain on the game LCG.

## Verify on re-soak

- Dream Land cross-ISA through first blink cycle after Go (~tick 580–600): no FC `diverged=rng` with matched inputs.
- `pupupu_ground` should not show one peer stuck at `blink=-9` while the other is on a fresh positive wait with matching wind_wait.
- Wind onset timing (game RNG) should still agree.

Follow-up (same day): Open→Blow one-tick skew after GGPO — [`netplay_pupupu_whispy_open_blow_rng_fc_2026-07-12.md`](netplay_pupupu_whispy_open_blow_rng_fc_2026-07-12.md).
