# Netplay Samus screw FallSpecial pass diagnostics — 2026-06-09

**Status:** Fix shipped — see [`netplay_fallspecial_pass_allow_stomp_2026-06-09.md`](netplay_fallspecial_pass_allow_stomp_2026-06-09.md)

## Symptom

Samus aerial screw (Up+B) inconsistent soft-platform pass-through after helpless fall in netplay soak. Prior log read showed Samus `228 → 58 → 59` cycles with `StatusVars` landing/fallspecial stomps, but no pass/contact fields.

## Diagnostics

Enable on both peers:

```bash
SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG=1
SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG_VERBOSE=0   # 1 = every proc_pass in scope
SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG_TICK_MIN=2180
SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG_TICK_MAX=2400
SSB64_NETPLAY_STATUSVARS_WITNESS=1
SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG=1
SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=1
```

Look for `SSB64 FallSpecialPassDiag:` lines:

| event | Meaning |
|-------|---------|
| `fallspecial_enter` | `ftCommonFallSpecialSetStatus` (`allow_pass` should be 1) |
| `proc_pass` | Soft-platform pass predicate; `block=0` means pass allowed |
| `pass_cliff` | Floor contact during screw/`FallSpecial` map handling |

`deny=` when `block=1`: `allow_pass0`, `nopass_floor`, `stick_high`.

## Implementation

- `port/net/sys/netplay_fallspecial_pass_diag.c`
- Hooks: `ftCommonFallSpecialProcPass/SetStatus/ProcMap`, `ftSamusSpecialHiProcPass/ProcMap`
- Gated: `#if defined(PORT) && defined(SSB64_NETMENU)`

## Soak pass criteria

Repro pass attempt with stick held down on Dream Land top platform:

- `fallspecial_enter` shows `allow_pass=1` after screw anim ends.
- Failed pass at soft platform: `proc_pass` with `pass_floor=1`, `stick_y<-44`, and `block=1` — note `deny=` and compare with witness stomps on same tick.
- Successful pass: `proc_pass` with `block=0` on the contact tick.
