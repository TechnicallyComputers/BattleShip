#include <sys/objman_gcport.h>

/*
 * Traversal fingerprint + GObj eject ring are implemented in decomp/src/sys/objman.c
 * (needs static sGCProcessQueue). This TU exists for CMake linkage of the header API.
 */
extern u32 gcPortHashGcRunAllTraversalFingerprint(void);
extern void gcPortGcRunAllTraversalFingerprintEx(u32 *gch, u32 *ngobj, u32 *ngobj_run, u32 *nproc_run);
extern void gcPortSnprintGcRunAllTraversalHeadPairs(char *buf, size_t bufsize, int max_pairs);
extern void gcPortRecordGObjEject(const struct GObj *gobj);
extern void gcPortDumpGObjEjectRing(const char *tag, u32 load_tick);
