# Netplay DK / Link intro resim anchor scope (2026-06-11)

**Soak:** soak1 Linux + Android, DK vs Link Dream Land, `INJECT_TICK=240` during dual Appear intro.

## Symptom

Forced rollback at tick 240 during DK `AppearL` + Link `AppearR` intro. Anchor walkback exhausted at `load=223`; `resim begin aborted` with `anchor_probe_unresolved=1` → `resim_load_fail` → CSS (not SIGSEGV). `step_figh_fail=1`, `match_a=1`; joint TRS / `fold_topn_ty` / `fold_coll_pos_diff_y` drift on probe +1 sim.

## Root cause

`syNetRbSnapFighterInAppearPresentationScope` and blob twin only recognized Kirby/Yoshi Appear statuses. DK/Link (and the rest of the roster) were treated as non-Appear during intro anchor probe, so:

- `intro_anchor_probe` stayed FALSE (no Entry peer, no Kirby/Yoshi Appear).
- Skipped: TopN MPColl pre-sim rebind, `SyncAppearGobjTranslateFromTopN`, `ReconcileAnchorProbeAppearSteady`, intro sim trail, `fhash_light` step oracle.
- Full fighter hash compared on +1 sim without Appear reconcile → walkback failure.

## Fix

Route Appear presentation scope through `dFTCommonEntryAppearStatusIDs` (same table as spawn LR infer) plus Captain/Ness Appear chains and Boss Appear. Export `syNetRbSnapshotStatusInAppearPresentationScope`; reuse in `netplay_sim_quantize.c` end-of-tick intro joint canonicalize.

## Verify

Re-run soak1 DK/Link with `SSB64_NETPLAY_ROLLBACK_INJECT_TICK=240`. Expect `intro_anchor_sim_trail` lines, `ReconcileAnchorProbeAppearSteady` activity, `step_figh_fail=0` on walkback, resim completes without `anchor_probe_unresolved`.

## Post-fix soak (2026-06-11)

Intro resim at 240 **completed** on both peers. Follow-on desync was **not** another inject — see [`netplay_battle_go_resim_wait_skew_2026-06-11.md`](netplay_battle_go_resim_wait_skew_2026-06-11.md) (GO 1-tick skew after seal-wait interface drift → FC recovery at 480).

Inject @230 soak: intro resim still succeeds; stall @241 from stale outcome correction — see [`netplay_outcome_correction_post_resim_deadlock_2026-06-11.md`](netplay_outcome_correction_post_resim_deadlock_2026-06-11.md).
