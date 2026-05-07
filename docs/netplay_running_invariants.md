# Netplay: invariants after `RUNNING`

Once the VS session phase reaches **RUNNING** (see `syNetPhaseIsRunning` in `port/net/sys/netphase.c`):

1. **No tick-grid `FeedDeviation` mutations** — `syNetTickGridLockFeedDeviation` only applies latch bias and scale state when `syNetPhaseAllowsTickGridFeedDeviation()` is true (bounded **CALIBRATING** window after the battle barrier). After `RUNNING`, non-zero D is ignored (rate-limited log); D == 0 may still mark the guest lock converged without feeding bias.

2. **No automatic cadence / tick-grid retuning** — startup calibration is one-shot (optional wall-clock budget via `SSB64_NETPLAY_TICK_GRID_CALIBRATE_MS`). There is no live `FeedDeviation` loop or cadence self-tuning during steady match play.

3. **Strict exec gate and skew / decouple bypass** — `SSB64_NETPLAY_TICK_GRID_EXEC_GATE`, skew pacing bypass, and decouple bypass for tick-grid apply only when `syNetPhaseIsRunning()` is true, so calibration and pre-running paths are not held to guest lock or bypass rules meant for steady play.

4. **Corrections after go** — rollback remains the mechanism for post-start divergence; deterministic state plus immutable inputs are the primary contract, not continuous grid chasing.
