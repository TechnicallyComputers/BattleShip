# Kirby Stone (rock form) XF garbage SIGSEGV — 2026-07-08

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)  
**Scope:** `decomp/src/ef/efmanager.c`

## Symptom

soak2 session `178665165` / seed `1509863911` (Kirby vs Fox): drift scan **PASS** (`LOAD_HASH_DRIFT=0`, synctest OK), but both peers `UNSTABLE` with `sigsegv=1` near `max_sim_tick≈2535`.

Gameplay: Kirby `status=265` (`SpecialAirLwHold`, Stone air hold/fall) drops onto Fox → Fox enters damage/hitlag → both peers SIGSEGV.

Linux (AppImage Release `build-bundle-linux-netplay-us`):

```
fault_addr=0x7f4d00000128  x0=0x7f4d000000fa
pc ≈ efManagerNetplayEjectStaleXfEffect  (read xf->users_num at LBTransform+0x2e)
```

Android: same class (`fault_addr=0x70000001c2`). Nearby: damage/dust shells (`gobj_id=1011`) → `efManagerDefaultProcUpdate` → stale-xf eject path.

## Root cause

1. **Garbage non-NULL `LBTransform*`** in `ep->effect_vars.common.xf` (union stomp / recycle after FX churn). Stone motion spawns dust/hit cosmetics that hit this path.
2. Netmenu `DefaultProcUpdate` historically ran **`efManagerNetplayEffectXfIsLive` before the PORT pointer sanitizer**, so a garbage-but-non-NULL `xf` was field-dereferenced (`users_num`, free/alloc checks).
3. **`efManagerNetplayEjectStaleXfEffect` logged `xf->users_num` / free-list checks without validating the pointer** — the "safe eject" logger itself SIGSEGV'd.

`lbParticleTransformIsOnFreeList` / `IsAllocated` mostly pointer-compare and do not fault first; the crash was on real struct field reads in IsLive / eject logging.

Related but separate: Fox reflector stale-shell SIGSEGV (`netplay_fox_reflector_stale_forward_sigsegv_2026-07-08.md`) — different symbol/`func_ovl2_801031E0` mapping in Debug vs Release; this soak maps `+0x3982d7` to **`efManagerNetplayEjectStaleXfEffect`** under the bundle Release layout.

## Fix

| Change | Purpose |
|--------|---------|
| `efManagerNetplayXfPointerLooksValid` | Reject `<0x10000`, low-16 `0xFFFF`, unaligned pointers before any `xf->` read |
| `efManagerNetplayEffectXfIsLive` | Early `reason="xf_garbage"` when pointer fails looks-valid |
| `efManagerNetplayEjectStaleXfEffect` | Only log/read xf fields when looks-valid; clear `ep->xf` + `common.xf`; null-safe eject |
| `efManagerDefaultProcUpdate` | PORT sanitize **first** (netmenu ejects on garbage); then single IsLive path |
| `DustLightProcUpdate` / `DustHeavyDoubleProcUpdate` | Same looks-valid gate before IsLive / write |

Offline PORT early-out on NULL/garbage xf unchanged (no fork eject). Netmenu only ejects stale shells.

## Verification

- Rebuild netmenu / AppImage bundle with this `efmanager.c`.
- soak2 Kirby vs Fox: Stone (down-B) drop onto Fox around mid-match — no SIGSEGV.
- Drift scan should remain PASS; confirm `effect_xf_stale ... reason=xf_garbage` may log once then eject cleanly (no crash).

## Code pointers

| Area | Symbol |
|------|--------|
| Pointer gate | `efManagerNetplayXfPointerLooksValid` |
| Liveness | `efManagerNetplayEffectXfIsLive` |
| Safe eject | `efManagerNetplayEjectStaleXfEffect` |
| Default FX | `efManagerDefaultProcUpdate` |
| Dust paths | `efManagerDustLightProcUpdate`, `efManagerDustHeavyDoubleProcUpdate` |
