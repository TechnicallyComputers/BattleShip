# SNAP_AGREE must include wpn (Ness PK Hold aim) (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** `BASELINE_UNIVERSE_MISMATCH` / `class=replay_determinism` after invent hash_confirm  
**After:** [snap_agree watermark](netplay_snap_agree_hash_confirm_watermark_2026-07-26.md); invent hash_confirm stack

## Symptom

Soak `seed=3042743425` (~1340 ticks): invent tax low (`confirm→GGPO` 0/12; FORCE path OK; resim ~1.7/100), then late kill:

- `BASELINE_UNIVERSE_MISMATCH` load_tick≈1331, inputs agree through load
- p0 Ness `status=236` / `SpecialAirHiJibaku` (air jibaku), figh skew vs peer
- Preceded by material LEDGER GGPO cluster through Hold, then hash_confirm Promote-only micros @1319–1325

## Root cause

1. Through tick **1318**, `figh` and `wpn` matched after prior GGPO heal.
2. At **1319** (still Ness `status=233` / `SpecialAirHiHold`): **`wpn` forked while `figh`/`world`/`item`/`rng` still matched** for 7 ticks.
3. Hold stick invent (`-75,39` → `-74,44` …) was **hash_confirm Promote-only** because SNAP_AGREE only compared figh/world/item/rng — thunder **head aim follows stick**, so predicted vs authority stick skews `wpn` without moving fighter hash.
4. At **1326** jibaku launch (`status=236`): self-hit / launch from divergent head → lasting `figh` fork → next material GGPO @1332 loads a forked baseline → `BASELINE_UNIVERSE` deepen storm + `stale_episode_tuple` noise.

Not a Kirby CopyFox collision: `fkind=11` is Ness; 233/236 are Hold → air jibaku.

## Fix

Extend `SYNETPEER_PACKET_SNAP_AGREE` to **40 bytes** (was 36): append `wpn` after `rng`. Match requires `figh/world/item/rng/wpn`. Mismatch still diag-only (FC grid owns state dive); effect is **no watermark advance** → hash_confirm will not skip GGPO across a wpn-only fork.

Killswitch unchanged: `SSB64_NETPLAY_SNAP_AGREE=0`.

## Acceptance

Matched APK + Linux (prefer Ness ditto / PK Thunder soaks, seed `3042743425` if reproducible):

- SNAP_AGREE wire size 40; `SNAP_AGREE_MISMATCH` logs include `wpn=`
- During Hold stick invent: when `wpn` would diverge, prefer `HASH_CONFIRM_DEFER_RESOLVE class=rewind` / short GGPO over Promote-only across wpn skew
- No `BASELINE_UNIVERSE` immediately after Hold→jibaku from this class
- `skipped_hash_confirm` still > 0 when all five hashes match (tax not fully undone)

## Related

- [`netplay_snap_agree_hash_confirm_watermark_2026-07-26.md`](netplay_snap_agree_hash_confirm_watermark_2026-07-26.md)
- [`netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md`](netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md)
- [`netplay_ness_pk_hold_skip_durable_aim_fork_2026-07-15.md`](netplay_ness_pk_hold_skip_durable_aim_fork_2026-07-15.md)
