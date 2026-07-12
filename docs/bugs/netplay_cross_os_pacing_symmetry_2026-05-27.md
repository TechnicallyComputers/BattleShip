# Netplay cross-OS pacing symmetry (Windows / Linux / macOS / Android)

**Date:** 2026-05-27  
**Status:** Fix in progress — portable clocks, ingress/bootstrap, decouple parity  
**Companion:** [`netplay_cross_isa_determinism_2026-05-27.md`](netplay_cross_isa_determinism_2026-05-27.md) (ARM↔x86 float)

## Problem

Linux↔Linux netplay is healthy. Cross-play issues on **Windows** are often **cadence asymmetry**, not different UDP semantics:

- Host runs **more display frames** than sim steps (120–240 Hz monitor vs ~60 Hz contract sim).
- **`hr==0`** while the other peer already simulates (bootstrap / exec-ready ordering).
- **Strict-R** storms at ticks 1–4 when wire frontier lags sim.
- **ICE/LAN path** differences (Windows adapter enumeration, /16 prefix clamp).

Cross-ISA float drift is handled separately by `syNetplayQuantizeF32` on all peers during VS.

## Soak matrix

| Host | Guest | Priority | Notes |
|------|-------|----------|-------|
| Linux x86_64 | Linux x86_64 | Control | Regression |
| **Windows x86_64 (MinGW)** | **Linux x86_64** | **P0** | Same ISA, OS pacing only |
| Linux x86_64 | Android arm64 | P0 | Cross-ISA + pacing |
| **Windows x86_64** | **Android arm64** | **P0** | Production-like |
| macOS arm64 | Linux x86_64 | P1 | |
| Windows arm64 (WoA) | Linux x86_64 | P1 | MSVC path |
| Windows x86_64 | Windows x86_64 | P1 | |

## Cross-OS soak env bundle

Use [`scripts/netplay-cross-os-soak.env.example`](../../scripts/netplay-cross-os-soak.env.example) on **both** peers (desktop `debug.env` or Android import).

Includes cross-ISA quantize vars from [`scripts/netplay-cross-isa-soak.env.example`](../../scripts/netplay-cross-isa-soak.env.example) plus pacing diagnostics.

## Analysis SOP

1. Align logs on same `tick` and `commit_gen`.
2. During ticks 0–20: `ahead` should not stay negative on one peer only; `hr>0` on both before sim 3+.
3. Flag `path=R` / `pct_R` in `FRAME_COMMIT_DIAG` during early fight.
4. Session banner: `cross_isa_session platform=windows|linux|darwin|android`.
5. Optional: `cross_os_pacing` lines (`decouple_skips`, `push`, `hr`, `ahead`).

## Fixes shipped (2026-05-27)

| Change | Purpose |
|--------|---------|
| `syNetPeerOsMonotonicMs()` | QPC (Windows) / `CLOCK_MONOTONIC` (POSIX) for automatch deadlines |
| `mnVSNetAutomatchAMNowMs` → monotonic helper | Same deadline semantics Windows vs Linux |
| `netphase` calibration → monotonic ms | Avoid wall-clock skew in optional calibration |
| Session banner `platform=windows` | Clear log triage |
| `syNetPeerLogCrossOsPacingDiag` | Env `SSB64_NETPLAY_CROSS_OS_PACING_DIAG=1` |
| MSVC `/fp:precise` + `#pragma fp_contract(off)` on sim TUs | Parity with Clang `-ffp-contract=off` |
| Intro wait + wire lead buffer | Already in cross-ISA work; verify cross-OS |

## LAN / ICE (Windows)

- [`mm_lan_detect.c`](../../port/net/matchmaking/mm_lan_detect.c): `GetAdaptersAddresses`; **`mmLanClampPrefixForPeerReach`** uses **/16** on `192.168.x` (lines ~165–172) while Linux `getifaddrs` may use /24 — can disagree on `peer_lan` for multi-subnet LANs. Soak: log `peer_lan`, nominated ICE path, `INPUT drop` counts; compare host vs guest on same physical LAN.
- MinGW Windows netplay bundles **`ssl/cacert.pem`** for HTTPS automatch ([`package-mingw-windows.sh`](../../scripts/package-mingw-windows.sh)). Checklist: `CURL_CA_BUNDLE` or install-dir `ssl/cacert.pem` present; automatch queue reaches `ICE_CONNECT` without TLS errors.
- ICE relay-on-LAN filter: same code path as Linux ([`netplay_automatch_startup_pacing_2026-05-26.md`](netplay_automatch_startup_pacing_2026-05-26.md)). Exit: host-host nomination on LAN; no guest `drop=716`-class relay-on-LAN failure.

## Display decouple

Default **`SSB64_NETPLAY_DECOUPLE_DISPLAY_SIM=1`**: sim at contract VI Hz, display may be faster. **Do not disable** for cross-OS soak unless debugging display-only issues — disabling ties sim to monitor Hz and worsens cross-OS asymmetry.

Pre-exec: **`SSB64_NETPLAY_HOSTFRAME_GATE_PUMP=1`** forces ingress pump on high-Hz host frames while `execution_ready=0`.

**Windows sleep (2026-07-11):** [`netplay_windows_timer_sleep_pacing_2026-07-11.md`](netplay_windows_timer_sleep_pacing_2026-07-11.md) — `syNetPeerOsSleepMicros` uses high-res waitable timer (Fast3D path); wall-slot finish no longer sleeps a full `gran_ms` when already late on a skip frame.

## Soak results

| Pairing | Build | Result | Notes |
|---------|-------|--------|-------|
| Linux ↔ Linux | | Pass (user baseline) | Re-verify with quantize on |
| Windows x86 ↔ Linux x86 | MinGW netplay | **Manual required** | P0 |
| Linux ↔ Android | | **Manual required** | P0 |
| Windows x86 ↔ Android | | **Manual required** | P0 |

**Build verification (2026-05-27):** `cmake --build build --target ssb64 -j 4` with `SSB64_NETMENU=ON`.

## Deferred

- Full CSI field registry
- Re-authority skew pacing (`SSB64_NETPLAY_SKEW_*`)
- Fox reflector snapshot until intro cross-OS green
