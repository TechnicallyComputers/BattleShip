# netplay_delay_sync_starvation_wire_mismatch (2026-05-09)

**RESOLVED** — With `SSB64_NETPLAY_DELAY_SYNC_STARVATION_HANDLER` and match-linked delay, sessions could latch starvation on a bar **`hr` could never satisfy** relative to how strict admission actually chose the wire row, then never clear the latch (felt like a hard lock). A separate symptom was a small early hitch when starvation counters ran during the same ticks as **startup admission grace** (readiness returns TRUE without enforcing the blended frontier).

**Root cause:** `syNetInputStrictReadyCached` passed `required_wire = syNetPeerGetStrictRequiredWireTick(sim)` (strict ceiling `sim + D + slack`) into `syNetPeerMatchDelayStarvationUpdateAndShouldHold`, while `syNetPeerIsRemoteInputReadyForSimTick` after grace uses **`min(max(sim + D, hr), sim + D + slack)`**. Starvation underrun was therefore keyed off the cap, not the effective row.

**Fix:** Shared helpers in `port/net/sys/netpeer.c`: `syNetPeerStartupAdmissionGraceTicks`, `syNetPeerEffectiveWireFrontierFromHr`, public `syNetPeerGetEffectiveWireFrontierForAdmission`. Readiness uses the same effective row; starvation does not arm during startup grace and clears partial enter counters there. `port/net/sys/netinput.c` uses the effective frontier for strict cache `required_wire`, starvation, and `delay_sync_diag` (`wire_cap` vs `wire_eff`).

**Audit:** Any code comparing `hr` to “required wire” for strict VS must use the same frontier as `syNetPeerIsRemoteInputReadyForSimTick` after grace.
