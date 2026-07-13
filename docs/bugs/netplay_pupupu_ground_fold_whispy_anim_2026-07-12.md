# Netplay: Dream Land Pupupu ground_fold / Whispy anim desync (2026-07-12)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom (soak1 `914062045`, seed `1702773923`)

Android client / Linux host, Dream Land (`gkind=6` / Pupupu), Kirby ditto. After GGPO @552, epoch-7 resim never agreed baseline; Android froze at sim≈551 and received `VS_SESSION_END`; Linux deepened then unilaterally promoted and advanced.

Compatible seal apply worked (`COMPATIBLE_APPLY`); this was **not** the seal-tuple stall.

## Root cause

1. **Live `ground_fold` fork at tick 538** — `map_hash_save` triples matched through 537; at 538 `kin` still matched (`0x6EF1A305`) but `ground_fold` diverged (Android `0x22EFDAA9` vs Linux `0x93716642`). That is `SYNetRbSnapGroundPupupu` (Whispy FSM / flowers / `lr_players`), not yakumono kinematics.

2. **Whispy FSM gates on `map_gobj[*]->anim_frame <= 0.0F`** — same cross-ISA leftover class as fighter anim-end harden. Turn/Open/Stop/flower transitions can disagree by one tick on aarch64 vs x86_64.

3. **Observability hole** — live FC / `sim_state_tick` `mph=` used **kinematics only**; resim baseline `map=` uses **kin + ground_fold**. Pupupu drift was invisible until a GGPO load compared full map hashes.

4. **Recovery policy** — after `DEEPER_MAX` on map-only mismatch, Linux still fell through toward unilateral resim complete while Android stayed capped → asymmetric hang.

## Fix

1. **Harden** — `syNetplaySnapGobjAnimFrameToEndIfNearZero` / `syNetplayMapGobjAnimFrameEnded`; `syNetplayHardenPupupuWhispyMapAnimBeforeSim` from VS BeforeSim; Pupupu Turn/Open/Stop/Blink/flower gates use Ended helper under `PORT && SSB64_NETMENU`.

2. **Dump** — when `SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1`, `pupupu_ground` logs Whispy scalars next to `map_hash_save`.

3. **Hash authority** — NETMENU live FC validation and `sim_state_tick` `mph=` use `syNetRbSnapshotComputeMapHashLive()`; log also prints `mph_kin=`.

4. **Fail closed** — `RESIM_BASELINE_MISMATCH` deeper exhausted + gameplay-only map drift → `PEER_SNAPSHOT_DIVERGE` (no unilateral promote).

## Verify on re-soak

- Dream Land cross-ISA: no silent `ground_fold` fork with matching `mph_kin`; if fork remains, `pupupu_ground` names the field at first diverge tick.
- Prefer early FC / live `mph` diverge over late GGPO baseline map-only storm.
- Map-only deeper exhaust should log `map-only deeper exhausted … PEER_SNAPSHOT_DIVERGE`, not one peer advancing alone.
