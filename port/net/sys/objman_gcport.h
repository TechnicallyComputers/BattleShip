#ifndef SYS_OBJMAN_GCPORT_H
#define SYS_OBJMAN_GCPORT_H

#include <PR/ultratypes.h>
#include <stddef.h>

/*
 * gcRunAll-shaped traversal probes for NetSync / netpeer diagnostics.
 * Implemented in port/net (not the decomp submodule).
 */
u32 gcPortHashGcRunAllTraversalFingerprint(void);
void gcPortGcRunAllTraversalFingerprintEx(u32 *gch, u32 *ngobj, u32 *ngobj_run, u32 *nproc_run);
void gcPortSnprintGcRunAllTraversalHeadPairs(char *buf, size_t bufsize, int max_pairs);

#endif
