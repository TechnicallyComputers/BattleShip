# funchook macOS arm64 detours silently disabled — CMake `\;` anchor bug

**Date:** 2026-06-15
**Area:** Code-mod support (funchook hook backend) / build system
**Status:** FIXED (`cmake/funchook_capstone_bump.cmake`)

## Symptom

On Apple Silicon, TCC source mods compiled, loaded, resolved symbols, and ran
`ModInit`/`ModExit` fine — but any mod that installed a **funchook inline
detour** failed at install time:

```
[mods] InstallHook(osSpTaskStartGo): funchook_install failed: Failed to
unprotect memory 0x104bd1000 (size=4096, prot=read,write,exec) <-
0x104bd1c98 (size=8, error=Invalid argument)
[HookTest] hook install FAILED rc=1
[HookTest] exit OK (detour saw 0 frames)
```

`error=Invalid argument` (EINVAL), not `Permission denied` — `mprotect(RWX)` on a
code-signed `__TEXT` page is rejected outright on Apple Silicon regardless of the
JIT entitlements / `__TEXT` maxprot=rwx re-sign (both of which *were* applied).

## Root cause

funchook 1.1.3 only ever calls `mprotect()`. The port carries a FetchContent
`PATCH_COMMAND` (`cmake/funchook_capstone_bump.cmake`) that rewrites funchook's
two `__TEXT` protect call sites to use `mach_vm_protect` with `VM_PROT_COPY`
(a private writable COW copy — the Dobby/frida recipe) behind
`#if defined(__APPLE__) && defined(__aarch64__)`.

The rewrite **never reached the call sites.** The anchors were defined as:

```cmake
set(_anchor_begin "    rv = mprotect(mstate->addr, mstate->size, prot)\;")
```

In a CMake double-quoted string, `\;` renders as a **literal backslash-semicolon**
(`prot)\;`), not a plain `;`. So the anchor never matched funchook's actual
`    rv = mprotect(mstate->addr, mstate->size, prot);` and `string(REPLACE)`
silently no-op'd. Only the helper-*definition* insertion (anchored on
`#include <dlfcn.h>`, no semicolon) succeeded — leaving the `mach_vm_protect`
helpers defined but **unused**, while the live call sites kept stock `mprotect`.

The guard `if(NOT _unix_contents MATCHES "funchook_mac_arm64_set_rw")` then keyed
idempotency on the *helper name*, so this partial state always skipped
re-patching on later configures — it never self-healed.

Verify the CMake escaping trap directly:

```cmake
set(b "...prot)\;")   # b == "...prot)\;"  (backslash kept), NOT "...prot);"
```

## Fix

1. Anchors use a plain `;` (a literal semicolon inside a quoted CMake string
   needs no escape): `"    rv = mprotect(mstate->addr, mstate->size, prot);"`.
2. Idempotency guard keys on the *call-site wrap*
   (`funchook_mac_arm64_set_rw(mstate->addr`), and the helper is inserted only
   if absent, so a previously partial-patched `funchook-src` self-heals on
   re-fetch.

Because FetchContent only runs `PATCH_COMMAND` on initial population, an
already-populated `build*/\_deps/funchook-src` must be wiped to pick up the fix:
`rm -rf build*/_deps/funchook-{src,build,subbuild}` then reconfigure.

## Verification

`workspace/hooktest` (hooks `osSpTaskStartGo`, passes through) on macOS arm64:

```
[mods] hooked osSpTaskStartGo @ 0x102455c98 (trampoline=..., chain=1, owner=HookTest)
[HookTest] hook installed OK
[HookTest] osSpTaskStartGo detour hit 60 … 1260 times
[HookTest] exit OK (detour saw 1263 frames)
```

## Lessons

- Never write `\;` for a literal semicolon inside a CMake quoted string — it
  keeps the backslash. Use a plain `;` (quoting already prevents list-splitting),
  or a bracket argument `[[ ... ]]` which does no escape processing.
- A `string(REPLACE)` whose pattern doesn't match is a **silent** no-op. Patch
  scripts that "succeed" but produce dead code need a runtime assertion — here,
  the mod loader compiled and "loaded" cleanly, so only an actual hook-install
  smoke test surfaced it.
- Idempotency guards must key on the *effect* (the wrapped call site), not on an
  incidental side artifact (the helper definition), or a partial application
  becomes permanent.
