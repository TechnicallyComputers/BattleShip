# FTStatusVars overlay map (Approach C — Milestone 1)

**Date:** 2026-06-02  
**Scope:** `decomp/src/ft/ftcommon.h`, `decomp/src/ft/fttypes.h`, accessor layer in `decomp/src/ft/ftstatusvars.h`, witness in `port/net/sys/netplay_statusvars_witness.{h,c}`

## Summary

`FTStruct.status_vars` is a top-level union (`FTStatusVars`) whose `common` member is itself a union (`FTCommonStatusVars`) of per-status overlays. Every overlay begins at byte 0 of the blob; only one overlay is semantically live per `status_id`, but nothing in the N64 layout enforces that. Snapshot save copies the entire blob blindly (`port/net/sys/netrollbacksnapshot.c`).

Port LP64 probe (`sizeof` at compile time with project flags):

| Type | Size (bytes) |
|------|-------------|
| `FTCommonStatusVars` | 56 |
| `FTStatusVars` (top-level) | 56 |
| `FTStruct.status_vars` offset | 3696 |
| `FTStruct.hit_lr` offset | 2260 |
| `FTStruct.shuffle_tics` offset | 678 |

All top-level `FTStatusVars` arms (`common`, `mario`, `fox`, …) alias at offset 0.

---

## FTCommonStatusVars — overlay base offsets

Every named overlay member starts at **0** (union). Individual field offsets below are relative to `status_vars.common` / any aliasing overlay.

| Overlay member | offsetof (bytes) | Primary owning status_id(s) |
|----------------|------------------|----------------------------|
| `dead` | 0 | `nFTCommonStatusDeadDown` … `nFTCommonStatusDeadUpFall` (0–4) |
| `rebirth` | 0 | `nFTCommonStatusRebirthDown` … `nFTCommonStatusRebirthWait` (7–9) |
| `sleep` | 0 | `nFTCommonStatusSleep` (4) |
| `entry` | 0 | `nFTCommonStatusEntry` (5), `nFTCommonStatusEntryNull` (6), character AppearR/L (via `camera_mode == nFTCameraModeEntry`) |
| `turn` | 0 | `nFTCommonStatusTurn`, `TurnRun` |
| `kneebend` | 0 | `KneeBend`, `GuardKneeBend` |
| `jumpaerial` | 0 | `JumpAerialF`, `JumpAerialB` |
| `damage` | 0 | `nFTCommonStatusDamageStart` … `DamageEnd` |
| `squat` | 0 | Squat family |
| `dokan` | 0 | Pipe enter/wait/exit/walk |
| `landing` | 0 | Landing light/heavy |
| `fallspecial` | 0 | Fall special + landing |
| `twister` | 0 | `nFTCommonStatusTwister` |
| `tarucann` | 0 | `nFTCommonStatusTaruCann` |
| `downwait` | 0 | Down wait D/U |
| `downbounce` | 0 | Down bounce D/U |
| `rebound` | 0 | Rebound wait/rebound |
| `cliffwait` | 0 | Cliff wait |
| `cliffmotion` | 0 | Cliff climb/attack/escape family |
| `lift` | 0 | Lift wait/turn |
| `itemthrow` | 0 | Light/heavy throw ranges |
| `itemswing` | 0 | Item swing (bat/sword/harisen/star rod) |
| `fireflower` | 0 | Fire flower shoot (ground/air) |
| `hammer` | 0 | Hammer action states |
| `guard` | 0 | Shield on/guard/off/setoff |
| `escape` | 0 | Roll F/B |
| `catchmain` | 0 | `Catch`, `CatchPull` (166–167) |
| `catchwait` | 0 | `CatchWait` (168) |
| `capture` | 0 | `CapturePulled`, `CaptureWait` (171–172) |
| `thrown` | 0 | `ThrownStart` … `ThrownEnd` (181–186) |
| `capturekirby` | 0 | Kirby swallow / copy star |
| `captureyoshi` | 0 | Yoshi egg / capture |
| `capturecaptain` | 0 | Falcon up-tilt grab victim |
| `throwf` | 0 | `ThrowF`, `ThrowB` |
| `throwff` | 0 | DK cargo back-throw turn |
| `throwfdamage` | 0 | Throw hitstun overlay |
| `attack1` | 0 | Jab / dash attack |
| `attack100` | 0 | Multi-hit attacks |
| `attacklw3` | 0 | Down tilt loop flag |
| `attack4` | 0 | Smash attacks (stores `lr`) |
| `attackair` | 0 | Aerial attack rehit timer |

---

## High-risk field offsets (aliasing at byte 0 and +4)

| Field | offsetof from union base | Notes |
|-------|-------------------------|-------|
| `entry.entry_wait` | 0 | Same word as `catchwait.throw_wait`, `dead.wait`, `rebirth.halo_offset.x` (f32 bit pattern) |
| `entry.lr` | 4 | Stomped by Appear motion events; overlaps `rebirth.halo_offset.y` |
| `entry.floor_line_id` | 8 | |
| `entry.is_rotate` | 12 | Captain AppearL rotate flag |
| `catchwait.throw_wait` | 0 | Grab hold countdown; union-stomped during Appear/other statuses |
| `rebirth.halo_offset` | 0 | Vec3f — byte 0 overlaps `entry_wait` |
| `rebirth.pos` | 12 | |
| `rebirth.halo_despawn_wait` | 24 | |
| `rebirth.halo_number` | 28 | |
| `rebirth.halo_lower_wait` | 32 | |
| `damage.hitstun_tics` | 0 | |
| `damage.status_id` | 44 | |
| `catchmain.catch_pull_frame_begin` | 0 | |
| `capture.is_goto_pulled_wait` | 0 | Written by grabber's `CatchPull` on **victim** struct |
| `thrown.status_id` | 0 | Queued follow-up status after throw anim |

---

## Out-of-union mirrors (port bridges)

| Field | Location | Bridge role |
|-------|----------|-------------|
| `dead_gate_wait` | `FTStruct` | Authoritative dead countdown mirror for netplay snapshot invariants (`dead.wait` aliases entry/catch at union +0). |
| `fp->lr` | `FTStruct` | Combat facing outside union; cleared to 0 during Appear SetStatus (upstream). |

**Removed (2026-06-03):** Appear `hit_lr` cache / `GetEntryLR` fallback; CatchWait `shuffle_tics` throw mirror — upstream parity + accessor migration made these unnecessary offline.

---

## Known cross-status / cross-fighter writers

| Writer | Accessed overlay | Victim/grabber | Risk |
|--------|------------------|----------------|------|
| `ftCommonAppearProcPhysics` | `entry` translate/rotate | self during character Appear | TopN translate only (upstream); no `entry.lr` repair |
| `ftCommonAppearSetStatus` | `entry.*` | self | Saves combat `fp->lr` into `entry.lr`, clears `fp->lr` to 0 |
| `ftCommonCatchPullProcUpdate` | `capture.is_goto_pulled_wait` | **victim** (`catch_gobj`) while grabber in CatchPull | Correct owner is victim capture overlay |
| `ftCommonCatchWaitProcInterrupt` | `catchwait.throw_wait` | grabber in CatchWait | Direct countdown (upstream) |
| `ftMainSetStatus` / motion figatree | entire blob | self | New status may read stale overlay bytes until init proc runs |
| Snapshot save/load | entire blob | all fighters | ~~Blind memcpy~~ **Resolved by C2a/C2b** (see below) |
| `ftCommonAttackS4CheckInterruptTurn` | `turn.lr_dash` | self in Turn | Same bytes as `attack4.lr` at union +0x10; must use turn accessor |
| `ftCommonDamageInitDamageVars` | `damage.*` | self pre-SetStatus | Writes damage overlay while prior `status_id` still active until `ftMainSetStatus` |
| Thrown motion `SetDamageThrown` / `ftCommonThrownProcStatus` | `damage.script_id` | self in Thrown | Stages throw release script before damage status owns the blob |

---

## Witness cross-overlay allowances (2026-06-02)

Legitimate cross-overlay touches that must not log `witness stomp`:

| Pattern | Fix |
|---------|-----|
| Turn + `attack4.lr` read | Code: `ftCommonAttackS4CheckInterruptTurn` uses `ftStatusVarsTurn(fp)->lr_dash` |
| Any status + `damage.*` during `ftCommonDamageInitDamageVars` | Witness: `syNetplayStatusVarsWitnessEnterDamageInit` depth gate |
| Thrown + `damage.*` (motion events, proc_status) | Witness: allow `accessed=damage expected=thrown` |
| Turn/Dash/… + `entry.*` while `camera_mode` Entry/Explain | Witness: allow `accessed=entry` when `gmcamera.c` reads `entry.lr` (`netplay_witness_soak1_false_stomps_2026-06-11.md`) |
| Dash/Run/Wait/… + `turn.*` (persistent `lr_turn`) | Witness: allow `accessed=turn` for `status_id` in Wait…Ottotto |

Ownership table also tags `ThrownKirbyStar` / `ThrownCopyStar` as **thrown** overlay.

---

## Witness env gate

Set `SSB64_NETPLAY_STATUSVARS_WITNESS=1` (any non-empty value except `"0"`) to log:

- `witness armed` once per process
- `witness stomp` when accessed overlay ≠ expected for current `status_id` / `camera_mode`
- `corrupt entry_lr`, `corrupt catchwait`, `corrupt dead_gate`, `corrupt kneebend_stuck` when legacy mirrors diverge from union bytes

Integrity checks read raw `fp->status_vars.common.*` (never `ftStatusVars*()` — re-entrancy would recurse into this hook).

```
SSB64 NetStatusVars: witness stomp tick=… player=… status_id=… accessed=… expected=… hit_lr=… shuffle_tics=… fp_lr=…
```

Ownership table covers common `status_id` 0 … `nFTCommonStatusSpecialStart-1` (220). Character Appear statuses fall back to **expected = entry** when `camera_mode == nFTCameraModeEntry`.

---

## Migrated call sites (Milestone 1)

| Phase | Files | Overlays routed |
|-------|-------|-----------------|
| 2 (hotspots) | `ftcommonentry.c`, `ftcommoncatch2.c`, `ftcommonthrow.c`, `ftcommonrebirth.c` | entry, catchwait, rebirth |
| 3 (extended) | `ftcommondead.c`, `ftcommoncapturepulled.c`, `ftcommonthrown1.c` | dead, capture, thrown |
| 4 (batch 1) | `ftcommontwister.c`, `ftcommontarucann.c`, `ftcommoncatch1.c`, `ftcommondamage.c`, `ftcommonguard1.c` | twister, tarucann, catchmain, damage, guard |
| 5 (batch 2) | `ftcommonguard2.c`, `ftcommonwalldamage.c`, `mpcommon.c`, `ftcommonthrown2.c`, `ftmain.c` | guard, damage |
| 6 (batch 3) | `netrollbacksnapshot.c`, `netsync.c`, `grjungle.c`, `ftcommonitemthrow.c`, `ftcomputer.c` | guard, twister, tarucann, dead, rebirth, attackair, captureyoshi, attack4, itemthrow, escape, cliffwait, downwait |
| 7 (batch 4) | `ftcommoncaptureyoshi.c`, `ftcommondokan.c`, `ftcommonitemshoot.c`, `netplay_rebirth_gate.c`, `netplay_sim_quantize.c` | captureyoshi, dokan, fireflower, rebirth |
| 8 (batch 5) | `ftcommonturn.c`, `ftcommonsquat.c`, `ftcommonfallspecial.c`, `ftcommoncapturekirby.c`, `ftcommonattack1.c` | turn, squat, fallspecial, capturekirby, attack1 |
| 9 (batch 6) | `ftcommoncliffclimb.c`, `ftcommondownwaitbounce.c`, `ftcommonattacks4.c`, `ftcommonattack100.c`, `ftcommonsleep.c` | cliffmotion, cliffwait, downwait, downbounce, attack4, attack100, sleep |
| 10 (batch 7) | `ftcommonhammerkneebend.c`, `ftcommonhammerfall.c`, `ftcommonhammerlanding.c`, `ftdonkeythrowff.c`, `ftdonkeythrowfkneebend.c`, `ftdonkeythrowfdamage.c`, `ftdonkeythrowffall.c`, `ftdonkeythrowflanding.c` | hammer, throwff, throwf, throwfdamage |
| 11 (batch 8 — sweep) | `ftcommoncapturecaptain.c`, `ftcommonrebound.c`, `ftcommondownattack.c`, `ftcommonescape.c`, `ftcommonget.c`, `ftcommonitemswing.c`, `ftcommonlanding.c`, `ftcommonpass.c`, `ftcommonattackair.c`, `ftcommonattacklw3.c`, `ftcommonattacklw4.c`, `ftcommoncliffcatchwait.c`, `ftcommondash.c`, `ftkirbyspecialn.c`, `ftyoshispecialn.c`, `ftkirbycopyyoshispecialn.c`, `ftcaptainspecialhi.c`, `gmcamera.c`, `netplay_pikachu_quickattack_gate.c`, `netplay_statusvars_witness.c` | capturecaptain, capturekirby, captureyoshi, rebound, downbounce, escape, lift, itemswing, landing, squat, attackair, attacklw3, attack4, cliffwait, turn, dead, entry, fallspecial, catchwait, kneebend |

Deferred: ~~C2 parallel storage, snapshot tag serialization~~ — **implemented (C2a + C2b, 2026-07-28); see "Approach C2 — implemented" below.**

**Phase A accessor sweep complete (batch 11):** all live `.c` call sites now route through `ftStatusVars*()` accessors. Remaining `status_vars.common.*` references are comments only (`ftcommonentry.c`, `netrollbacksnapshot.c`) plus the accessor definitions in `ftstatusvars.h`.

**Phase B snapshot guard sweep (`netrollbacksnapshot.c`):** status-scope gates on rollback paths that read/write union overlays — same pattern as `syNetRbSnapSanitizeCaptureYoshiEffectGobj` / `syNetRbSnapClearCoupledGObjPointersInStatusPassive`:

| Site | Guard |
|------|-------|
| `syNetRbSnapClearFighterEffectPointerIfMatch` | `syNetRbSnapFighterGuardEffectUnionOwned`, `syNetRbSnapFighterInYoshiEggLayScope`, `syNetRbSnapFighterInFoxReflectorScope` |
| `syNetRbSnapRebindFighterEffectGobjs` | guard/yoshi rebind only when live fighter owns the union slot; yoshi NULL clear only in egg-lay scope |
| `ring_save_player` diag | `attackair.rehit_timer` only in attack-air scope (yoshi egg-lay override unchanged) |
| `syNetRbSnapHashCaptureYoshiEffectGobjId` | early return unless egg-lay scope |
| Pikachu thunder-shock effect save/respawn | `attack4.gfx_id` only in `syNetRbSnapFighterInPikachuAttackS4Scope` |
| `syNetRbSnapFighterGuardEffectUnionOwned` (new) | guard scope minus Twister/TaruCann (offset 0x08 alias) |

---

## Phase 3 analysis notes (pre-soak)

Expected witness behavior for Sector Z Kirby/Yoshi soak:

1. **Intro Appear** — accessor tag `entry` with `camera_mode == Entry`; **no stomp** if ownership fallback works. Pre-accessor logs showed `entry_lr=0` with valid `hit_lr` — union stomp visible in NDJSON, not necessarily a witness mismatch (accessed overlay is still `entry`).
2. **CatchWait** — expect `catchwait` overlay only; premature throw if `throw_wait` stomped should show `shuffle_tics` > 0 while union reads 0 (witness may not fire if both accessed through accessor with CatchWait status).
3. **Rebirth** — expect `rebirth` overlay for status 7–9; deck canonicalize interactions documented separately in `docs/bugs/netplay_sector_z_intro_rebirth_quantize_2026-06-02.md`.

After soak: extend `syNetplayStatusVarsWitnessFillRange` for any false negatives; migrate next overlay batch per witness output.

---

## Approach C2 — implemented (2026-07-28)

The union-aliasing contract this map documents is now structurally removed under `PORT && SSB64_NETMENU`. Two phases; offline/vanilla layout untouched (decomp union + offline accessors unchanged).

### C2a — tagged exact snap (retires the scrub denylist)

| Piece | Where | What |
|-------|-------|------|
| `s16 status_vars_overlay` | `SYNetRbSnapFighterBlob` (`netrollbacksnapshot.c`) | Per-slot overlay tag from the ownership table (`syNetplayStatusVarsExpectedOverlay`). Never on the wire — digest-only protocol, both peers ship the same binary. |
| Exact capture | `syNetRbSnapCaptureFighterStatusVarsFromLive` | Copies the active overlay bytes verbatim; the netmenu build no longer runs `syNetRbSnapScrubInactiveStatusVarsInBlob` overlay `memset`s (that function is `#if !defined(SSB64_NETMENU)` now). |
| Apply tag validation | `syNetRbSnapValidateFighterStatusVarsTagFromBlob` | Real overlay mismatch → log + heal. Unowned (`None`) → quiet-clear stale owned tags (no TAG_HEAL flood). |
| Blob light lockstep | `syNetRbSnapHashFighterBlobLight` | Mirrors the live `syNetSyncHashFighterStructLight` folds for Squat / Landing / FallSpecial so blob-vs-live light stays green on the statuses the scrub used to poison. |

Supersedes the per-status scrub early-returns: `netplay_{squat_pass_wait,landing_allow_interrupt}_statusvars_scrub_fc_2026-07-28.md`, plus the older Turn / JumpAerial / KneeBend / Damage / Kirby-Stone / Firefox / Samus-charge / CliffMotion / PK-Thunder / Twister / TaruCann denylist arms.

### C2b — parallel overlay bank (authority moves off the union)

| Piece | Where | What |
|-------|-------|------|
| Sidecar bank | `port/net/sys/netplay_statusvars_bank.{h,c}` | `nFTStatusVarsOverlayCount` isolated overlay slots per fighter, keyed by sim player slot (~2.2 KB × 4). Union stops being shared storage — statuses can no longer stomp each other through it. |
| Accessor redirect | `ftstatusvars.h` via `syNetplayStatusVarsBankAuthoritySlot` | **Per-overlay migration, not global.** A migrated `ftStatusVarsX(fp)` returns the bank slot when `syNetplayRollbackSemanticsActive()`; offline modes (and the offline build) keep the vanilla union path. Migrated so far: **Turn**, **KneeBend**, **JumpAerial**, **Dead**, **Rebirth**, **Damage**, **Squat**, **Landing**, **FallSpecial** (2026-07-28). |
| Capture / apply | `netrollbacksnapshot.c` | Owned: `bank[expected]` + tag. Unowned: union projection + tag=`None`. Apply restores bank[tag] (or clears live on `None`). Migrated overlays that outlive their status additionally get always-captured blob sidecars (`turn_vars`, `squat_vars`). |
| Session reset | `syNetRollbackReset` (`netrollback.c`) | `syNetplayStatusVarsBankResetSession()` at session reset; slots lazily init per fighter on first bank access. |

> **Correction (2026-07-28):** earlier revisions of this table claimed a global `FT_STATUSVARS_PTR` accessor redirect plus `ftMainSetStatus` / `ftManagerInitFighter` bank wiring. Those were never implemented — the bank was only written at snapshot apply, so `bank[expected]` captures served stale bytes and mid-status loads projected them over the live union (the Turn `lr_dash` wipe, seed `3066947259`). See `docs/bugs/netplay_turn_statusvars_bank_authority_2026-07-28.md`. Migration is now explicitly per-overlay: redirect the accessor, verify SetStatus initializes every field, and add a blob sidecar only if the overlay is read outside its owning status.

The union becomes a **projection** of the live overlay for residual raw readers (character arms that read a single `status_vars.<char>.*` family share one consistent view and are not an aliasing hazard). Residual raw common-overlay *writers* are the remaining C2b risk — the witness + audit own them; a bank write does not flow back into a raw reader that caches a stale union pointer.

### C2 follow-up — unowned `live_overlay` lifecycle (2026-07-28)

Soak `4092906820` proved scrub-class FC gone and 0× PEER, but ~900× `STATUSVARS_TAG_HEAL expected=-1` from sticky prior overlays on Wait/Jump/Fall/Pass. Fixed in `docs/bugs/netplay_statusvars_tag_heal_unowned_sticky_2026-07-28.md`. Remaining MATCH-input FC is SoftLip `topn_tx` (physics), not status_vars.

### Verification status

- Build: netmenu target compiles clean (no new warnings).
- Soak gate (C2a + C2b): Dream Land Pass + Landing→Turn — scrub-class FC cleared on seed `4092906820`; re-soak after TAG_HEAL lifecycle fix. Watch for near-zero `STATUSVARS_TAG_HEAL`, no scrub-class MATCH FC, SoftLip `topn_tx` tracked separately.
