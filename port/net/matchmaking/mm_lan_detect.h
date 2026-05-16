#ifndef MM_LAN_DETECT_H
#define MM_LAN_DETECT_H

#include <PR/ultratypes.h>
#include <ssb_types.h>

#if defined(PORT) && defined(SSB64_NETMENU)

/* Linux: getifaddrs. Windows: GetAdaptersAddresses (CMake links iphlpapi). */
extern sb32 mmLanDetectEndpoint(char *buf, u32 bufsize, s32 udp_fd, const char *bind_spec_opt);

#endif

#endif /* MM_LAN_DETECT_H */
