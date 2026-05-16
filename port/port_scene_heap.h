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

#ifdef __cplusplus
}
#endif

#endif /* PORT_SCENE_HEAP_H */
