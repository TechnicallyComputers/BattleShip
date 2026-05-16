#include "port_scene_heap.h"

void *gPortSceneHeap = NULL;

/* Must match kPortHeapSize in both PORT syTaskmanStartTask implementations. */
const size_t gPortSceneHeapSize = 16u * 1024u * 1024u;
