# Netrollback item_gobj Resim SIGSEGV

## Summary

Rollback resim could SIGSEGV in `ftMainProcUpdateInterrupt` at `fault_addr=0x18` on the first frame after loading a snapshot (e.g. host tick 2520). The crash happened immediately after `resim begin` with no `resim_tick` lines.

## Root Cause

`ftMainProcUpdateInterrupt` dereferenced `itGetStruct(this_fp->item_gobj)->kind` when `item_gobj` was non-NULL but the item payload was NULL — a GObj still in the ID table during teardown, or a stale fighter-held reference after snapshot load.

Snapshot apply used `gcFindGObjByID` alone for `item_gobj` / throw / catch / capture refs, which does not reject GObjs with no live `ITStruct` / `FTStruct` / `WPStruct` payload.

## Fix

- **ftmain.c (PORT):** Null-check `itGetStruct(item_gobj)` before reading `kind`; clear stale `item_gobj` when payload is missing (hammer update + sword afterimage paths).
- **netrollbacksnapshot.c:** `syNetRbSnapResolveLiveGobj` / `syNetRbSnapResolveItemGobj` validate typed payloads before restoring fighter cross-refs.

## Verification

Rebuild `ssb64` and re-run high-prediction rollback soak; resim at real stick mismatches should no longer crash in `ftMainProcUpdateInterrupt`.
