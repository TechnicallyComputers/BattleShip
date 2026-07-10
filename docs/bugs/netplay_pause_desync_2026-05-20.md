# Netplay pause desync (2026-05-20)

## Symptom

Mid-match pause in VS netplay caused hard desync: one peer entered `nSCBattleGameStatusPause` while the other stayed in `Go`. World hash diverged on `game_status` / `time_passed`, rollback tolerated anim-only drift briefly, then `FRAME_COMMIT_STATE_DIVERGE` or partition mismatch. Client-side pause was worse (camera jump, no real pause UI on peer).

## Root cause

Stock pause is **local-only**: `ifCommonBattleGoUpdateInterface` calls `ifCommonBattlePauseInitInterface` immediately on Start, changing `game_status`, camera zoom, and BGM without any network contract. Rollback hashes include `game_status` and pause camera state; asymmetric pause is an immediate shared-state fork.

An earlier fix added a dedicated netpause framework (pause/unpause packets, sim-tick freeze, deferred `gcRunAll`, special FuncRead paths). That model fought the stock engine and caused regressions (stuck sim tick 0, dead pause-menu input, hard lock after pause cycles).

## Fix

Treat **Start like any other synced input** — no netplay pause contract, no sim hold, no input gating:

1. **`decomp/src/if/ifcommon.c`** — Stock Start handling during `Go` and `Pause`; no netplay intercepts. Sim keeps advancing through pause UI (`gcRunAll` every Go update).
2. **`port/net/sc/sccommon/scvsbattle.c`** — Unconditional authoritative sim advance after battle update (same as offline, modulo existing strict-contract gates).
3. **`port/net/sys/netinput.c`** — Full battle FuncRead resolve/publish every tick; no pause-menu bypass.
4. **Removed** `port/net/sys/netpause.c`, pause/unpause wire packets (27/28), `syNetRollbackRewindToPauseBoundary`, and pause episode gating in rollback / frame-commit recovery.
5. **`decomp/src/gm/gmcamera.c`** — Netplay pause PlayerZoom no longer falls back to battle follow when the pausing fighter is out of zoom bounds (camera framing fix retained).

### Follow-up (synced Start timing)

1. **`port/net/sys/netinput.c`** — Local HID sampling ORs `button_tap` into stored frame `buttons` so one-frame Start edges reach published history and wire bundles (pause is input-driven, not side-channel).
2. **`port/net/sys/netrollback.c`** — Defer deferred input/state resim while `game_status` is `Pause` or `Unpause` so pause camera transitions do not trigger baseline-mismatch storms; resim resumes once both peers return to `Go`.
3. **`port/net/sys/netinput.c`** — `syNetInputPublishFrame` same-tick republish (FuncRead then `syNetInputRepublishRemoteHumanControllersForTick`) no longer clears `button_tap` on the remote pausing slot; edge detect uses the prior sim tick and preserves taps already materialized (fixes host missing Start before `ifCommonBattleGoUpdateInterface`).

### Follow-up (pause camera zoom target)

Pause menu stick input follows `sIFCommonBattlePausePlayer`, but rollback snapshot reload resolved `pzoom_fighter_gobj` via `gobj->id`. All fighters use `nGCCommonKindFighter` (1000), so `gcFindGObjByID(1000)` always returned player 0 — camera jumped to the other fighter while pause ownership stayed correct.

1. **`port/net/sys/netrollbacksnapshot.c`** — Capture/apply `pzoom_fighter_player` / `pfollow_fighter_player` (sim slot) instead of relying on kind id alone.
2. **`decomp/src/if/ifcommon.c`** — Reassert `pzoom_fighter_gobj` from the pausing player's `fighter_gobj` at pause init (`nIFPauseKindDefault`).
3. **`port/net/sys/netsync.c`** — Include pause zoom target player in `syNetSyncHashGMCamera`.


Re-run VS netplay (host pause / client pause) and confirm:

- Match starts and sim advances past tick 0 (`frame_commit_diag` present).
- Pause on Start: both peers enter pause on the same tick; sim **does not** freeze.
- Pause menu: stick pan and Start unpause work (normal FuncRead every tick).
- No `SSB64 NetPause:` log lines.
- Repeated pause/unpause cycles without `FRAME_COMMIT_STATE_DIVERGE`.
