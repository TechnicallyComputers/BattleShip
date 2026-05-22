# Coupled weapon lifecycle audit — preemptive netplay fixes

**Date:** 2026-05-20  
**Status:** AUDIT (Yoshi + Samus fixed; others pending soak-driven work)  
**Subsystem:** Fighter special moves + `port/net/sys/netrollbacksnapshot.c`

## Purpose

Yoshi egg and Samus charge shot both failed after rollback with **orphan duplicates** and **stale re-coupling**. This doc traces all fighter special moves that hold a persistent `GObj*` to a spawned weapon so netplay testing can prioritize the same Phase 4 hardening where needed.

## Phase 4 pattern (reference)

Applied successfully to Yoshi egg and Samus charge shot:

| Layer | What to add |
|-------|-------------|
| **Charge/coupled predicate** | Distinguish “still coupled to fighter” vs released projectile (egg: `!is_throw && !is_spin && attack_state==Off`; Samus: `is_release==FALSE`). |
| **Reacquire** | Pointer-only scan (`FindLiveWeaponForOwner` + predicate) before spawn. |
| **Guarded spawn** | `MakeWeapon` only when reacquire returns NULL. |
| **Cull helper** | Destroy duplicate coupled weapons for owner; keep one `keep_gobj`. |
| **Exit cleanup** | Destroy coupled weapon + cull on status exit (Wait/Fall/End/interrupt). |
| **Load rebind** | Resolve with predicate → reacquire fallback → cull only if non-NULL (never cull-all before rebind). |
| **Emergency fallback** | On anim fire/throw event, reacquire then spawn if still NULL. |

## Coupled weapons matrix

Snapshot layer already stores coupled weapon gobj ids ([`netrollback_fighter_coupled_gobj_snapshot_2026-05-19.md`](netrollback_fighter_coupled_gobj_snapshot_2026-05-19.md)). Runtime Phase 4 lifecycle hardening status:

| Character | Move | Coupled field | Weapon kind | Risk | Snapshot rebind | Phase 4 runtime |
|-----------|------|---------------|-------------|------|-----------------|-----------------|
| **Mario / Luigi** | Neutral B | — | `nWPKindFireball` | **Fixed** | Emergency spawn + dedup | **Shipped** |
| **Yoshi** | Up+B (egg) | `status_vars.yoshi.specialhi.egg_gobj` | `nWPKindEggThrow` | **Fixed** | Yes + cull | **Shipped** (Phase 4 / 4.1 / 4.2) |
| **Samus** | Neutral B | `status_vars.samus.specialn.charge_gobj` | `nWPKindChargeShot` | **Fixed** | Yes + charging predicate + cull + orb gfx refresh | **Shipped** |
| **Kirby** | Copy Samus N-B | `status_vars.kirby.copysamus_specialn.charge_gobj` | `nWPKindChargeShot` | **Fixed** | Yes (same as Samus) | **Shipped** |

### Item weapons (snapshot + spawn guard — not Phase 4 coupled)

| Source | Weapon kinds | Snapshot respawn | Spawn guard | Notes |
|--------|--------------|------------------|-------------|-------|
| Held items (Ray Gun, Fire Flower, Star Rod) | `0x14`–`0x16` | **Shipped** (prior) | **Shipped** — `syNetRbSnapHeldItemWeaponNeedsSpawn` | Fire-and-forget; pos/vel dedup on anim event |
| Monster / stage items | `0x17`–`0x1F` | **Shipped** | N/A (AI/event spawn) | Item-parent owner resolve; see [`netrollback_item_monster_weapon_respawn_2026-05-20.md`](netrollback_item_monster_weapon_respawn_2026-05-20.md) |

| Character | Move | Coupled field | Weapon kind | Risk | Snapshot rebind | Phase 4 runtime |
|-----------|------|---------------|-------------|------|-----------------|-----------------|
| **Link** | Neutral B | `passive_vars.link.boomerang_gobj` | `nWPKindLinkBoomerang` | Medium | Pointer rebind only | **Not done** — see below |
| **Kirby** | Copy Link N-B | `passive_vars.kirby.copylink_boomerang_gobj` | `nWPKindLinkBoomerang` | Medium | Pointer rebind only | **Not done** |
| **Link** | Up+B (spin) | `status_vars.link.specialhi.spin_attack_gobj` | `nWPKindSpinAttack` | Medium | Pointer rebind only | **Partial** — spawn guarded on NULL |
| **Ness** | Up+B (PK Thunder) | `status_vars.ness.specialhi.pkthunder_gobj` | `nWPKindPKThunderHead` | Medium–High | Pointer rebind + cull | **Shipped** (Phase 4 — see [`netplay_ness_pkthunder_upb_segv_2026-05-22.md`](netplay_ness_pkthunder_upb_segv_2026-05-22.md)) |
| **Pikachu** | Down+B (Thunder) | `status_vars.pikachu.speciallw.thunder_gobj` | `nWPKindPikachuThunderHead` | Medium | Pointer rebind only | **Not done** |

### Low risk — fire-and-forget projectiles

No persistent fighter↔weapon coupling across status changes; snapshot weapon respawn covers rollback. Test for duplicate spawn under resim, but Phase 4 pattern usually not required:

- Mario / Luigi / Kirby copy: fireball (`wpMarioFireballMakeWeapon`, etc.)
- Fox / Falco: laser
- Link: arrow (separate from boomerang passive)
- Samus: charge shot **after release** (projectile phase — predicate excludes these)
- Most other `SpecialN` one-shot spawns

### Fighter-state-only (no weapon GObj coupling)

- DK: Giant Punch charge level in fighter vars
- Jigglypuff / Kirby: Rest / puff states
- Purin: Sing (effect/state, not a coupled weapon GObj)

## Per-move notes for testing

### Link — Boomerang (`ftlinkspecialn.c`, `ftkirbycopylinkspecialn.c`)

- **Spawn:** `ftLinkSpecialNMakeBoomerang` on anim `flag0` — **no NULL guard**, always assigns new gobj.
- **Destroy:** `ftLinkSpecialNDestroyBoomerang` exists; called on catch/interrupt paths.
- **Risk:** Synctest during throw anim could leave boomerang in flight while `boomerang_gobj` cleared → duplicate on next B or stale passive pointer.
- **Suggested fix:** Reacquire live boomerang owned by fighter before spawn; cull extras; destroy on SpecialN anim-end if still coupled (boomerang returns via weapon logic — predicate TBD: in-flight vs held).

### Link — Spin attack (`ftlinkspecialhi.c`)

- **Spawn:** `ftLinkSpecialHiMakeWeapon` already checks `spin_attack_gobj == NULL` before spawn.
- **Risk:** Orphan spin weapon if status exits without destroy; duplicate less likely than boomerang.
- **Suggested fix:** Exit cleanup + cull helper for `nWPKindSpinAttack` if soak shows orphans.

### Ness — PK Thunder (`ftnessspecialhi.c`)

- **Spawn:** `ftNessSpecialHiMakePKThunder` once at SpecialHi start — **no reacquire**.
- **Complexity:** Trail segments (`parent_gobj` chain), collide/recall logic, `is_thunder_destroy` passive flag.
- **Risk:** Highest among remaining — orphan head + trail desync after rollback; recall may target wrong gobj id.
- **Suggested fix:** Phase 4 + trail-aware cull; charging predicate = head not released / trail intact (needs weapon-var review).

### Pikachu — Thunder (`ftpikachuspeciallw.c`)

- **Spawn:** `ftPikachuSpecialLwMakeThunder` on Start anim event — **no NULL guard**.
- **Loop:** `thunder_gobj` used for position/effect updates in Loop/Hit states.
- **Risk:** Similar to boomerang — duplicate thunder column if spawn fires twice after resim.
- **Suggested fix:** Reacquire before spawn; cull duplicate `nWPKindPikachuThunderHead` for owner; destroy on SpeicalLw exit.

## Test checklist (user soak)

For each row in the matrix marked **Not done** or **Partial**:

1. Hold/charge through several rollback frames (synctest or natural GGPO).
2. Exit move cleanly (Wait/Fall) — no orphan weapon at last attach point.
3. Re-enter move — no duplicate weapon GObjs in `SNAPSHOT_WEAPON_DIAG`.
4. Fire/release path — projectile behaves normally (no empty release, no instant re-charge).
5. Kirby copy variants where applicable.

## Related fixes

- [`yoshi_egg_orphan_duplicate_2026-05-20.md`](yoshi_egg_orphan_duplicate_2026-05-20.md)
- [`samus_charge_shot_orphan_duplicate_2026-05-20.md`](samus_charge_shot_orphan_duplicate_2026-05-20.md)
- [`netrollback_weapon_owner_by_player_2026-05-20.md`](netrollback_weapon_owner_by_player_2026-05-20.md)
