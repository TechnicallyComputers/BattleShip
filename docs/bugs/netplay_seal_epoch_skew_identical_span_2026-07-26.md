# Seal epoch skew with identical span → SEAL_ROWS_EXHAUSTED (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `503281020` seed `3082600816` — Android client ↔ Linux host (Ness ditto)  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** correction / seal protocol (not `replay_determinism`)  
**After:** [jibaku stick absorb retire](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md) accepted (`real_stick=0`)

## Symptom

Absorb retire OK (0× jibaku absorb skips; `GGPO_CLASS_SUMMARY real_stick=0`). Matched jibaku @693. Live hashes still agree @696/@722. Session dies:

```text
[Linux]   active_epoch=51 SEND slot=0 digest=0x6E1D3365 WAIT missing_slots=0x2
[Android] active_epoch=50 SEND slot=1 digest=0xECD9C13D WAIT missing_slots=0x1
Both:     mismatch=723 target=727 load=722
Both:     EPISODE_SEAL_ROWS_REJECT reason=stale_episode_tuple  (peer epoch ≠ local)
[Linux]   RESIM_SEAL_ROWS_EXHAUSTED … baseline_matched=1 → hard desync → VS stop
```

Same kill shape as [`netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md`](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md), but this soak shows **pure epoch skew** with identical `(mismatch, target, load)` — not digest fork under protocol stick GGPO.

## Root cause

1. Post-jibaku resim churn (incl. `peer_vs_armed wpn=1` bisect deepen) can leave peers with **independent episode epoch counters** for the same correction span.
2. `syNetRollbackEpisodeEpisodeTupleMatches` required `(epoch, mismatch, target)` exact match.
3. `COMPATIBLE_APPLY` only handled same-epoch XOR forks (mismatch XOR target). Epoch-only skew with **both** mismatch and target equal fell through to `stale_episode_tuple`.
4. Fail-closed: `baseline_matched=1` + seal miss → no deepen → `RESIM_SEAL_ROWS_EXHAUSTED`.

## Fix (`PORT && SSB64_NETMENU`)

`netrollback_episode.c`:

- `EpisodeTupleMatches` — when FSM is active and `(mismatch, target)` match local, accept peer seals even if `pkt_epoch != active_epoch`.
- Mark `PeerSealChunkSeen` on same span (not same epoch only).
- Log `EPISODE_SEAL_ROWS_EPOCH_SKEW_APPLY` when the skew path is used.

Does **not** re-add jibaku stick GGPO absorb. Weapon ring fidelity / Hold-style wpn-only baseline absorb during jibaku remains a separate lever if deepen thrash continues.

## Acceptance

Ness ditto soak through jibaku:

- Prefer `EPISODE_SEAL_ROWS_EPOCH_SKEW_APPLY` / normal `SEAL_ROWS_RECV` over `stale_episode_tuple` storms when span matches
- No `RESIM_SEAL_ROWS_EXHAUSTED` from epoch-only skew with `baseline_matched=1`
- Soft `class=protocol` OK; 0× PEER still required

## Related

- [`netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md`](netplay_protocol_ggpo_seal_rows_exhaust_hang_2026-07-25.md) — OPEN sibling (protocol GGPO digest fork)
- [`netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md`](netplay_seal_tuple_fork_asymmetric_stall_2026-07-12.md) — same-epoch compatible apply
- [`netplay_ness_pk_hold_confirmed_aim_weapon_absorb_2026-07-15.md`](netplay_ness_pk_hold_confirmed_aim_weapon_absorb_2026-07-15.md) — Hold wpn-only baseline absorb (jibaku band not covered)
