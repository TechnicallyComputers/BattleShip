# Pupupu flower LoopStart stuck after presentation repair → map-only PEER (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** session `802174271` — Android client (lp=1) ↔ Linux host (lp=0), matched post publish-gap builds  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Kill:** map-only `PEER_SNAPSHOT_DIVERGE` deeper exhaust @load ~1401/1403 (`figh` match); Android `VS_SESSION_END` @1413  
**Bucket:** `PEER_SNAPSHOT_DIVERGE` / Dream Land `ground_fold`

## Symptom

Publish-gap / Turn–Dash class accepted (session reached ~1413). Then:

```
RESIM_BASELINE_MISMATCH map-only deeper exhausted … peer_map≠local_map — PEER_SNAPSHOT_DIVERGE
PEER_SNAPSHOT_DIVERGE suppressed (resim_seal_wait) … peer figh == local figh
```

`kin` matched; `ground_fold` forked. Whispy `wind_dur` / `blink` matched.

## Timeline

| Tick | Both (first save) | After GGPO resim 1397→1400 |
|------|-------------------|----------------------------|
| 1396 | `fl_b=2/0` WindLoopStart | load |
| 1397 | `fl_b=3/15` WindLoop — **fold match** | Android resim save: `fl_b=2/0` stuck |
| 1398+ | — | Linux `3/15` vs Android `2/0` → fold diverge |

GGPO chain through Blow (`status=4`) around Open→Blow @1381–1382 then stick corrections @1379…1397.

## Root cause

`grPupupuFlowersBackLoopStart` / `FrontLoopStart` gate WindLoopStart→WindLoop on `syNetplayMapGobjAnimFrameEnded(map_gobj[2|3])`.

Post-load / resim `grPupupuWhispyRepairPresentationCosmetic` (and forward-texture refresh during Blow) `gcPlayAnimAll` the flower texture clip from **full length**. Near-zero snap cannot clear that. One peer stays in WindLoopStart (`2/0`) while the other already entered WindLoop (`3/15`) → `SYNetRbSnapGroundPupupu` / `ground_fold` fork → map-only baseline PEER.

Live first-pass @1397 matched; rewind+repair broke the re-entry.

## Fix

| Site | Change |
|------|--------|
| `syNetRbSnapPinPupupuFlowerLoopStartAnimEnded` | When flower status ∈ [LoopStart, WindStop], force `map_gobj[2\|3]` anim_frame=0 |
| `RefreshPupupuWhispyMapAnimAfterLoad` | Call pin after near-zero snap |
| `RepairPupupuWhispyPresentationAfterLoad` | Call pin after `RepairPresentationCosmetic` |
| `RepairPresentationCosmetic` | Pin after PlayAnim **only while `IsResimulating`** (preserve live LoopStart duration) |

## Acceptance

Matched APK + Linux on Dream Land through a Blow with GGPO spanning WindLoopStart→WindLoop:

- Both peers `fl_b` / `fl_f` stay matched across resim (no Android stuck `2/0` vs Linux `3/15`)
- No map-only deeper exhaust from this flower class with matched figh/rng
- Live LoopStart duration unchanged (pin is resim/post-load only)

## Related

- [`netplay_post_resim_local_publish_gap_branch_deferred_2026-07-26.md`](netplay_post_resim_local_publish_gap_branch_deferred_2026-07-26.md) — accept soak that surfaced this
- [`netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md`](netplay_pupupu_ground_fold_whispy_anim_2026-07-12.md) — anim-end harden family
- [`netplay_pupupu_whispy_open_blow_rng_fc_2026-07-12.md`](netplay_pupupu_whispy_open_blow_rng_fc_2026-07-12.md) — mouth Open→Blow tick gate
- [`netplay_pupupu_whispy_stop_wait_tick_gate_2026-07-18.md`](netplay_pupupu_whispy_stop_wait_tick_gate_2026-07-18.md) — Stop→Wait mouth leftover
