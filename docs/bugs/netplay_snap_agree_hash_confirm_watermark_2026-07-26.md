# Snap-agree watermark for hash_confirm (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak); follow-on [snap_tick_defer](netplay_hash_confirm_snap_tick_defer_2026-07-26.md) for REPLACE-before-recv  
**Bucket:** frame-commit / invent GGPO tax  
**After:** [FC pairing grid](netplay_frame_commit_pairing_grid_2026-07-26.md); [hash_confirm](netplay_hash_confirm_runway_align_2026-07-26.md)

## Symptom

Soak `seed=1824422950`: FC pairing fixed (`compared=3`) but `skipped_hash_confirm=0`. Ledger micro REPLACE at `sim_now = tick+2` still always rewound (~36 Android ≤12 candidates).

## Root cause

`hash_confirm` needs `LastFrameCommitStateAgreedTick > sim_tick`. Full FC mints on a **120-tick** grid. Wire lateness ≈ 2 → REPLACE fires long before the next FC agreement past that tick. Pairing was necessary; cadence was still wrong for invent tax.

Full FC every tick (~392 B + diverge recovery) is too heavy for LAN.

## Fix

New compact packet **`SYNETPEER_PACKET_SNAP_AGREE` (32)** — originally 36 bytes; **40 bytes** after [wpn hold-aim](netplay_snap_agree_wpn_hold_aim_2026-07-26.md):

| Field | Role |
|-------|------|
| `snap_tick` | completed sim tick |
| `figh/world/item/rng/wpn` | stored subsystem hashes (`wpn` required for Ness PK Hold aim) |

Each live completed tick (post-Go, not resim, not deferred-GGPO-covered):

1. Read local snap hashes → store + send
2. On peer match → `syNetRollbackNoteFrameCommitStateAgreed(snap_tick + 1)` (same watermark units as FC)
3. On mismatch → diag only (`SNAP_AGREE_MISMATCH`); **no** state resim (120-tick FC still owns diverge)

`FRAME_COMMIT_DIAG` gains `snap_agree sent/recv/matched/mismatch`.

Killswitch: `SSB64_NETPLAY_SNAP_AGREE=0` (default on when FC token on).

## Acceptance

Matched APK + Linux:

- `snap_agree matched` ≫ 0 (order of live ticks)
- `skipped_hash_confirm` / `class=hash_confirm` > 0
- Remaining non-flip ≤12 ledger GGPO ↓ vs ~36
- `resim begin`/100 ↓ vs ~9.8 on short soaks
- FC `compared` still > 0; no SNAP_AGREE-driven state resim storms
- 0× JA SoftLip PEER from hash_confirm (740113729 class)

## Related

- [`netplay_snap_agree_wpn_hold_aim_2026-07-26.md`](netplay_snap_agree_wpn_hold_aim_2026-07-26.md) — append `wpn` (Hold invent poison)
- [`netplay_hash_confirm_runway_align_2026-07-26.md`](netplay_hash_confirm_runway_align_2026-07-26.md)
- [`netplay_frame_commit_pairing_grid_2026-07-26.md`](netplay_frame_commit_pairing_grid_2026-07-26.md)
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)
