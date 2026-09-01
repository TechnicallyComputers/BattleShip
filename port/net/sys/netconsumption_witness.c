/*
 * Consumption-mapping witness — REAL-DELAY flip Phase 0 (observe-only).
 * See netconsumption_witness.h and docs/netplay_real_delay_contract_2026-08-31.md.
 *
 * Definitions (current ZERO-DELAY contract, netpeer.c):
 *   required_wire(T) = T + D          (syNetPeerGetBaseRequiredWireTick)
 *   decode(w)        = w - D          (syNetPeerDelaySimTickFromWire)
 *   cushion(T)       = hr - required_wire(T)
 * where hr is the highest remote wire row held at admission time. cushion >= 0
 * means the remote row was already present when demanded (ring admit);
 * cushion < 0 is the prediction depth the sim ran at for that tick. Under the
 * flip, required_wire(T) becomes T and the same series stays comparable.
 */
#if defined(PORT) && defined(SSB64_NETMENU)

#include <sys/netconsumption_witness.h>
#include <sys/netpeer.h>
#include <sys/netrollback.h>
#include <sys/netsession_params.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);
extern void port_log(const char *fmt, ...);

#define SYNETCW_WINDOW_ADMITTED_TICKS 120U

static s32 sSYNetCwMode = -1; /* -1 unparsed, 0 off, 1 summary, 2 per-tick */

/* Window accumulators (admitted ticks only; hold frames counted separately). */
static u32 sSYNetCwWindowAdmitted;
static u32 sSYNetCwWindowRing;
static u32 sSYNetCwWindowPredict;
static u32 sSYNetCwWindowHoldFrames;
static s64 sSYNetCwWindowCushionSum;
static s32 sSYNetCwWindowCushionMin;
static s32 sSYNetCwWindowCushionMax;
static u32 sSYNetCwWindowFirstTick;
static u32 sSYNetCwLastAdmittedTick;

static void syNetCwResetWindow(void)
{
	sSYNetCwWindowAdmitted = 0U;
	sSYNetCwWindowRing = 0U;
	sSYNetCwWindowPredict = 0U;
	sSYNetCwWindowHoldFrames = 0U;
	sSYNetCwWindowCushionSum = 0;
	sSYNetCwWindowCushionMin = 0x7FFFFFFF;
	sSYNetCwWindowCushionMax = -0x7FFFFFFF;
	sSYNetCwWindowFirstTick = 0U;
}

void syNetConsumptionWitnessResetForSession(void)
{
	syNetCwResetWindow();
	sSYNetCwLastAdmittedTick = 0U;
	/* Re-read the env each session so soak scripts can flip it between matches. */
	sSYNetCwMode = -1;
}

static s32 syNetCwGetMode(void)
{
	if (sSYNetCwMode < 0)
	{
		const char *env = getenv("SSB64_NETPLAY_CONSUMPTION_WITNESS");

		sSYNetCwMode = 0;
		if ((env != NULL) && (env[0] != '\0'))
		{
			s32 v = atoi(env);

			if (v > 0)
			{
				sSYNetCwMode = (v > 2) ? 2 : v;
			}
		}
		if (sSYNetCwMode > 0)
		{
			port_log("SSB64 NetCw: consumption witness armed mode=%d window=%u\n",
			         (int)sSYNetCwMode, (unsigned int)SYNETCW_WINDOW_ADMITTED_TICKS);
		}
	}
	return sSYNetCwMode;
}

/*
 * Self-check the current mapping contract at a live tick. Any violation means
 * the encode/decode pair in netpeer.c no longer agrees with the documented
 * ZERO-DELAY arithmetic — that must be a deliberate flip, never drift.
 */
static void syNetCwContractCheck(u32 sim_tick)
{
	u32 d;
	u32 wire;
	u32 back;
	u32 want_wire;

	d = syNetPeerGetCommittedInputDelay();
	wire = syNetPeerDelayWireTickFromSim(sim_tick);
	back = syNetPeerDelaySimTickFromWire(wire);
	/* Mode-aware law (updated with the Phase 1 flip in the same commit that changed
	 * the mapping): REAL-DELAY encode/decode are identity; ZERO-DELAY is +/-D. */
	want_wire = (syNetSessionParamsRealDelayActive() != FALSE) ? sim_tick : (sim_tick + d);
	if ((wire != want_wire) || (back != sim_tick))
	{
		port_log(
		    "SSB64 NetCw: CONTRACT_VIOLATION mode=%s mapping sim=%u D=%u encode=%u (want %u) decode=%u (want %u)\n",
		    (syNetSessionParamsRealDelayActive() != FALSE) ? "real_delay" : "zero_delay",
		    (unsigned int)sim_tick, (unsigned int)d, (unsigned int)wire,
		    (unsigned int)want_wire, (unsigned int)back, (unsigned int)sim_tick);
	}
}

static void syNetCwFlushWindow(u32 d)
{
	s64 avg_x100;

	if (sSYNetCwWindowAdmitted == 0U)
	{
		return;
	}
	avg_x100 = (sSYNetCwWindowCushionSum * 100) / (s64)sSYNetCwWindowAdmitted;
	port_log(
	    "SSB64 NetCw: WINDOW ticks=%u..%u D=%u C=%u admitted=%u ring=%u predict=%u hold_frames=%u "
	    "cushion_min=%d cushion_max=%d cushion_avg_x100=%d\n",
	    (unsigned int)sSYNetCwWindowFirstTick, (unsigned int)sSYNetCwLastAdmittedTick, (unsigned int)d,
	    (unsigned int)((syNetSessionParamsRealDelayActive() != FALSE) ? syNetPeerRealDelayCushionTicks() : d),
	    (unsigned int)sSYNetCwWindowAdmitted, (unsigned int)sSYNetCwWindowRing,
	    (unsigned int)sSYNetCwWindowPredict, (unsigned int)sSYNetCwWindowHoldFrames,
	    (int)sSYNetCwWindowCushionMin, (int)sSYNetCwWindowCushionMax, (int)avg_x100);
	syNetCwContractCheck(sSYNetCwLastAdmittedTick);
	syNetCwResetWindow();
}

void syNetConsumptionWitnessNote(u32 sim_tick, const SYNetPeerSharedCommitStep *step)
{
	s32 mode;
	u32 hr;
	u32 d;
	s64 cushion64;
	s32 cushion;

	mode = syNetCwGetMode();
	if ((mode <= 0) || (step == NULL))
	{
		return;
	}
	/* Live admission only — resim replays are not consumption decisions. */
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (step->advance == FALSE)
	{
		/* Each hold evaluation is one frame spent not simulating. */
		sSYNetCwWindowHoldFrames++;
		return;
	}
	/* Stall retries re-evaluate the same tick until it admits; count once. */
	if ((sim_tick != 0U) && (sim_tick == sSYNetCwLastAdmittedTick))
	{
		return;
	}
	sSYNetCwLastAdmittedTick = sim_tick;

	hr = syNetPeerGetHighestRemoteTick();
	d = syNetPeerGetCommittedInputDelay();
	cushion64 = (s64)hr - (s64)step->required_wire;
	cushion = (cushion64 > 0x7FFFFFFF) ? 0x7FFFFFFF : ((cushion64 < -0x7FFFFFFF) ? -0x7FFFFFFF : (s32)cushion64);

	if (sSYNetCwWindowAdmitted == 0U)
	{
		sSYNetCwWindowFirstTick = sim_tick;
	}
	sSYNetCwWindowAdmitted++;
	if (step->uses_prediction != FALSE)
	{
		sSYNetCwWindowPredict++;
	}
	else
	{
		sSYNetCwWindowRing++;
	}
	sSYNetCwWindowCushionSum += (s64)cushion;
	if (cushion < sSYNetCwWindowCushionMin)
	{
		sSYNetCwWindowCushionMin = cushion;
	}
	if (cushion > sSYNetCwWindowCushionMax)
	{
		sSYNetCwWindowCushionMax = cushion;
	}

	if (mode >= 2)
	{
		port_log("SSB64 NetCw: TICK sim=%u req_wire=%u hr=%u cushion=%d mode=%s D=%u\n",
		         (unsigned int)sim_tick, (unsigned int)step->required_wire, (unsigned int)hr,
		         (int)cushion, (step->uses_prediction != FALSE) ? "predict" : "ring",
		         (unsigned int)d);
	}
	if (sSYNetCwWindowAdmitted >= SYNETCW_WINDOW_ADMITTED_TICKS)
	{
		syNetCwFlushWindow(d);
	}
}

#endif /* PORT && SSB64_NETMENU */
