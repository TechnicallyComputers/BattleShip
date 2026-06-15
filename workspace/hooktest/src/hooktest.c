/*
 * hooktest.c — exercises the funchook detour path of the mod loader.
 *
 * Hooks osSpTaskStartGo, the engine's per-frame GFX-task submit point
 * (called once every rendered frame). The replacement logs every 60th
 * invocation and then delegates to the original via the trampoline
 * funchook hands back — a pure pass-through, so game behavior is
 * unchanged and this is safe to leave installed.
 *
 * What a clean run proves on the host platform:
 *   - mod_install_hook resolved an exported engine symbol
 *   - funchook installed an inline detour at its address
 *   - the call-original trampoline works (engine keeps rendering)
 *   - the HookManager chain/thunk infra survives repeated invocation
 *
 * osSpTaskStartGo's real prototype is `void osSpTaskStartGo(OSTask *)`.
 * We treat the arg as an opaque void* — pointer-compatible, and it
 * spares the mod from pulling decomp's OSTask definition.
 */
#include "mod.h"

typedef void (*SpTaskStartGoFn)(void *task);

static SpTaskStartGoFn gOrigSpTaskStartGo = 0;
static unsigned gCalls = 0;

static void HookedSpTaskStartGo(void *task) {
    gCalls++;
    if ((gCalls % 60) == 0) {
        mod_log("[HookTest] osSpTaskStartGo detour hit %u times\n", gCalls);
    }
    /* Pass through to the unhooked original. */
    if (gOrigSpTaskStartGo) {
        gOrigSpTaskStartGo(task);
    }
}

MOD_INIT() {
    void *resolved = mod_resolve_symbol("osSpTaskStartGo");
    mod_log("[HookTest] mod_resolve_symbol(osSpTaskStartGo) = %p\n", resolved);

    int rc = mod_install_hook("osSpTaskStartGo",
                              (void *)HookedSpTaskStartGo,
                              (void **)&gOrigSpTaskStartGo);
    if (rc == 0) {
        mod_log("[HookTest] hook installed OK (trampoline=%p)\n",
                (void *)gOrigSpTaskStartGo);
    } else {
        mod_log("[HookTest] hook install FAILED rc=%d\n", rc);
    }
}

MOD_EXIT() {
    /* HookManager::UninstallHooksForOwner tears the detour down by
     * owner when the mod unloads; nothing to do here but report. */
    mod_log("[HookTest] exit OK (detour saw %u frames)\n", gCalls);
}
