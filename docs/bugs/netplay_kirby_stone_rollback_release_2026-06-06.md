# Kirby down-B stone premature release — rollback — 2026-06-06

**Status:** FIX SHIPPED (soak pending — 2026-06-06 follow-up for orphan B tap)  
**Scope:** `decomp/src/ft/ftchar/ftkirby/ftkirbyspeciallw.c`, `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netsync.c`

## Symptom

Netplay soak: Kirby down-B stone (SpecialLw) releases back to normal shortly after touching ground, without intentional B release. Logs showed ground `SpecialLwHold` (262) → `SpecialLwEnd` (263) after ~66 frames with no air-landing status in between.

## Root cause

1. **Down-B entry bleed** — resim can replay a stale B `button_tap` while `button_hold` still has B from stone entry. Vanilla edge detect never emits tap while held; ground Hold (`is_allow_release=TRUE`) then accepts the spurious tap as release.
2. **Duration stomp** — restored `speciallw.duration == 0` while blob still has remaining stone time causes `CheckRelease` to force release on the first Hold tick after rollback load.
3. **Orphan B tap after rollback load** (2026-06-06 soak) — resim can replay a historical B `button_tap` with **no** corresponding `button_hold` on immediate-release stone statuses (ground Hold 262, air Landing 265, air Fall 266). The entry-bleed filter does not cover this case. Observed as release 1–2 ticks after `LOAD_HASH_DRIFT` resim (e.g. ticks 509→511, 629→631).

## Vanilla stone timing (reference)

- `FTKIRBY_STONE_DURATION_MAX` (160): session countdown; forced release at 0.
- `FTKIRBY_STONE_DURATION_MIN` (18 US): minimum hold before B release on air `Unk` path only (`is_allow_release=FALSE`).
- Ground Hold / air Fall / air Landing: immediate B release on genuine tap (`is_allow_release=TRUE`).
- Landing does **not** reset `duration`.

## Fix

| Change | Purpose |
|--------|---------|
| `ftKirbySpecialLwIsGenuineButtonTapB` | During rollback VS/resim, ignore B tap when B is still held (matches vanilla edge semantics; blocks entry bleed without changing release rules) |
| `speciallw.unk_0x2` release suppress | After snapshot apply in stone scope, arm 4-tick B-tap release block (netplay only; cleared on fresh `SetDamageResist`) |
| Vanilla `CheckRelease` paths restored | Ground Hold keeps immediate B release; air Unk keeps min-hold gate |
| `ftKirbySpecialLwReconcileStoneAfterRollback` | After snapshot apply, if live `duration<=0` but blob `duration>0`, restore blob value; sync `is_damage_resist` from blob; arm release suppress |
| Fighter blob `is_damage_resist` | Round-trip stone armor gate with `damage_resist` scalar (was missing — stale FALSE re-fired `SetDamageResist` on air-hold / landing) |
| `ftKirbySpecialLwRollbackShouldSkipFullStoneReset` | During rollback VS/resim, skip full `SetDamageResist` timer/HP reset on mid-hold statuses (Hold/Landing/Fall/AirHold) when countdown active **and** `is_damage_resist` already TRUE (blocks fresh Hold entry from skipping full reset while union bytes still alias prior status) |
| Live + blob light hash | Fold `kirby.speciallw.duration` and `is_damage_resist` when status in SpecialLw..SpecialAirLwEnd (`netsync.c` + blob hash) |
| `SSB64_NETPLAY_KIRBY_STONE_RELEASE_DIAG=1` | Log release reason (`b_tap_allow`, `b_tap_min_hold`, `duration_zero`) |

Offline / non-rollback paths unchanged.

## Verify

- Ground stone under rollback: B tap releases immediately (vanilla) once suppress window expires; no spurious End 1–2 ticks after rollback load.
- Air stone landing/fall: same — no orphan-tap pop immediately after resim.
- Air stone Unk path: B release still gated by `FTKIRBY_STONE_DURATION_MIN`.
- Duration expiry at 0 still forces release after rollback restore.
- **Mid-session landing (Fall→Landing, walk-off→land):** `duration` continues counting down — no jump back to `FTKIRBY_STONE_DURATION_MAX` on ground touch.
- `ring_save_player` live_light matches blob_light for stone ticks (duration + `is_damage_resist` in live fold).
