# gcSetInvMatrix debug halt → SIGABRT on PORT

**Date:** 2026-07-12  
**Session:** soak with `SSB64_NETPLAY_STRICT_INPUT=1`  
**Status:** FIX IMPLEMENTED (PORT, all builds)

## Symptom

Both peers abort with SIGABRT after ~1000 log lines:

```text
SSB64: syDebugPrintf [1]: zero div in gcSetInvMatrix()
SSB64: syDebugRunFuncPrint called (debug crash screen)
...
SSB64: syDebugPrintf called 1000+ times — likely debug halt loop, aborting
SSB64: !!!! CRASH SIGABRT
```

Not a memory SIGSEGV — vanilla N64 infinite debug loop on singular joint matrix, accelerated on PORT because `syDebugPrintf` returns immediately but the `while(TRUE)` in `gmCollisionSetInvertMatrix` keeps calling it until `debug.c` aborts at 1000.

## Trigger (this soak)

Tick 512 after GGPO/resim + `emergency_restore`: p1 Kirby `status=19 motion=13`, figatree `frame=0.00`. Joint `mtx_translate` had zero determinant → `func_ovl2_800EDE00` → `gmCollisionSetInvertMatrix`.

STRICT_INPUT witness was logging normally (`wire_overwrite` / `confirm_rewrite`); unrelated to the crash except timing.

## Root cause

`decomp/src/gm/gmcollision.c` — six vanilla `while(TRUE) { syDebugPrintf(...); scManagerRunPrintGObjStatus(); }` sites on zero division / zero determinant. On N64 that waits for a controller combo on the crash screen. On PORT, `syDebugPrintf` logs and returns → tight loop → abort guard in `debug.c`.

## Fix

On `#ifdef PORT` only, replace infinite debug halts with rate-limited `port_log` + safe fallback:

| Site | Fallback |
|------|----------|
| `gcSetInvMatrix` zero det | identity inverse matrix, return |
| `gcSetMatrixNcs` zero `scale_mul` axis | skip that axis divide |
| `gcColSphere` zero `sp54` | `return FALSE` (no hit) |

Offline N64 ROM behavior unchanged (`#else` keeps vanilla halt).

## Follow-up

Singular fighter joint matrices after netplay rollback restore may still indicate bad pose/state — worth tracing if hitboxes drift after the fallback. Separate from crash class.

Related: `netplay_strict_input_authority_witness_2026-07-12.md`, `grab_pose_stale_root_transform_2026-05-23.md`.
