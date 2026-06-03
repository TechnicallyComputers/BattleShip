# Yoshi's Island cloud evaporate stall in netplay VS

**Date:** 2026-05-30  
**Scope:** `decomp/src/gr/grcommon/gryoster.c`, `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netsync.c`  
**Status:** FIX SHIPPED (soak pending) — extended 2026-05-30 after Phase 2 gate patch insufficient

## Symptoms

Yoshi's Island (`nGRKindYoster`) in **live netplay VS** (AppImage / rollback optional):

- Cloud platforms never evaporate under a standing fighter; works offline.
- `SSB64_NETPLAY_YOSTER_CLOUD_DIAG=1`: `stand=1` for hundreds of ticks while `gate=0`, `ptimer=-1`, `pressure=0`, `translate_y=0.00`, `mobj=0` (Phase 3 signature).

Reproduced Linux x86↔x86 with `SSB64_NETPLAY_SIM_F32_QUANTIZE=0` — not cross-ISA or quantize.

## Root cause

Stacked failures in VS netplay:

1. **Mat-anim gate stall** — `grYosterUpdateCloudSolid` originally required `mobj->anim_wait == AOBJ_ANIM_NULL`. After solid mat-anim attach (`anim_id = -1`), `anim_wait` could stall at `0.0f`.
2. **Null MObj after rebind (Phase 3)** — `grYosterRebindCloudDobjs` repointed `cloud->dobj[]` at live display DObjs that no longer had `MObj`s attached. `grYosterUpdateCloudSolid` returned early on `mobj == NULL`, skipping pressure, translate, and yakumono position updates entirely. Diag showed `matanim=0`, `translate_y=0` while `altitude` was valid.
3. **Null root DObj after rollback (Phase 6)** — rollback particle reset can leave `cloud->gobj` live but `DObjGetStruct(gobj) == NULL`. Phase 5 only rebuilt when `root->child == NULL`; rebind returned early on null root, so repair never ran. Diag: `translate_y=0.00`, `dobj0=0`, `anim_id=-1` stuck while sim (`pressure`, `gate`) works.
4. **Per-tick anim requeue (Phase 8 regression)** — `grYosterRequeueCloudAnimIfPresentationReady` ran before `grYosterUpdateCloudSolid`, resetting `anim_id` to `0` each tick and closing the pressure gate; `stand=1` but `pressure=0` forever.
5. **Collision X/Z drift after rollback (Phase 7–8)** — snapshot restore copied `sc->translate` X/Z onto roots; all three clouds could share one X while Y varied → vertical collision stack. Full **`grYosterRebuildCloudDobjTree`** did not fix this and re-triggered spawn anims after countdown.

6. **Hollow presentation after snapshot load — particle + invisibility never play (Phase 9 → Phase 10)** — A clean no-resim soak log (`rb_resim=0 rb_applied=0` everywhere) showed a single `LOAD_HASH_DRIFT`/`SYNCTEST_FAIL` at **tick 509**; from tick 600 on **every cloud** was hollow: `root=0 dobj0=0 mobj=0` while `map_head`/`spawn_x` stayed valid (i.e. `cloud->gobj->obj == NULL`). `DObjGetStruct(gobj)` is literally `(DObj*)gobj->obj` — the same pointer the renderer draws from. The snapshot load at 509 tore the cloud GObj's DObj payload to NULL. The pressure/evaporate/line-toggle sim still ran (it precedes the null-root guard), so collision "dissipate under feet" + positioning worked, **but** the vapor particle (`grYosterCloudVaporMakeEffect`, gated behind `DObjGetStruct(gobj)==NULL → return`) never spawned and the evaporate mat-anim never attached (`anim_id` stuck at `1`, `matanim=0`). **Phase 9 removed `grYosterRebuildCloudDobjTree`, which was the only code that re-established the DObj tree after a load** — leaving `grYosterRepairCloudPresentation`'s rebind a no-op on a null root.

7. **Cloud GObj identity collision on restore — render never sinks/fades (Phase 11)** — After Phase 10 fixed the hollow tree, the cloud *render* still didn't drift down with the collision plane and never faded out on evaporate. Log proof: cloud 1's root read its own spawn (`3855`/`1740`) while solid but flipped to **cloud 0's exact spawn** (`7335`/`-60`) the instant it evaporated — because the diag runs right after `grYosterUpdateCloudSolid(1)` (which writes the root in solid but not evaporate), so in evaporate it saw whatever `update(0)` last wrote. Root cause: all three cloud GObjs are created with `gcMakeGObjSPAfter(nGCCommonKindGround, ...)`, so `gobj->id == nGCCommonKindGround` for all three. The yoster snapshot restore re-resolved them with `gcFindGObjByID(gobj_id)`, which returns the **first** ground GObj — collapsing `clouds[0/1/2].gobj` onto one. The sim then drove only that single GObj (translate, evaporate mat-anim/fade, particle); the other two visible cloud GObjs were never updated → static render, no fade. Collision still worked because each `mpCollisionSetYakumonoPosID` uses its own `cloud_id` + spawn. (Vanilla never has this bug: it keeps the per-slot `clouds[i].gobj` pointers from init and never looks them up by id.)

## Fix (Phase 10–11 — 2026-05-30)

The DObj tree genuinely **must** be re-established after a snapshot load nulls it (Phase 9's pure-rebind approach can't recover a null root). Phase 10 restores a **clean one-shot re-establish** and keeps the collision decoupled from the saved snapshot translate. Phase 11 fixes the GObj identity collision so the sim drives the correct per-slot cloud.

| Layer | Change |
|-------|--------|
| Sim gate | `grYosterCloudPressureGateOpen`: open when `anim_id == -1` (attach consumed) **or** mat-anim idle — no `mobj` required. |
| Solid update | Only require `cloud->gobj`; run pressure + translate + yakumono pos even when `dobj[0]->mobj == NULL`. |
| **DObj re-establish** | **`grYosterReestablishCloudDobjTree`**: when `grYosterRebindCloudDobjs` finds a **null root** (post-load hollow), `gcRemoveDObjAll` → `gcSetupCustomDObjs` → `gcAddAnimJointAll` → attach display children → `gcPlayAnimAll`. Latched (`sGRYosterCloudReestablishFailed`) so a failing setup can't thrash; resets the moment the root is healthy again. |
| MObj repair | `grYosterEnsureCloudDisplayDobjs` re-attaches `MObj`s when display DObj exists but `mobj` is null. |
| Root translate | **`grYosterApplyCloudRootTranslate`**: every tick anchor **X/Z** from init `sGRYosterCloudSpawnTranslate`, **Y** from `altitude - pressure`. Re-establish uses the same anchor — **not** the saved snapshot translate. |
| Snapshot restore | **`grYosterRepairCloudPresentation`** (rebind→re-establish + anchor); **does not** write `sc->translate` X/Z onto the root (that was the vertical-stack bug — saved translate was zeroed when captured while hollow). |
| Root anim proc | `grYosterCloudPlayAnimAllProc` (prio 5): `gcPlayAnimAll` then reapply anchored translate. |
| Anim consume | `grYosterUpdateCloudAnim` sets `anim_id = -1` only when at least one DObj received mat-anim attach. |
| Anim re-queue | On leaf repair **and** on re-establish (`grYosterRequeueCloudAnimAfterRepair`) — **not** every tick in `grYosterProcUpdate`. |
| Altitude | `grYosterSampleYakumonoLineBaseY` from collision vertices when yakumono translate is 0. |
| Rebind | `grYosterRebindCloudDobjs` each PORT tick + on snapshot restore via `grYosterRepairCloudPresentation`. |
| **GObj identity** | **`sGRYosterCloudGobjs[slot]`** captured in `grYosterInitAll` + `grYosterGetCloudGobj(slot)`. The yoster snapshot restore now resolves `lc->gobj = grYosterGetCloudGobj(ci)` **instead of `gcFindGObjByID(gobj_id)`** (all three clouds share `id == nGCCommonKindGround`, so the id lookup collapsed them onto one GObj). Table is non-rollback state; refreshed every stage load; clouds are never recreated mid-match. |
| Snapshot | `dobj0_anim_wait_bits` + `syNetRbSnapRestoreYosterCloudPresentation`. |
| Diag | Log `root`, `root_child`, **`reest`** (re-establish ran this tick), `dobj0`, `mobj`, **`gobj`** (resolved GObj pointer — three clouds must be distinct), `gate`, `root_x`/`translate_y`/`root_z`, `spawn_x`/`spawn_y`/`spawn_z`. |

## Test plan

- Netplay VS on Yoshi's Island: stand on **each** cloud (0/1/2); `yoster_cloud_fighter` must show `match=1` for the correct line. Diag: three distinct `spawn_x` values; `root_x`/`root_z` track spawn each tick.
- After a snapshot load (e.g. `LOAD_HASH_DRIFT`/`SYNCTEST_FAIL`): expect a `reest=1` row, then `root=1 root_child=1 dobj0=1 mobj=1` from the next tick, with the vapor particle + evaporate fade now playing on state change.
- The three clouds must show **distinct `gobj=` values**; each cloud's `root_x` must equal its own `spawn_x` through the whole solid↔evaporate cycle (no jumping to a neighbor's spawn). The visible cloud should now drift down with the collision plane and fade out on evaporate.
- Rollback resim mid-cloud-pressure: lifecycle continues after load.
- Offline Yoshi's Island: cloud evaporate/regrow unchanged.

## Related

- [`netplay_yoster_cloud_rollback_2026-05-29.md`](netplay_yoster_cloud_rollback_2026-05-29.md) — prior dobj rebind SIGSEGV fix.
