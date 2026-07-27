# Post-jibaku predicted micro GGPO → Hold extend → vertical relaunch (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `537887313` seed `2908879106` — Android client ↔ Linux host (Ness ditto)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** input contract / resim reconstruct  
**After:** [jibaku stick absorb retire](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md)

## Symptom

First-pass PK Thunder self-hit launches in the aimed direction; a short completed-sim stick GGPO through the entry tick re-triggers jibaku nearly straight down (both peers).

| Pass | Tick | `jibaku_launch_dist` | `vel_air` |
|------|------|----------------------|-----------|
| First (good) | 1636 | `(232, 102)` | `(183, 80)` |
| Post-resim (corrupt) | 1640 | `(-6, -27)` | `(-43, -195)` |

Same shape earlier: Android-only early trigger @1453, then both @1455 with steeper `dist`.

## Timeline (1636 class)

| Step | Detail |
|------|--------|
| First-pass @1636 | Both peers jibaku with matched head/launch; Android invent `hold_last (79,23) pred=1` |
| Wire @ sim_now=1639 | `(78,26)` — Δ(1,3) inside `micro_db=3` |
| StickReplace | Predicted → never micro-skips ([740113729](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md)); `hash_confirm` dead (`FRAME_COMMIT_DIAG compared=0`) |
| GGPO | `mismatch=1636 load=1635 target=1640` |
| Resim @1636 | `hold_tick` with head in box, **no `jibaku_collide`** — Hold extends |
| Resim 1637–1639 | Head orbits onto Ness |
| Live @1640 | Late collide → vertical launch (both peers) |

## Root cause

1. Absorb retire correctly removed live jibaku status gates from GGPO stick.
2. Predicted completed-sim micro still always rewinds; FC pairing/`hash_confirm` did not arm (`compared=0`).
3. Short resim through the entry tick fails to re-fire self-hit; Hold continues and corrupts launch geometry.

This is **not** mid-Hold aim freeze — first-pass launch was correct; the GGPO *destroys* it.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| Snap peek | `syNetRbSnapshotGetFighterStatusIdAtTick` — read-only status from committed ring blob |
| StickReplace | Predicted same-intent ≤`micro_db`: Promote-only when snap@`sim_tick` is already jibaku/bound (`class=snap_post_jibaku_micro`). Material REPLACE / Hold-aim still rewind. **Not** live-status absorb. **Deepened:** [same-intent continuity](netplay_jibaku_post_launch_same_intent_continuity_2026-07-26.md) — ≤`continuity_db` (default 12); class `snap_post_jibaku_same_intent`. |
| CheckCollide | On rollback semantics: reacquire dead/stale `pkthunder_gobj` and clear false `is_thunder_destroy` before failing self-hit |

Offline unchanged. Mid-Hold stick REPLACE before entry still short-resims.

## Acceptance

Matched APK + Linux, Ness ditto Hold→jibaku with late micro wire after entry:

- `GGPO stick replace skipped class=snap_post_jibaku_micro` for (79,23)→(78,26)-class after first-pass jibaku snap
- No second `jibaku_trigger` with near-vertical `dist` from that class
- Mid-Hold material aim REPLACE before entry still `resim begin` (span ≪ 20)
- 740113729 JA predicted micro (non-jibaku snap) still rewinds
- Telemetry: `GGPO_CLASS_SUMMARY … skipped_snap_post_jibaku_micro=`

## Related

- [`netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md`](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md) — absorb retired; residual vertical relaunch
- [`netplay_jibaku_ledger_refresh_stick_resim_launch_2026-07-26.md`](netplay_jibaku_ledger_refresh_stick_resim_launch_2026-07-26.md) — prior absorb-hole era
- [`netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md`](netplay_hold_last_micro_skip_predicted_peer_2026-07-26.md) — predicted bare micro skip banned
- [`netplay_hash_confirm_runway_align_2026-07-26.md`](netplay_hash_confirm_runway_align_2026-07-26.md) — FC agree Promote-only (still preferred when `compared>0`)
- [`netplay_frame_commit_pairing_grid_2026-07-26.md`](netplay_frame_commit_pairing_grid_2026-07-26.md) — `compared=0` still observed this soak
