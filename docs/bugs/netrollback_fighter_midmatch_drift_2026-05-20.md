# NetRollback — mid-match fighter drift under matching inputs (@900 soak)

**Date:** 2026-05-20  
**Status:** INVESTIGATION (diagnostics shipped; root fix pending soak bisect)

## Symptom

Automatch soak with episode FSM + input authority fixes:

- **30** clean frame-commit compares (30→870).
- First `FRAME_COMMIT_STATE_DIVERGE` at **validation 900** (~15 s sim).
- `inp_local == inp_peer` (`0x1042200D`); `commit_token_mismatch=0`.
- `world`/`rng` agree in commit token; **`figh` differs** (host `0x29E88DDB` vs client `0x32886D35` mirrored).
- Live `sim_state_tick` at **899** already split (`host 0x24C9D402` vs `client 0xD73C6C73`) while `world` still `0xB14346A0`.
- Recovery: `BASELINE_UNIVERSE_MISMATCH` @ load 899, `seal_authority_mismatch`, `RESIM_BASELINE_TIMEOUT` (`baseline_matched=0`).
- **Not** `stale_episode_tuple` (improved vs @420 soak).

Remote analog 877–889: `pub_pred=0`, no `analog_onset_predict` — input authority path ruled out for this failure class.

## Root cause (hypothesis)

**Deterministic sim / snapshot fidelity**, not published-input sealing:

1. Fighter microstate diverges in the **870–899** window while commit-input digests stay matched.
2. Load tick **899** snapshot round-trip may be faithful locally but peers disagree on post-load `figh` with agreeing `rng`/`world` → poisoned baseline exchange.
3. Forward replay (`scVSBattleFuncUpdateBattleSimOnly`) may not reproduce the same evolution as the original live ticks 900–902 if hidden state was wrong before load.

## Blob vs hash audit (reference)

| Surface | `SYNetRbSnapFighterBlob` | `syNetSyncHashFighterStructLight` | `syNetSyncHashBattleFightersFull` |
|---------|--------------------------|-----------------------------------|-----------------------------------|
| Top joint translate | `joint_translate[]` | top joint only | all joints |
| `motion_attack_id`, `hitstatus`, jostle | yes | partial / extended in Full | yes |
| Joint `event32`, `gobj_anim_frame` | yes (prior fixes) | no in Light | separate `anim` hash |
| Coupled weapon IDs | yes (2026-05-19) | via gameplay | weapon hash |

Use `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1` + `SNAPSHOT_FIGHTER_FIELD_DIFF=1` to separate **local round-trip failure** vs **cross-peer live drift**.

## Diagnostics added (2026-05-20)

| Env | Log line |
|-----|----------|
| `SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG=1` (+ optional tick window) | `fighter_slot_hash` per player each `sim_state_tick` |
| `SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1` | `fighter_field_diff` on `LOAD_HASH_DRIFT` (figh) |
| `SSB64_NETPLAY_FRAME_COMMIT_DIAG=2` | `FRAME_COMMIT_STATE_DIVERGE_DETAIL` (token vs live) |
| Episode FSM replay log | `EPISODE_REPLAY_DIVERGE` when replay `inp` stable but `figh` steps |
| Baseline recovery | `BASELINE_UNIVERSE_DIFF`, `BASELINE_UNIVERSE_STORM_CAP` |
| Soak preset | `scripts/netplay-midmatch-fighter-soak.env.example` |

## Recovery policy changes

- **`BASELINE_UNIVERSE_MISMATCH`** — **input-poisoned** only when `FindEarliestInputMismatch` ≤ `load_tick` → GGPO. If inputs agree through load (mismatch after load / none) → deeper load / peer resync, not invent `load+1` or re-queue the post-load stick episode. See [`netplay_baseline_universe_state_vs_input_routing_2026-07-13.md`](netplay_baseline_universe_state_vs_input_routing_2026-07-13.md).
- **`seal_authority_mismatch`**: same deeper-load-first path when sealed inputs agree but `figh` differs.
- Baseline retransmit storm cap when the same `(load_tick, peer_figh, local_figh)` repeats (>12).

## Verify

Re-soak with `scripts/netplay-midmatch-fighter-soak.env.example` on both peers:

1. Grep both logs for first tick where `fighter_slot_hash` differs for the same `player`/`status`/`motion`.
2. Correlate with `hash_transition partition=figh`, `mp_collision_tic`, `gcEjectGObj`, `EPISODE_REPLAY_DIVERGE`.
3. Pass: live `figh` aligned through 899 + FC past 900 OR clean `RESIM_POST_MATCH` without `BASELINE_UNIVERSE_STORM_CAP`.

## Related

- [`netinput_remote_analog_authority_2026-05-20.md`](netinput_remote_analog_authority_2026-05-20.md)
- [`netrollback_fighter_snapshot_apply_order_2026-05-19.md`](netrollback_fighter_snapshot_apply_order_2026-05-19.md)
- [`netrollback_episode_input_seal_2026-05-20.md`](netrollback_episode_input_seal_2026-05-20.md)
- [`../netplay_canonical_state_image.md`](../netplay_canonical_state_image.md) §14 (blob vs hash)
