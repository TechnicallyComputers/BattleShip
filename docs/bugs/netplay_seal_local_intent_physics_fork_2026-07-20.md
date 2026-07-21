# Local seal intent asym → PHYSICS_FORK (2026-07-20)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `1857971875` seed `4002696012` — Android client ↔ Linux host (scan labels swapped)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** PHYSICS_FORK@420 P0 Jump → PEER storm + RESIM_STICK_FORK cascade

## Symptom

| Signal | Detail |
|--------|--------|
| Earliest | `PHYSICS_FORK_ONSET gut=420` P0 `topn_tx` (host `0x43aecccc` \| guest `0x43b075c2`) |
| SoftLip | First `post_ceil@420` **matched** both peers; Linux **second** pass diverged after epoch 12 |
| Seal | Android `SEAL_LEDGER_INTENT_OVERRIDE` P0@419 sealed=`(73,49)` \| ledger=`(-75,-41)` |
| Stomp | `REMOTE_PUBLISH_SEAL_OVERRIDE` P0@419 confirmed=`(-75,-41)`→sealed=`(73,49)` |
| Stick | Linux `STICK_SAMPLE@419` first `(-75,-41)` then resim `(73,49)` (== live `@421`) |
| Cascade | Many `RESIM_STICK_FORK`, `TURN_DASH@456`, `STATUS_FORK@487` |

Epoch 12 was a **P1** correction (45→55) that co-sealed P0 `[419,422)`.

## Root cause

1. **Seal pack poison on gap tick** — Linux never `LOCAL_PUBLISH` P0@419 (418→420). Epoch 12 packed P0@419 as opposite smash `(73,49)` (live@421) instead of owner truth `(-75,-41)`. Prior `SEAL_PACK_PREFER_GAMEPLAY` could amplify restage/gap poison; latch skip alone was insufficient when Transmitted disagreed with published History.

2. **INTENT_OVERRIDE remote-only** — Android (remote P0) applied ledger during sealed resim → SoftLip stayed matched. Linux (local P0) applied the sealed row blindly → PHYSICS_FORK@420.

3. **SEAL_OVERRIDE undid the heal** — After commit, `store_published_api` stomped Android confirmed `(-75,-41)` back to sealed `(73,49)` → RESIM_STICK_FORK storm.

## Fix (`port/net/sys/netinput.c`)

| Layer | Change |
|-------|--------|
| Seal pack | Order: History-over-Transmitted on intent disagree → transmitted → history → gameplay; never latch; always-on `SEAL_PACK` / `SEAL_PACK_PREFER_HISTORY` / `SEAL_PACK_SKIP_LATCH`. Dropped gameplay-over-wire prefer. |
| Sealed resim | `SEAL_LEDGER_INTENT_OVERRIDE` for **local** slots too (wire / history / gameplay truth). |
| Publish write-once | `REMOTE_PUBLISH_SEAL_OVERRIDE_REFUSE_INTENT` when sealed vs confirmed dash-gate / opposite intent — keep confirmed. |
| Scan | Surfaces refuse as `~~ SEAL_OVERRIDE_REFUSE_INTENT`; STOMP regex no longer matches refuse lines. |

## Acceptance (re-soak)

Dual-stick both peers through Jump / dash-dance:

- No PHYSICS_FORK where SoftLip first-pass matched then local seal flipped stick sign
- `SEAL_PACK` lines show `history` / `transmitted` for gap ticks, not live smash from a later tick
- `SEAL_OVERRIDE_REFUSE_INTENT` (or zero STOMP) when sealed opposite-intent vs confirmed
- Local + remote both log `SEAL_LEDGER_INTENT_OVERRIDE` on poisoned seals

## Related

- [`netplay_seal_pack_latch_turn_dash_soft_nz_2026-07-20.md`](netplay_seal_pack_latch_turn_dash_soft_nz_2026-07-20.md) — latch pack + remote-only intent override
- [`netplay_seal_ledger_resim_stick_fork_2026-07-19.md`](netplay_seal_ledger_resim_stick_fork_2026-07-19.md) — seal vs ledger class
