# Retire jibaku stick absorb — portable frame-delta GGPO stick (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Bucket:** input contract / exportability  
**Supersedes:** [`netplay_jibaku_stick_absorb_material_ggpo_2026-07-26.md`](netplay_jibaku_stick_absorb_material_ggpo_2026-07-26.md) (narrow absorb), July-17 full absorb under netmenu

## Motivation

Jibaku/bound stick absorb was **move-context policy** (live Ness status + same-intent heuristics). It is not exportable to other games. It was introduced because `ness_pk_defer` turned every mid-jibaku stick GGPO into a span storm; that TryBegin defer is already retired. Remaining absorb fought invent/GGPO reliability (inputs-agree PEER when material REPLACE was Promote-only).

Portable GGPO stick contract:

| Rule | Behavior |
|------|----------|
| Buttons differ | Rewind |
| Release (analog→neutral) | Rewind |
| Optional numeric micro deadband | Promote-only (frame math only) |
| Else stick REPLACE | GGPO |

No fighter status / intent gates on the GGPO path.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| `syNetInputJibakuStickAbsorbBlocksGgpo` | Stub → always `FALSE` |
| `syNetplayNessPlayerInJibakuStickAbsorbScope` | Stub → always `FALSE` |
| Hold-last invent | Remove `hold_last_ness_jibaku_absorb` skip |
| LEDGER_REFRESH / QueueOrWiden | Remove jibaku absorb skip branches |
| Keep | Completed-sim numeric `micro_stick` deadband; dead-ghost stick absorb; BRANCH_DEFERRED same-stick |

## Acceptance

Matched APK + Linux, Ness ditto through jibaku with stick noise:

- **0×** `skipped class=jibaku_stick` / `ledger_jibaku_stick` / `hold_last_ness_jibaku_absorb`
- Mid-jibaku stick REPLACE → short `resim begin` (span ≪ 20)
- No inputs-agree PEER from absorbed material aim
- Invent stability is the follow-on lever (not re-adding status absorb)
- If vertical re-launch / `jibaku_post_cull` returns → resim reconstruct, not absorb
- **Follow-on (2026-07-26):** [`netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md`](netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md) — predicted micro after committed jibaku snap → Promote-only (`snap_post_jibaku_micro`)

## Related

- [`netplay_ness_pk_defer_retire_input_resim_2026-07-26.md`](netplay_ness_pk_defer_retire_input_resim_2026-07-26.md) — TryBegin defer retired
- [`netplay_ness_jibaku_stick_ggpo_storm_eff_load_2026-07-17.md`](netplay_ness_jibaku_stick_ggpo_storm_eff_load_2026-07-17.md) — original absorb (historical; defer was the real storm)
- [`netplay_jibaku_ledger_refresh_stick_resim_launch_2026-07-26.md`](netplay_jibaku_ledger_refresh_stick_resim_launch_2026-07-26.md) — absorb hole; policy superseded
- [`netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md`](netplay_jibaku_post_launch_micro_ggpo_relaunch_2026-07-26.md) — post-retire vertical relaunch
