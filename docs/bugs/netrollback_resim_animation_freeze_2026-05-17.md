# NetRollback resim animation freeze

**Date:** 2026-05-17  
**Status:** RESOLVED

## Symptoms

During client rollback, fighters could snap into an old airborne/tumble pose, keep translating, but stop advancing animation state until a later status transition. Logs showed rollback input mismatches and successful resim, but every snapshot load reported animation hash drift while fighter/world hashes matched.

## Root Cause

Rollback load restored fighter status, motion, physics, joint transforms, and partial animation scalars, then called `gcRemoveAnimAll` on every fighter/item/weapon. That stripped the live AObj/MObj animation chains that figatree playback needs, leaving a valid `status_id` / `motion_id` with no attached animation driver.

The resim loop also called `scVSBattleFuncUpdate` while `syNetRollbackIsResimulating()` was true, and that path skipped `ifCommonBattleUpdateInterfaceAll()`. As a result, rollback replay advanced net input ticks and snapshots without running the same `gcRunAll`-backed battle update that normally advances fighter motion, collision, and animation.

## Fix

- Keep AObj/MObj chains intact after snapshot apply so restored figatree playback survives rollback.
- Run `ifCommonBattleUpdateInterfaceAll()` during rollback resim; keep replay recording disabled while resimulating.

## Verification

Build target: `ssb64`.
