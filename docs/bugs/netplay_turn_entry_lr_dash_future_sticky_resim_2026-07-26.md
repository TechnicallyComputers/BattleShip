# Turn `entry_lr_dash` future sticky → false Dash on resim (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** soak1 session `1929938261` seed `1383043725` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM`

## Symptom

| Signal | Detail |
|--------|--------|
| Pair | PAIRED; both peers soft-stable to ~2040; MATCH UNSTABLE |
| Scan earliest | `TURN_DASH_FORK@654` P1 — soft, GGPO StickReplace heals by 658 |
| Hard kill | `STATUS_FORK@1961` P0 (host Turn `18` / guest Dash `15`) → `PEER_SNAPSHOT` ~1959–1964 **map agree** |
| Red herrings | Flower map-only (0× this soak); hold_last smash_flip diag; SoftLip / Hold gravity |

Label swap: sync `--label host` = Android client, `--label guest` = Linux host.

## Root cause

Not hold_last invent on the kill tick. Stick flip `-84→+80` @1957 did GGPO; Android resim sealed `sx=80`. Peers still forked because **Linux falsely dashed on resim @1957**.

| Pass | Linux P0 @1957 | Android P0 @1957 (sealed +80) |
|------|----------------|--------------------------------|
| First-pass | `allow=1` `lr_dash=0` `did_dash=0` | hold_last `-84` then GGPO |
| Live @1958 | `DashCheckTurn` arms `lr_dash=1`, `NoteEntry(1)` | — |
| Resim @1957 | `harden_lr_dash was=0 now=1 entry=1` → `BRANCH_COMMITTED` **`did_dash=1`** | `entry=0` `did_dash=0` stays Turn |

`sSYNetplayTurnEntryLrDash[]` is a process-local sticky (July 19 InvertLR stomp harden). It is **not** in the fighter snapshot. Live-ahead `DashCheckTurn` @1958 left `entry=1`; GGPO load@1956 + resim@1957 reused that pin via `HardenTurnLrDash` and invented a Dash the first-pass never took. Android never armed entry on the wrong-stick first-pass → stayed Turn → status fork → PEER.

`stick×lr_turn` on allow@1957 is negative (`80×-1`); real arming is @1958 after facing flip — so the resim Dash was purely sticky pollution.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| Note | Stamp `note_tick = syNetInputGetTick()` with each `syNetplayTurnNoteEntryLrDash` |
| Harden | Apply entry pin only when `note_tick <= sim tick`; else witness `harden_lr_dash_skip_future` |
| Load | `syNetplayTurnSyncEntryLrDashAfterLoad` from restored `turn->lr_dash` (or clear) in `ApplyFighterNetplayPost` |
| Get / lr_turn | `GetEntryLrDash` / `HardenTurnLrTurn` use the same usable-at-tick gate |

Offline / non-rollback unchanged. July 19 same-tick stomp harden preserved when `note_tick <= sim tick`.

## Acceptance (re-soak)

Matched APK + Linux binary, grounded InvertLR / dash-dance with stick flip mid-Turn (`SSB64_TURN_DASH_WITNESS=1`):

- No `harden_lr_dash` that invents `did_dash=1` on a tick where first-pass had `did_dash=0` with matching sticks
- Optional `harden_lr_dash_skip_future` when live-ahead Note exists
- No `STATUS_FORK` Turn vs Dash → PEER figh-only from this path after stick GGPO heals
- Soft @654-class Turn/Dash mag miss may still appear; must reconverge (not the kill)

## Related

- [`netplay_turn_lr_dash_stomp_fc_2026-07-19.md`](netplay_turn_lr_dash_stomp_fc_2026-07-19.md) — why entry sticky exists
- [`netplay_turn_lr_dash_statusvars_scrub_synctest_2026-07-26.md`](netplay_turn_lr_dash_statusvars_scrub_synctest_2026-07-26.md) — scrub zeros blob `lr_dash` → SyncAfterLoad Note(0) (opposite load poison)
- [`netplay_hold_last_dash_dance_smash_flip_peer_2026-07-20.md`](netplay_hold_last_dash_dance_smash_flip_peer_2026-07-20.md) — stick-asymmetry Turn entry (different class)
- [`netplay_pupupu_flower_loopstart_repair_anim_map_diverge_2026-07-26.md`](netplay_pupupu_flower_loopstart_repair_anim_map_diverge_2026-07-26.md) — map-only PEER (clean on this soak: `map-only=0`)
