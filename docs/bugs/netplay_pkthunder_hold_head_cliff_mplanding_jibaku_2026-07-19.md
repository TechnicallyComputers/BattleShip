# PK Thunder Hold head CLIFF MpLanding → jibaku launch (2026-07-19)

**Soak:** soak1 session `128512323` seed `454354279` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM`

## Symptom

1. Tooling reported `PHYSICS_FORK_ONSET gut=2043 fields=topn_tx,topn_ty fflags=CLIFF` — misread as fighter soft-lip.
2. SoftLipPhase: Android **5420** rows, Linux **0** (stale desktop binary).
3. `FRAME_COMMIT_STATE_DIVERGE` @2069 `diverged=figh` inputs MATCH, `REPLAY_DETERMINISM`.
4. `fhash` / `sim_state` matched through **2063**; first fighter DIFF @**2064** when status → **236** `SpecialAirHiJibaku`.

## Root cause

Each gut has **two** MpLanding rows (last-write-wins hid the weapon):

| Actor | Status / kind | translate | Cross-peer |
|-------|---------------|-----------|------------|
| P1 Ness Hold (`SpecialAirHiHold` **233**) | fighter | ≈ −3611.913 | **matched** |
| PK Thunder head (`nWPKindPKThunderHead` **0x0E**) | weapon | ≈ −4715 | **forks @2043** (Δx≈0.13, Δy≈0.02 → ~0.76/0.47 by 2063) |

Head map uses `wpMapTestAllCheckCollEnd` → `mpProcessRunFloorCollisionAdjNewNULL` on CLIFF. SoftLipPhase only ran in fighter `SpecialCollisions`, so it never named the weapon. Fine `syNetplayQuantizeDObjTranslate` after ProcUpdate does not absorb post-map AdjNew ULP. At Hold→jibaku, Refresh copies the forked head into `pkthunder_pos` → `dist = fighter − pkthunder_pos` → `atan2` → `vel_air` — same family as [`netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-19.md`](netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-19.md); launch-anchor 1.0u at SetStatus is too late once head has already drifted for many ticks.

Secondary bug: SoftLip sticky keyed via `ftGetStruct(CollGObj)` while weapon UpdateMain also sets CollGObj — unsafe / wrong for WPStruct.

## Fix (`PORT && SSB64_NETMENU`)

1. **MpLanding identity** — log `domain=ft|wp|it` + `player` + `kind` (status_id or `wp->kind`) from CollGObj / `link_id`.
2. **SoftLip sticky fighter-only** — `SoftLipPlayerIndex` requires `link_id == nGCCommonLinkIDFighter`.
3. **SoftLipPhase on wpMap** — `wp_post_phys` / `lwall` / `rwall` / `ceil` / `floor` in `wpMapProcAllCheckCollEnd`; phase log includes `domain=`.
4. **`syNetplayNessHardenPKThunderHeadAfterMap`** — after `wpMapTestAllCheckCollEnd` in `wpNessPKThunderHeadProcMap`, coarse-snap head translate onto the jibaku **1.0u** launch grid while owner is in Hold scope (same grid as `syNetplayNessHardenPKJibakuLaunchAnchor`).
5. **scan-drift** — multi-actor `PHYSICS_FORK_ONSET` (prefer `domain=wp` / PKThunderHead + `hold_head→jibaku_risk`); `SOFTLIP_PHASE_PARITY` WARN when one peer has 0 SoftLipPhase rows.

Offline / non-rollback unchanged.

## Verify

**Agent:** `cmake --build build --target ssb64 -j 4` (netmenu).

**Human re-soak:** Ness Hold near Dream Land CLIFF → self-hit jibaku — both peers on SoftLipPhase binary + cliff env:

- `PHYSICS_FORK_ONSET` (if any) labeled `domain=wp … kind=14 (PKThunderHead)` before jibaku
- SoftLipPhase counts non-zero on **both** peers; head `wp_post_*` topn matched after harden
- No FC `figh` inputs MATCH from Hold-head → jibaku launch split

## Related

- [`netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-19.md`](netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-19.md) — launch-anchor + dist harden at SetStatus
- [`netplay_jumpaerial_cliff_softlip_phase_probe_2026-07-19.md`](netplay_jumpaerial_cliff_softlip_phase_probe_2026-07-19.md) — fighter SoftLipPhase probes
- [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md) — fighter sticky latch
