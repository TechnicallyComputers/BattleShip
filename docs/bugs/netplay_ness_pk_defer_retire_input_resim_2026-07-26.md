# Retire Ness `ness_pk_defer` — input contract + immediate confirmed-input resim (2026-07-26)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak (pre-fix):** session `41294254` seed `4222942986` — Android client ↔ Linux host  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM` / protocol

## Symptom

Ness P0 AirHiHold→AirHiJibaku:

| Tick | Event |
|------|--------|
| 1475 | Owner `LOCAL_PUBLISH (23,79)`; remote `REMOTE_PUBLISH_SKIP hold_last_ness_pk_scope` still predicting `(67,69)` |
| 1476 | Both leave Hold→Jibaku on divergent sticks → `PHYSICS_FORK` / light onset (anim match) |
| 1477 | Ledger REPLACE `1475 (67,69)→(23,79)` queues GGPO |
| 1477–1525 | `try_begin_fail stage=ness_pk_defer` + `lift_livecap`; target grows to **1526** (span **51**) |
| Late resim | Sealed sticks correct; `jibaku_post_cull hold_skip=1` every tick; status stays Hold — bad resim |

Soft-stable session; 0× PEER. SoftLip is symptom surface, not the seed.

## Root cause

1. **`hold_last_ness_pk_scope`** skipped Hold promote invent for the whole `FcResimDeferScope` (includes Hold) → remote aim frozen until wire.
2. Wire arrived **after** jibaku entry → physics already forked.
3. **`ness_pk_defer` TryBegin** blocked rewind for the volatile jibaku window while live advanced under lifted live-cap → mega-span predict.
4. Late burst could not reconstruct jibaku (weapon/cull couple).

Policy “finish jibaku on live, then rewind” fights confirmed-input authority.

## Fix (`PORT && SSB64_NETMENU`)

| Layer | Change |
|-------|--------|
| TryBegin | `DeferResimForNessPKThunder` / `DeferFcStateRecoveryForNessPKThunder` → always allow Begin |
| Live-cap | Remove volatile-scope live-cap lift + epoch deferred-skip; pending GGPO caps at `mismatch-1` again |
| Hold invent | Hold/Start/End invent again (jibaku invent skip later retired — [absorb retire](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md)) |
| Stick GGPO | Portable frame-delta only; jibaku absorb retired ([absorb retire](netplay_jibaku_stick_absorb_retire_portable_ggpo_2026-07-26.md)) |

Offline unchanged. Resim quality (weapon restore / post_cull) remains a follow-up if jibaku still fails under short immediate bursts. Ledger-refresh hole that rewound mid-jibaku stick despite absorb: [`netplay_jibaku_ledger_refresh_stick_resim_launch_2026-07-26.md`](netplay_jibaku_ledger_refresh_stick_resim_launch_2026-07-26.md).

## Acceptance

Matched APK + Linux, Ness ditto mid-Hold aim change into jibaku:

- No perpetual `try_begin_fail stage=ness_pk_defer` / `fc_ness_pk_defer`
- Mid-Hold REPLACE → `resim begin` in the same short window (span ≪ 50)
- Remote Hold STICK_SAMPLE tracks owner aim (or GGPO heals before jibaku entry)
- Prefer matched Hold→jibaku over SoftLip hardening
- Mid-jibaku stick: short GGPO OK (absorb retired)

## Related

- [`netplay_ness_pk_hold_aim_ggpo_defer_2026-07-15.md`](netplay_ness_pk_hold_aim_ggpo_defer_2026-07-15.md) — Hold must rewind (superseded volatile defer)
- [`netplay_ness_pk_defer_ggpo_livecap_deadlock_2026-07-13.md`](netplay_ness_pk_defer_ggpo_livecap_deadlock_2026-07-13.md) — lift was hang workaround; retired with defer
- [`netplay_ness_jibaku_stick_ggpo_storm_eff_load_2026-07-17.md`](netplay_ness_jibaku_stick_ggpo_storm_eff_load_2026-07-17.md) — stick absorb kept
- [`netplay_jibaku_ledger_refresh_stick_resim_launch_2026-07-26.md`](netplay_jibaku_ledger_refresh_stick_resim_launch_2026-07-26.md) — ledger/QueueOrWiden absorb hole after defer retire
- [`netplay_turn_entry_lr_dash_future_sticky_resim_2026-07-26.md`](netplay_turn_entry_lr_dash_future_sticky_resim_2026-07-26.md) — same-day sticky (accepted on prior soak)
