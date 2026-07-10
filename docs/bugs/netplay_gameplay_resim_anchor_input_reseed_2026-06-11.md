# Netplay: gameplay resim anchor probe input reseed @480

**Symptom:** Soak3 tick-480 `FORCE_MISMATCH` resim — intro OK, but anchor walkback 479→449 exhausts with `step_figh_fail=1` / `step_anim_fail=1` → `resim_load_fail` CSS kickback. Yoshi P1 in Pass (`status=33`) after Squat: leg joint AObj drift (`j6_anim_frame live=0` vs blob mid-keyframe). Visible Kirby/Yoshi leg-spin during walkback probe cycles.

**Root causes:**
1. `syNetInputRollbackPrepareForResim` ran only after walkback — fixed: reseed before each probe step.
2. Gameplay probe +1 sim poisons leg AObj like intro Appear (presentation drift, `light_ok=1` `anim_ok=0`) — no gameplay reconcile existed.
3. Step compare at original mismatch tick uses corrected input vs ring captured pre-correction (`FORCE_MISMATCH` btn XOR @480).
4. Failed probe reload left `syNetInputSetTick(probe_tick)` while world restored @load_tick — presentation desync during walkback.

**Fix:**
1. Reseed inputs before each anchor probe step (`PrepareForResim(probe_tick)`).
2. `syNetRbSnapshotReconcileAnchorProbeGameplayFromProbeSlot` — re-pin Pass/Squat/Landing/Dash leg AObj from ring@probe when live status matches (no modelpart push).
3. Use `fhash_light` for all NETMENU anchor probe step figh compares (not intro-only).
4. Skip step compare at `probe_tick == mismatch_tick` when postload matched (input-correction tick).
5. After failed probe reload, hold sim tick at `load_tick` not `probe_tick`.

**Verify:** Re-run soak3 @480. Expect probe pass @479 (or skip @480 + pass @479), resim completes, no CSS kickback, no leg-spin artifact during walkback.
