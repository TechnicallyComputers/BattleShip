# Authoritative symmetric episode contract (2026-05-19)

**Status:** FIX SHIPPED (soak pending)

## Symptoms

- GGPO stick @464: client resim `464→467`, host follower `463→466` after `LOAD_TICK_ADJUST` and frontier clamp.
- Client `rollback_epoch_hold sim=467 cap=464 peer_target=467` while host advances.

## Root cause

Symmetric follower re-derived episode bounds locally (frontier clamp, `follower_local_auth` scan, `LOAD_TICK_ADJUST`) instead of executing the initiator's locked tuple. Initiator also armed `ROLLBACK_SYNC` before final `load_tick` was resolved.

## Fix

1. **`SYNetRollbackPendingEpisode`** — store authoritative `(slot, mismatch, target, load, epoch, flags)` on notify.
2. **Blind follower path** when `SSB64_NETPLAY_ROLLBACK_EPISODE_AUTHORITY` enabled (default).
3. **Authoritative clamp** — no frontier cap on follower; deferred symmetric uses notify target verbatim.
4. **`EPISODE_LOAD_FAIL` / limited rewind** — no silent mismatch shift on follower authoritative loads.
5. **Notify after load** — `ArmSymmetricNotifyEx` from `BeginResim` with final `load_tick` / `epoch_id`.
6. **`ROLLBACK_SYNC` wire** — 34-byte packet adds `epoch_id`, `load_tick` (28-byte legacy still accepted).
7. **`RESIM_POST_MATCH`** — clear peer epoch only when completed local POST key matches peer POST key.
8. **`EPISODE_EXEC` log** — requested vs executed tuple at `resim begin`.

## Verify

Automatch soak with stick @~464: both sides log matching `exec_mismatch` / `exec_target`; `RESIM_POST_MATCH`; no sustained epoch hold past target+slack.
