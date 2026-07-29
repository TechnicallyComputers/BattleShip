# Attack-record fighter victims: shared `gobj->id` rebinds rehit to wrong player

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Soak:** `1020830879` seed `165379561` (Linux host / Android guest, Ness ditto)  
**Prior:** DamageE2 `proc_passive` rebind healthy earlier in match; kill ~4040 after longer soak.

## Symptom

- Through **4039** figh/item/P1 light MATCH (`cam` soft-diverged earlier).
- **4036:** PK Fire pillar (`nITKindNessPKFire`, kind=20) + P1 DamageN3.
- **4037:** matching `DAMAGE_RESIST` ColAnim both peers.
- Android light-resimmed **4039** after P0 B predict (`btn=0x4000 pred=1` → confirmed `0x0000`). During resim: `DAMAGE_RESIST … tick=4039 … colanim resim=1`.
- Post-resim P1 **dmg=10 hitlag=5** vs Linux first-pass **dmg=7 hitlag=3** (same TopN / `vel_dmg` / stack / `status_tics`).
- Kill: PEER@4040 `figh`+`item`+`cam`, `wpn`/`rng`/`anim`/`map` MATCH, `replay_determinism`. FC@4048 inputs MATCH; P1 both DamageN3 with TopN/`vel_damage` cascade.

## Root cause

Item/weapon/fighter attack-record victims were captured and restored via `gobj->id`. **All fighters share `gobj->id == nGCCommonKindFighter` (1000)**, so `syNetRbSnapResolveLiveGobj(1000)` returns the **first** fighter on the link list.

After mid-hitlag load@4038, PK Fire’s rehit record no longer pointed at P1 → resim allowed an extra ColAnim hit on the real victim. Same family as fighter grab/throw coupling (`gobj_id` ambiguity), applied to attack-record / `can_rehit_fighter` cooldown state.

Not SoftLip; not an input fork (FC inputs MATCH); not PK Fire preserve cull — blob fidelity of victim identity.

## Fix

1. Encode fighter attack-record victims as `SYNETRB_ATTACK_VICTIM_FIGHTER_TAG | player` on capture (`syNetRbSnapAttackVictimGobjIdForCapture`).
2. Restore via `syNetRbSnapResolveAttackVictimGobj` (player resolve; refuse legacy raw `1000` rather than rebind to the first fighter).
3. Apply path for fighter / item / weapon attack records uses the new resolver.
4. `syNetSyncAttackRecordVictimIdForFold` uses the same encoding so slot hash == blob == restore.

## Acceptance

Ness PK Fire (or any `can_rehit_fighter` item/weapon) that leaves a victim in hitlag across mid-window resim:

- Rehit cooldown still names the correct player after load.
- No extra ColAnim / damage tick on resim vs first-pass with matching inputs.
- No PEER `figh`+`item` with inputs MATCH from this class.

## Related

- [netrollback_fighter_coupling_gobjid_ambiguity_2026-06-27.md](netrollback_fighter_coupling_gobjid_ambiguity_2026-06-27.md) — same shared-`gobj_id` family (catch/capture)
- [netrollback_item_gobjid_ambiguity_resim_2026-06-26.md](netrollback_item_gobjid_ambiguity_resim_2026-06-26.md) — item kind-id ambiguity
- [netplay_damage_e2_setstatus_proc_passive_rebind_2026-07-29.md](netplay_damage_e2_setstatus_proc_passive_rebind_2026-07-29.md) — prior kill in same soak family
