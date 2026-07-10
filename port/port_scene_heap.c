#include "port_scene_heap.h"

#include <stdint.h>

#ifndef SSB64_UPSTREAM_DECOMP_NET_SYS
void *gPortSceneHeap = NULL;

/* Must match kPortHeapSize in both PORT syTaskmanStartTask implementations. */
const size_t gPortSceneHeapSize = 16u * 1024u * 1024u;
#endif

int portDObjDLLinkChainLooksValid(const void *chain)
{
    const uint8_t *entry;
    int zero_pairs;
    int i;

    if (chain == NULL)
    {
        return 0;
    }

    /* Recycled scene arena reads as {list_id=0, dl=0} forever — not a real chain. */
    if (gPortSceneHeap != NULL)
    {
        uintptr_t addr = (uintptr_t)chain;
        uintptr_t base = (uintptr_t)gPortSceneHeap;

        if ((addr >= base) && (addr < base + gPortSceneHeapSize))
        {
            const int32_t *words = (const int32_t *)chain;

            if ((words[0] == 0) && (words[1] == 0))
            {
                return 0;
            }
        }
    }

    entry = (const uint8_t *)chain;
    zero_pairs = 0;

    for (i = 0; i < 16; i++, entry += 8)
    {
        int32_t list_id = *(const int32_t *)(entry + 0);
        uint32_t dl_token = *(const uint32_t *)(entry + 4);

        if (list_id == PORT_DOBJ_DLLINK_TERMINATOR)
        {
            return 1;
        }

        if ((uint32_t)list_id > PORT_DOBJ_DLLINK_TERMINATOR)
        {
            return 0;
        }

        if ((list_id == 0) && (dl_token == 0))
        {
            zero_pairs++;
            if (zero_pairs >= 2)
            {
                return 0;
            }
        }
        else
        {
            zero_pairs = 0;
        }
    }

    return 0;
}
