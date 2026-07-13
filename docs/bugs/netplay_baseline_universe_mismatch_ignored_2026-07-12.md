# Netplay: baseline universe mismatch ignored → PEER_SNAPSHOT_DIVERGE — 2026-07-12

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom (soak1 `608380406`, seed `1122173807`)

Android client / Linux host, Dream Land, Kirby ditto. Synctest clean; drift scan PASS (no FC / load-hash). Session stops on:

```text
PEER_SNAPSHOT_DIVERGE load_tick=426
  peer  figh=0xC1490CBE map=0x0E9C39D5  (Android)
  local figh=0x6301E10E map=0x0949DF1A  (Linux, after overwrite)
  world/item/rng MATCH
```

Preceded by `BASELINE_UNIVERSE_MISMATCH … ignored (no pending resim)` while both sides had already exchanged load-426 baselines with **matching map** but **different figh** (Kirby Pass / status 33).

## Root causes (stacked)

1. **P1 stick phase lag** — Android local (feel-0) applied Pass down-stick one tick earlier than Linux remote. From tick 424, `STICK_SAMPLE` showed `Android[t] ≈ Linux[t+1]`. Gap-filled hold-last priors are not `RemoteStrictConfirmed`, so the feel-0 provisional-replace GGPO path never fired on the real stick REPLACE.

2. **Universe-mismatch abort order** — `TryOpenResimBaselineGateFromPeerDigest` did `ResetBaselineResimState()` **before** `AbortToInputCorrectionFromUniverseMismatch`. Reset cleared `ResimPending`, so the abort logged `ignored (no pending resim)` and dropped the figh fork.

3. **Load-tick ring clobber** — With `sim == load_tick` (426), `SnapshotReadyForBaselineCompare` returned false (`load_tick >= sim`). Echo retry deferred as `snapshot_not_ready` while forward `MpLanding` / catch-up still saved tick 426, overwriting the armed baseline (`map` `0x0E9C39D5` → `0x0949DF1A`, Whispy wait/blink advanced one tick). Final compare then hard-diverged.

## Fix

1. **`netinput.c`** — Treat `RemoteGapFilled` priors like strict confirms for feel-0 replace GGPO; add `late_wire_completed_sim` path when confirmed wire arrives for an already-completed sim tick with a significant stick delta.

2. **`netrollback.c`** — Call `AbortToInputCorrectionFromUniverseMismatch` **without** Reset-first; allow correction when pending was already cleared; reset only after queuing. Same for seal-authority mismatch fallthrough.

3. **`netrollback.c`** — Skip `SavePostTick` for the load_tick while echo-retry is armed or awaiting peer baseline; allow baseline compare when `sim == load_tick` in that state.

## Verify on re-soak

- Dream Land Pass / platform drop cross-ISA: no `BASELINE_UNIVERSE_MISMATCH ignored`; expect `→ input correction` or matched baseline.
- No `SNAPSHOT_SAVE` of load_tick between `BASELINE_ECHO_RETRY_DEFER` and compare that changes `map_hash_save` digests.
- Grep `feel0_gapfill_replace` / `late_wire_completed_sim` on Pass stick onsets after GGPO.
- Prefer continued match over `PEER_SNAPSHOT_DIVERGE` at ~426 with matched world/rng.
