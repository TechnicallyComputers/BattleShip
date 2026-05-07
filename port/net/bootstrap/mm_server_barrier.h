#ifndef MM_SERVER_BARRIER_H
#define MM_SERVER_BARRIER_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

/*
 * Server-assisted barrier bootstrap (optional HTTPS coordinator).
 * When `SSB64_NETPLAY_SERVER_BOOTSTRAP=1`, the host may replace locally computed
 * `start_ms_raw` / VI-quantized `start_ms` with a server contract.
 *
 * Non-Windows netmenu builds link the translation unit; Windows netmenu uses the
 * no-op macro until curl-backed bootstrap is ported.
 */

#if defined(PORT) && defined(SSB64_NETMENU) && !defined(_WIN32)

extern sb32 mmServerBarrierTryApplyHostSchedule(u64 *io_start_ms_raw, u64 *io_quantized_start_ms);

#else

#define mmServerBarrierTryApplyHostSchedule(io_start_ms_raw, io_quantized_start_ms) \
	((void)(io_start_ms_raw), (void)(io_quantized_start_ms), FALSE)

#endif

#endif /* MM_SERVER_BARRIER_H */
