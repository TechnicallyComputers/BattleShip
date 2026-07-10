#ifndef _SYNETPEER_TRANSPORT_H_
#define _SYNETPEER_TRANSPORT_H_

/*
 * Transport ingress boundary: socket pump + staged packet apply before session/input layers run.
 * Session readiness, delay contract, and rollback live in netpeer.c / netinput / netrollback.
 */

#include <PR/ultratypes.h>

#ifdef PORT
extern void syNetPeerPumpIngressTransport(const char *caller_tag);
extern void syNetPeerPumpIngressBeforeInputRead(void);
#endif

#endif /* _SYNETPEER_TRANSPORT_H_ */
