#pragma once

/**
 * gameloop.h — PC game loop interface for the SSB64 port.
 *
 * Replaces the N64 multi-threaded model with a single-threaded frame loop.
 * The game's original code runs inside coroutines that yield at blocking
 * points (osRecvMesg BLOCK), and the main loop resumes them once per frame.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the game boot sequence.
 * Creates the game loop coroutine and starts the N64 boot chain
 * (syMainLoop -> thread creation -> scManagerRunLoop).
 * Must be called after PortInit().
 */
void PortGameInit(void);

/**
 * Run one frame of the game loop.
 * Posts a VI retrace message, resumes the game coroutine (which runs
 * until it yields at the next osRecvMesg BLOCK), and presents the frame.
 * Must be called from the main loop after PortGameInit().
 */
void PortPushFrame(void);

/**
 * Shut down the game loop and destroy all coroutines.
 */
void PortGameShutdown(void);

/**
 * Resume all registered service thread coroutines that are waiting.
 * Called internally by PortPushFrame. Defined in n64_stubs.c.
 */
void port_resume_service_threads(void);

/** Frame index for net barrier / logging (PC port). */
int port_get_push_frame_count(void);
void port_reset_push_frame_count_for_net_barrier(void);
/** Reset VS decouple sim-step deadline phase when battle barrier first releases (pairs with taskman resync). */
void port_reset_vs_decouple_pacing_for_net_barrier(void);
/** Add nanoseconds bias applied once when decouple pacing first latches sVsNextSimStepDeadline after a barrier. */
void port_add_vs_decouple_barrier_latch_bias_ns(long long delta_ns);

/**
 * Counters for PortPushFrame cadence vs VS decoupled sim stepping (`SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM`).
 * wall_calls increments once per PortPushFrame; sim_advances when a game sim step runs; sim_skips when VS decouple
 * holds a host refresh frame without advancing the negotiated VI sim step. With the default
 * `SSB64_NETPLAY_VS_PUSH_FRAME_HZ` policy (contract VI Hz), wall_calls should track sim_advances during VS.
 */
void port_get_netplay_push_frame_diag(unsigned long long *out_wall_calls, unsigned long long *out_sim_advances,
                                      unsigned long long *out_sim_skips);

#ifdef __cplusplus
}
#endif
