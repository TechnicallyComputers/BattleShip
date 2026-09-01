#ifndef NETCONSUMPTION_WITNESS_H
#define NETCONSUMPTION_WITNESS_H

#include <sys/netpeer.h>

/*
 * Consumption-mapping witness (REAL-DELAY flip Phase 0 — observe-only).
 *
 * Logs, per admission verdict, the relationship between the sim tick being
 * admitted and the remote wire frontier, so "cushion" becomes a measured
 * series instead of an inference from wire_gap. Also self-checks the current
 * ZERO-DELAY tick-mapping contract (wire = sim + D both directions) once per
 * window, so any drift in the mapping functions is caught before the flip
 * changes them deliberately.
 *
 * Contract + target mapping: docs/netplay_real_delay_contract_2026-08-31.md.
 *
 * Gate: SSB64_NETPLAY_CONSUMPTION_WITNESS
 *   unset/0 — off (default; zero cost beyond one cached getenv)
 *   1       — window summaries every 120 admitted ticks + contract self-check
 *   2       — additionally one line per admitted sim tick
 *
 * No behavior change: read-only observer of the already-computed admission
 * verdict, hooked beside syNetRbeSchedShadowObserve.
 */
#if defined(PORT) && defined(SSB64_NETMENU)
void syNetConsumptionWitnessNote(u32 sim_tick, const SYNetPeerSharedCommitStep *step);
void syNetConsumptionWitnessResetForSession(void);
#else
static inline void syNetConsumptionWitnessNote(u32 sim_tick, const SYNetPeerSharedCommitStep *step)
{
	(void)sim_tick;
	(void)step;
}
static inline void syNetConsumptionWitnessResetForSession(void)
{
}
#endif

#endif /* NETCONSUMPTION_WITNESS_H */
