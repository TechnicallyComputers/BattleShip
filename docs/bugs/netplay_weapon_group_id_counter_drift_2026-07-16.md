# Netplay: weapon group-id mint counter drifts across rollback histories → wpn-only PEER_SNAPSHOT_DIVERGE (2026-07-16)

## Symptom

Soak session `1068427999` (Android host-role=client, Linux guest-role=host, Ness dittos, seed
`2871220968`) stopped at `load_tick=1190` with a weapon-partition-only baseline divergence:

```
SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE load_tick=1190
  peer  figh=0xDD324112 ... wpn=0x97DCFB7F map=0x83B0F680 cam=0xB1CD5A92
  local figh=0xDD324112 ... wpn=0x1F789064 map=0x83B0F680 cam=0xB1CD5A92
SSB64 NetRollback: PEER_DIVERGE_DIFF load_tick=1190 partition=weapon peer=0x97DCFB7F local=0x1F789064
```

Every other partition (figh/world/item/rng/anim/map/cam) matched exactly. `netplay-scan-drift.py`
reported PASS (no LOAD_HASH_DRIFT, no synctest fails); only the sync-report flagged
`PEER_SNAPSHOT_DIVERGE x1` on the guest. The Android side had already absorbed the mismatch via
`RESIM_BASELINE_PK_HOLD_WPN_ONLY_ABSORB` (Hold-aim weapon-fragile path), but the Linux side
hard-stopped the session.

Timeline: P1 Ness entered PK Thunder Hold (status 233) around tick 1188 (head weapon spawned,
`weapon_count` 0→1). At tick 1190 the first trail segment spawned (`weapon_count` 1→2). Both peers
had agreed on the armed baseline at `load_tick=1188` (`weapon=0x4643A6CB` on both). The fork
appeared exactly at the trail-0 spawn tick, with identical inputs and identical resim episodes
(14/14 on both peers, same load/mismatch ticks).

## Root cause

`wpNessPKThunderTrailMakeWeapon` assigns the first trail segment a fresh group id:

```c
if (trail_id == 0)
{
    trail_wp->group_id = wpManagerGetGroupID(...);   /* sWPManagerGroupID++ */
}
```

`sWPManagerGroupID` (decomp `wpmanager.c`) is a monotonically increasing global counter. It is:

- **folded into the wpn rollback hash** — `syNetSyncHashActiveWeaponsForRollback` folds
  `wp->group_id` per weapon;
- **gameplay-relevant** — `wpprocess.c` uses group-id equality to suppress weapon-vs-weapon
  collision within a group;
- **not snapshotted** — unlike `rng_seed` (round-trips through `SYNetRbSnapWorldBlob`) and
  the weapon `instance_id` counter (netmenu-only, reset via `wpManagerResetInstanceIds`), the
  group counter had no save/restore anywhere in the rollback layer.

The blob apply path *does* overwrite each matched/respawned weapon's `group_id` from the blob
(`syNetRbSnapApplyWeaponBlobToGObj`), so live weapons always carried correct ids — but the
**counter itself stayed advanced**. Every synctest probe or rollback load that respawns a weapon
from a blob goes through the normal `wpManagerMakeWeapon` / kind-specific spawn path, which mints
a group id before the blob overwrite. Those mint events are *not* cross-peer deterministic: probe
cadence, load-fail retries, deepen passes, and resim-follower vs initiator asymmetries all bump
the counter different numbers of times on each peer.

Consequence: the next *forward-sim* (deterministic, gameplay) mint — here PK Thunder trail 0 at
tick 1190 — returned different group ids on the two peers. Same weapon, same position, same
lifetime, different `group_id` → wpn fold forks → `PEER_SNAPSHOT_DIVERGE`.

## Fix

Round-trip the counter with the world blob, exactly like `rng_seed` (all netmenu-gated):

1. **Accessors** (`decomp/src/wp/wpmanager.c` / `.h`, inside the existing
   `PORT && SSB64_NETMENU` block): `wpManagerGetGroupIdCounter()` /
   `wpManagerSetGroupIdCounter()` (0 is normalized to 1, matching the wraparound skip in
   `wpManagerGetGroupID`).
2. **Snapshot field** — `SYNetRbSnapWorldBlob.wp_group_id_counter`.
3. **Capture** — in `syNetRbSnapCaptureWorld`, and re-captured at the end of slot fill at the
   same instant as the `rng_seed` refold / `hash_weapon` fold (repairs between CaptureWorld and
   the tail folds can mint ids).
4. **Restore** — in `syNetRbSnapApplyWorld` (rollback load), and re-pinned at the end of
   `syNetRbSnapshotFinalizeLoadFromSlot` next to the `rng_seed` re-restore, so group ids minted
   by load-finalize respawns cannot leak into the post-load counter.

With the counter pinned to the slot, forward resim replays mint the same ids as the original
forward pass, and probe/load traffic can no longer skew the counter — both peers mint identical
group ids for the same deterministic spawn.

## Why the absorb path masked it on one side only

The Android peer classified the mismatch as Hold-aim weapon-fragile
(`RESIM_BASELINE_PK_HOLD_WPN_ONLY_ABSORB`) and adopted the peer wire weapon hash into its armed
baseline. The Linux peer hit the echo/compare path first and failed hard. The absorb heuristic
exists for transient Hold-aim quantization noise; a persistent group-id fork is exactly the kind
of real divergence it should not paper over. With the counter snapshotted the fork no longer
occurs, so no change to the absorb heuristic is needed.

## Verification

- `netmenu` target builds clean.
- Re-soak the Ness-ditto PK Thunder scenario: expect no wpn-only `PEER_SNAPSHOT_DIVERGE` at
  Hold trail-spawn ticks and `netplay-trim-logs.py --sync-report` MATCH: STABLE.

## Related

- `netplay_ness_pkwave_jibaku_eff_fold_dropout_2026-07-16.md` — same session class, effect-hash
  fold scope + livecap deadlock (previous soak).
- `netplay_ness_pkthunder_jibaku_defer_synctest_2026-07-15.md` — PK Thunder weapon ring
  preservation during jibaku deferred teardown.
