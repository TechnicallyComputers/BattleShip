/**
 * HookManager.h — Runtime function detouring for mods.
 *
 * Wraps funchook (cross-platform: Windows x64, Linux x64/arm64,
 * macOS arm64) behind a clean interface. Each call to InstallHook
 * resolves the symbol via SymbolResolver, registers a funchook detour
 * with the resolved address as the target, installs it, and writes a
 * trampoline pointer to *original_out so the mod can delegate to the
 * original function.
 *
 * funchook is per-handle: one funchook_t is created per hooked target
 * (stored in ChainState::fh). funchook has no incremental "add another
 * hook to an installed handle" API, so every chain mutation tears the
 * target's handle down (uninstall + destroy) and rebuilds it with the
 * new outermost replacement — the same shape the old MinHook backend
 * used (remove + re-create on re-hook).
 *
 * Per-mod ownership: each install records the current owner (set via
 * SetCurrentOwner before MOD_INIT runs). UninstallHooksForOwner walks
 * those records and tears them down. Three removal cases are handled:
 *   - top of chain, chain.size == 1 → destroy handle, free inner_thunk
 *   - top of chain, chain.size  > 1 → rebuild handle at chain[i-1]
 *   - mid-chain                     → re-stitch chain[i+1]'s trampoline
 *                                      thunk to skip past us
 *
 * Mid-chain re-stitch means a single mod can be hot-reloaded even
 * when another mod is stacked on top of one of its hooks — the
 * upper mod stays installed and its calls to "original" now bypass
 * the removed mod's replacement and reach the engine code directly.
 *
 * On engine shutdown, Shutdown() uninstalls and destroys every
 * target's handle regardless of owner.
 *
 * Trampoline thunks are portable double-indirect stubs: the
 * executable stub is emitted once (W^X-clean — written then made
 * read+exec, never rewritten, so it works under macOS hardened
 * runtime without MAP_JIT) and reads its jump target from a separate
 * always-writable data cell. SetThunkTarget only rewrites that cell,
 * never executable memory. x86-64 and arm64 stubs are emitted.
 */
#pragma once

#include <vector>
#include <mutex>
#include <string>
#include <map>

namespace ssb64::mods {

class HookManager
{
public:
    /* Initialize the hook subsystem. Must be called once at startup.
     * (funchook needs no global init; this just arms the manager.) */
    static bool Init();

    /* Uninstall + destroy every target handle and tear down. */
    static void Shutdown();

    /* Resolve `symbol_name` via SymbolResolver, install a detour to
     * `replacement` at the resolved address, write the trampoline
     * function pointer to *original_out.
     *
     * Returns 0 on success. On failure: non-zero, *original_out is
     * set to nullptr, and the engine logs the cause. */
    static int InstallHook(const char *symbol_name,
                           void       *replacement,
                           void      **original_out);

    /* Set the current "owner" for InstallHook calls. Each subsequent
     * install records this as its owner_id until ClearCurrentOwner.
     * ScriptLoader's LoadAll wraps each ModInit with these calls. */
    static void SetCurrentOwner(const char *owner_id);
    static void ClearCurrentOwner();

    /* Walk every installed hook owned by `owner_id`, tear down its
     * funchook handle (or rebuild it for the entry below), free
     * trampoline thunks, sweep sHooks. Returns the number of hooks
     * removed. Mid-chain entries (where another mod's hook is on top)
     * are re-stitched, not orphaned. Safe to call BEFORE unloading
     * the owner's library. */
    static int UninstallHooksForOwner(const char *owner_id);

private:
    struct HookRecord {
        std::string symbol_name;
        void       *target;
        std::string owner_id;
    };

    /* Per-symbol chain state for stacking multiple hooks on the same
     * function. Three parallel vectors track each chain entry:
     *
     *   chain[i]              — replacement function pointer
     *   chain_owners[i]       — owner_id at install time
     *   chain_trampolines[i]  — the thunk the mod stashed as `original`.
     *                           For i=0 this == inner_thunk (shared).
     *                           For i>=1 it's an AllocThunk pointing
     *                           at chain[i-1] (so chain[i]'s "original
     *                           fn" walks down the chain). Used during
     *                           uninstall to free the per-entry thunk. */
    struct ChainState {
        void *fh;            /* funchook_t* for this target (one per target) */
        void *orig_call;     /* funchook trampoline → original engine code   */
        void *inner_thunk;   /* bottom-of-chain thunk; jumps to orig_call    */
        std::vector<void *>       chain;
        std::vector<std::string>  chain_owners;
        std::vector<void *>       chain_trampolines;
    };

    static std::mutex sMutex;
    static std::vector<HookRecord> sHooks;
    static std::map<void *, ChainState> sChains;
    static std::string sCurrentOwner;
    static bool sInitialized;

    /* Allocate an executable double-indirect thunk: the stub jumps to
     * *cell, where cell is a separate always-writable data word. The
     * stub is emitted once and made read+exec (never rewritten — safe
     * under macOS hardened runtime). SetThunkTarget rewrites only the
     * data cell. FreeThunk releases both. */
    static void *AllocThunk(void *initial_target);
    static void  SetThunkTarget(void *thunk, void *new_target);
    static void  FreeThunk(void *thunk);
};

} // namespace ssb64::mods
