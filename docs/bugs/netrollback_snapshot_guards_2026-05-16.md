# Netrollback Snapshot Guards

## Summary

Enabling `tc netem` delay/loss during rollback netplay could SIGSEGV inside `syNetRbSnapshotSave()` around the time dynamic battle objects were active. The failure was consistent with a fighter, item, or weapon `GObj` being present in an active link list while its typed payload pointer was temporarily NULL or stale during snapshot capture or the post-capture NetSync hash pass.

## Root Cause

Rollback snapshot save walked the fighter list and immediately dereferenced `ftGetStruct(fighter_gobj)->player`. The same save path then computed fighter, item, weapon, and animation hashes whose helpers dereferenced `ftGetStruct`, `itGetStruct`, and `wpGetStruct` without checking for NULL.

A follow-up repro on a rebuilt AppImage still crashed at `syNetRbSnapshotSave+0x14c5`; resolving that offset against the local binary mapped it to `syNetSyncHashActiveWeapons()` reading `DObjGetStruct(gobj)->translate.vec.f.y`. The helper checked `DObjGetStruct(gobj) != NULL`, but expanded the macro again for each coordinate, so a transiently cleared `gobj->obj` could pass the first check and fault on a later coordinate read.

A later repro with the cache-once hash fix progressed farther and again crashed at `syNetRbSnapshotSave+0x14c5` in the packaged binary, this time inside inlined fighter snapshot capture reading `gobj->id` from a fighter-held reference. Those references can be stale after object teardown; `syNetRbSnapGobjId()` now validates pointer identity against the live `gGCCommonLinks[]` lists before reading the ID.

Under packet loss, rollback/resim increases pressure on object creation, teardown, and restore ordering. A transient object-list entry without a live typed payload could therefore crash the diagnostic hash or snapshot path instead of being skipped.

## Fix

- Guard fighter `ftGetStruct()` use in snapshot save, load, and battle-state relink.
- Guard item/weapon payload lookups in snapshot capture/apply and in NetSync hashes.
- Cache item/weapon `DObj *` once before reading transform coordinates in NetSync hashes and snapshot apply loops.
- Validate fighter/item/weapon cross-object references against live GObj lists before serializing IDs.
- Cache `next_gobj` before `gcEjectGObj()` in item/weapon restore loops so ejection does not invalidate traversal.
- Add throttled `SSB64 NetRbSnapshot: guard skip ...` diagnostics with phase, object kind, tick, pointer, and ID for future repros.

## Verification

Built `ssb64` successfully after the patch.

