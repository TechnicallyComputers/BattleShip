# Netplay: Link / Kirby CopyLink boomerang Phase 4

**Date:** 2026-07-11  
**Scope:** `PORT && SSB64_NETMENU`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom class

Coupled-weapon Phase 4 gap ([`coupled_weapon_lifecycle_audit_2026-05-20.md`](coupled_weapon_lifecycle_audit_2026-05-20.md)):
Neutral-B can re-spawn a boomerang after resim while an owned live boomerang still exists, or
leave a coupled orphan on empty-slot verify (`FighterCoupledReference` preserves
`passive_vars.link.boomerang_gobj` / `kirby.copylink_boomerang_gobj`).

## Fix

Runtime Phase 4 (decomp, netmenu-gated):

- `ftLinkSpecialNMakeBoomerang` / `ftKirbyCopyLinkSpecialNMakeBoomerang` — reacquire owned live
  boomerang before spawn; cull extras; NULL-guard assign
- Destroy paths cull remaining owned boomerangs after clearing the coupled pointer

Verify cull (snapshot, fireball / Thunder Head class):

- Blob scope for Link/NLink and Kirby/NKirby CopyLink SpecialN* (throw / empty / get / return)
- Empty + unmatched owned `nWPKindBoomerang` cull; clear coupling on destroy
- Wire into `prepare_verify` and `syNetRbSnapshotTryRepairWeaponHashForVerify`

Helpers: `syNetRbSnapReacquireBoomerangForFighter`, `syNetRbSnapCullOwnedBoomerangsForFighter`.

## Test plan

- [ ] Link + Kirby CopyLink Neutral-B spam under synctest; no duplicate boomerang / empty-slot
      `diverged=wpn`.
- [ ] Control: single boomerang still throws, returns, and rebinds after rollback.
