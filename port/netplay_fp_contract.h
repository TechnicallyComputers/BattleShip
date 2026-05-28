#ifndef PORT_NETPLAY_FP_CONTRACT_H
#define PORT_NETPLAY_FP_CONTRACT_H

/*
 * Cross-ISA netplay FP contract markers.
 * Sim-critical TUs are compiled with -ffp-contract=off (see CMakeLists.txt).
 * Runtime quantization lives in port/net/sys/netplay_sim_quantize.{h,c}.
 */

#if defined(SSB64_NETMENU)
#define SSB64_NETPLAY_FP_CONTRACT 1
#if defined(_MSC_VER)
#pragma fp_contract(off)
#endif
#else
#define SSB64_NETPLAY_FP_CONTRACT 0
#endif

#endif /* PORT_NETPLAY_FP_CONTRACT_H */
