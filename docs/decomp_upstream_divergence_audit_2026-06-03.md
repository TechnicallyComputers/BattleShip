# Decomp upstream divergence audit (2026-06-03)

Tracked checklist for fork `decomp/` vs **JRickey’s maintained PC port** ([`JRickey/ssb-decomp-re` `port-patches`](https://github.com/JRickey/ssb-decomp-re/tree/port-patches)).

## Policy (2026-06-03)

**We are not patching the offline build.** Offline is JRickey’s release product — sim and port behavior as shipped. Netmenu is where fork netplay work lives.

| Rule | Detail |
|------|--------|
| **Offline baseline** | JRickey `port-patches` at the release submodule SHA — not N64 `main`, not fork deltas |
| **Fork delta default** | `#if defined(PORT) && defined(SSB64_NETMENU)` (+ `syNetplayRollbackSemanticsActive()` for forward-sim policy) |
| **Promotion** | If a netplay fix also helps vanilla offline → JRickey review → widen gate or upstream merge — never self-ship in offline |
| **`#ifdef PORT` alone** | Only for code **already in JRickey’s port-patches** at the release baseline (reloc, LUS bridge paths JRickey owns) |
| **Assumption** | Any fork-only `#ifdef PORT` block (reloc guards, null checks, wrappers, accessors, diag) is netplay work until JRickey adopts it |

**Compare fork delta:**

```bash
# Replace <release-sha> with the decomp submodule SHA from the official release tag.
git -C decomp diff <release-sha> --stat -- ':!src/relocData'
git -C decomp diff <release-sha> --name-only -- ':!src/relocData' | rg '#ifdef PORT'
```

**Verify offline parity:** `SSB64_NETMENU=OFF` — gameplay matches JRickey release (1P, VS intro, grab, specials). No netplay symbols: `nm build/BattleShip | rg 'syNet(Rollback|play|Input)|mm_|juice_'`.

---

## Phase 0 — Already fixed (intro / grab)

- [x] Remove fork-only Appear ProcPhysics TopN ±90° hammer (`ftcommonentry.c`)
- [x] Revert CapturePulled invalidate/rebuild to upstream (`ftcommoncapturepulled.c`)
- [x] Revert catchwait `shuffle_tics` throw mirror to upstream (`ftcommoncatch2.c`, `ftmain.c`)

---

## Phase 1 — Quick wins (offline parity, low risk)

- [x] Revert Wait proc indirection → upstream `ftPhysicsApplyGroundVelFriction` in status table
- [x] Remove dead Wait-entry TopN re-apply block in `ftMainSetStatus`
- [x] Clean redundant `#ifdef PORT` duplicate include in `ftcommonwait.c`
- [x] Remove unconditional `port_log` spam in `ftcommoncatch1.c`
- [x] Gate `ftmain.c` SetStatus / hidden-parts / motion-watchdog logs behind `SSB64_DECOMP_DIAG`

---

## Phase 2 — Wrap net-only decomp symbols

- [x] `#if defined(PORT) && defined(SSB64_NETMENU)` on `ftMainRebindStatusProcs`, `ftMainRefreshFigatreeVisual`
- [x] Same gate on ground-obstacle helpers + `ftParamInvalidateFighterTransformFromRoot`

---

## Phase 3 — Fork gameplay policy

- [x] Revert Appear `GetEntryLR` / `hit_lr` mirror — direct `ftStatusVarsEntry(fp)->lr`
- [x] Gate orphan item sweep in `ftCommonWaitSetStatus` behind `PORT && SSB64_NETMENU`
- [x] Gate capture ProcMap NULL guards behind `PORT && SSB64_NETMENU`

---

## Phase 4 — Offline binary hygiene (CMake + fork mirrors)

**Goal:** Strip netmenu-only tooling and snapshot mirrors from `SSB64_NETMENU=OFF`.

- [x] Link `debug_tools/` (GBI + Acmd trace) **only when `SSB64_NETMENU=ON`**; offline uses `port/stubs/debug_trace_stubs.c`
- [x] **`dead_gate_wait`** — field + mirror getters netmenu-only; offline uses JRickey union `dead.wait` path
- [x] Gate Ness PK Thunder `#ifdef PORT` weapon wrapper (`ftnessspecialhi.c`) — netmenu-only; offline JRickey paths
- [x] Gate Samus charge `PortValidateCoupledCharge` + coupled-charge helpers — netmenu-only; offline JRickey paths
- [x] CMake: drop offline net include injection on decomp TUs when no net headers needed (Phase 5a subset)

**Verify:** Release offline build smaller vs prior; offline Ness PK Thunder + Samus charge shot; netmenu automatch soak for same.

**Deferred:** Full `dead_gate_wait` / status-var mirror removal and dead/rebirth/stock regression pass (offline OK today; revisit after accessor netmenu-only Phase 6).

---

## Phase 5 — JRickey baseline diff pass (fork `#ifdef PORT` audit)

For each file in `git diff <release-sha>`:

- [x] Classify remaining diffs: **JRickey-owned** vs **fork-only** (gate `PORT && NETMENU` or revert)
- [x] Gate fork-only stage rollback repair (Yoster clouds, DK tarucann), Saffron door offline anim, ftmain null guards, twister stale shoot, weapon instance IDs, objman eject trace, taskman offline tick-wait logs, grhyrule canonicalize wrappers
- [x] Document JRickey-owned blocks in-file `/* PORT (JRickey): … */` when touched (ongoing on touch)

**Kept under `#ifdef PORT` offline (JRickey / port stability):** `PORT_RESOLVE`, `portAudioPurgeFGMs`, `portFixupMObjSub`, `objdisplay` stale dl_link guards, efmanager NULL `file_head`, `port_coroutine_yield`, Samus/Kirby escape 3rd arg.

### Phase 5a — Fighter camera + diag (done)

- [x] `ftkirbycopysamusspecialn.c` — coupled-charge helpers netmenu-only (mirror Phase 4 Samus)
- [x] `gmcamera.c` — netplay pause/wait zoom policy netmenu-only; JRickey NULL-camera guard kept
- [x] `ftcommonthrow.c` / `ftcommoncatch2.c` — netplay diag includes/log paths netmenu-only; JRickey `PORT_RESOLVE` kept
- [x] `taskman.h` — fix invalid `#ifdef PORT && …` → `#if defined(PORT) && defined(SSB64_NETMENU)`
- [x] CMake — shrink offline net include injection list (drop entry/wait/throw/catch2/gmcamera)

**Verify:** Offline VS camera zoom; Kirby Copy Samus charge; netmenu automatch camera at match start.

**Do not** blanket-remove JRickey port-patches reloc/audio paths — offline still needs JRickey’s PC port to boot.

### Phase 5b — Yoshi egg + Pikachu thunder + witness scaffolding (done)

- [x] `ftyoshispecialhi.c` — Port egg validate/charge helpers + `UpdateEggVectors` fork logic netmenu-only; offline JRickey simple vector sync
- [x] `ftpikachuspeciallw.c` — fork null guards on `MakeThunder` / collide netmenu-only; JRickey `thunder_gobj == NULL` spawn guard kept under `#ifdef PORT`
- [x] `ftstatusvars.h` — witness decls + `NoteAccess` netmenu-only (offline: no-op, no stub link)
- [x] `ftmariospecialn.c` — remove empty `#ifdef PORT` comment block

**Verify:** Offline Yoshi egg throw (up+B); Pikachu down+B thunder spawn; netmenu soak for same + egg rollback.

### Phase 5c — Stage hazards + sys glue (done)

- [x] `gryamabuki.c` / `.h` — offline restores priority-5 `gcPlayAnimAll` gate process; rollback API decls netmenu-only
- [x] `gryoster.c` / `.h` — per-tick rebind/reestablish/ApplyCloudRootTranslate netmenu-only; offline JRickey cloud Y update
- [x] `grjungle.c` / `.h` — tarucann cache/repair/GetPosition fallbacks netmenu-only; offline JRickey AddAnimOffset + pose getters
- [x] `ftmain.c` — controller/item/twister/sword fork null guards netmenu-only
- [x] `ftcommontwister.c` — stale tornado shoot + rebind netmenu-only
- [x] `wpmanager.c` / `.h` — weapon `instance_id` netmenu-only
- [x] `objman.c` — GObj eject trace ring netmenu-only; offline JRickey eject log
- [x] `taskman.c` — remove offline tick-wait `port_log` spam branch
- [x] `grhyrule.c` — twister canonicalize static helpers netmenu-only

**Verify:** Offline Saffron door anim; Yoster clouds; DK tarucann; Hyrule twister pickup; `nm build/BattleShip | rg syNet` empty.

## Phase 5d — Offline binary size gap (deferred)

**Observed:** Release offline AppImage ~14.76 MB vs JRickey release ~13.8 MB (~600 KB). Expected while `decomp/` remains the fork submodule; not pursuing decomp pin or file-by-file revert for size parity.

**Shipped before deferral:** `gryoster.c` missing `#endif` fix; `gryamabuki.c` held-pose netmenu gate; `ftmain.c` diag stub.

---

## Phase 6 — ftStatusVars accessors + witness (netmenu-only)

- [x] Gate accessor `NoteAccess` / witness decls behind `PORT && SSB64_NETMENU` offline (inline union access only offline; no per-read stub calls)
- [ ] Witness env `SSB64_NETPLAY_STATUSVARS_WITNESS=1` — netmenu soak only
- [ ] Parallel storage milestone (separate PR)

---

## Phase 7 — Debug / diag sweep

### Phase 7a — Fighter / item / opening boot logs (done)

- [ ] Grep `port_log` in `decomp/src/**` — delete or `SSB64_NETMENU` + env gate only (remaining: `objdisplay`, `audio`, `debug`, …)
- [ ] Netmenu agent logs — `SSB64_NETMENU` only
- [x] `SSB64_DECOMP_DIAG` — `ftMainDecompDiagEnabled()` env read netmenu-only; offline always-false stub (Release DCE)
- [x] `scsubsysfighter.c` — SetStatus begin/end logs netmenu + `SSB64_DECOMP_DIAG`
- [x] `ftmanager.c` — MakeFighter verbose trail netmenu + `SSB64_DECOMP_DIAG`
- [x] `itmain.c` — destroy-item trace logs netmenu-only; offline keeps null/zombie guards silent
- [x] `mvopeningroom.c` — opening boot / logo tree logs netmenu + `SSB64_DECOMP_DIAG`; offline uses vanilla display proc
- [x] `objanim.c` — unhandled-opcode / TraI trace netmenu-only; offline keeps anim terminate fix

### Phase 7b — Sys boot / scene loop logs (done)

- [x] `main.c` — Thread5 boot trail netmenu + `SSB64_DECOMP_DIAG`
- [x] `taskman.c` — scene load/start / malloc debug netmenu + `SSB64_DECOMP_DIAG`; fatal overflow logs netmenu-only
- [x] `scmanager.c` — runloop / env-override / scene-boundary sweep logs netmenu + `SSB64_DECOMP_DIAG`
- [x] `objman.c` — gobj alloc trace + eject ENTER/EXIT/DOUBLE-EJECT logs netmenu-only; offline keeps eject guards silent

| Variable | Purpose |
|----------|---------|
| `SSB64_DECOMP_DIAG=1` | `ftmain` SetStatus / hidden-parts logs (netmenu) |
| `SSB64_NETPLAY_CATCHWAIT_DIAG=1` | CatchWait throw decision |
| `SSB64_NETPLAY_HYRULE_TWISTER_DIAG=1` | Twister rider diag |

---

## Out of scope

- `src/relocData/**` — ROM extraction pipeline
- `src/netplay/**`, `port/net/**` — CMake-stripped when `NETMENU=OFF`
- Build yaml, menu data tables owned by JRickey release process

---

## Notes

- **Compile gate:** `#if defined(PORT) && defined(SSB64_NETMENU)` — fork work stripped from offline.
- **Runtime gate:** `syNetplayRollbackSemanticsActive()` — policy inside netmenu binary only.
- Related: [netplay_intro_facing_mirror_cleanup](bugs/netplay_intro_facing_mirror_cleanup_2026-06-03.md), [netplay_ness_grab_floor_sink](bugs/netplay_ness_grab_floor_sink_2026-06-03.md).
