#ifndef PORT_SCENE_HEAP_H
#define PORT_SCENE_HEAP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host scene arena bounds for the active taskman scene (see syTaskmanStartTask
 * PORT paths in decomp/src/sys/taskman.c and port/net/sys/taskman.c).
 * Used by port_classify_dl_ptr / Gfx diagnostics — range checks only. */
extern void *gPortSceneHeap;
extern const size_t gPortSceneHeapSize;

/* Terminator list_id in DObjDLLink chains (= ARRAY_COUNT(gSYTaskmanDLHeads)). */
#define PORT_DOBJ_DLLINK_TERMINATOR 4

/* Returns 1 if chain looks like a valid reloc DObjDLLink list, 0 if stale/corrupt. */
int portDObjDLLinkChainLooksValid(const void *chain);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SCENE_HEAP_H */
