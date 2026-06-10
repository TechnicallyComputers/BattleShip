# Resim baseline wire load tick vs episode load (netplay)

**Date:** 2026-06-10  
**Scope:** `port/net/sys/netrollback.c`  
**Status:** FIX SHIPPED — soak pending (`INJECT_TICK=480`)

## Symptoms

Forced-resim soak @480 (Android host / Windows guest): peers agree on baseline wire digest at `load_tick=442` (`figh=0xD2905E12`), seal rows exchange completes, but session never logs `resim baseline digest matched` or `resim replay gate open`. Both stall at `sim=444` with baseline retransmit + `strict stall egress limit` until quit.

Soak @240 (same build) still passes — `resim begin load=223` matches `RESIM_BASELINE_SEND load_tick=223`.

## Root cause

`syNetRollbackArmResimBaselineAfterLoad()` clamps the wire baseline tick via `syNetRollbackClampLoadTickForPeerSend()` when local physical load is behind remote `hr` (soak1: physical `load=441`, wire `load=442`).

`syNetRollbackEpisodeSyncToLegacy()` then overwrites `sSYNetRollbackResimLoadTick` from episode FSM `load_tick` (441). `syNetRollbackTryOpenResimBaselineGateFromPeerDigest()` requires `peer_load_tick == sSYNetRollbackResimLoadTick`, so inbound packets at 442 are silently ignored — no mismatch log, no gate open.

## Fix

After every episode→legacy sync while awaiting peer baseline, align `sSYNetRollbackResimLoadTick` to `sSYNetRollbackPeerBaselineLoadTick` (the clamped wire tick). Log `BASELINE_WIRE_LOAD_ALIGN episode_load=… wire_load=…` when they diverge.

## Test plan

1. Re-run `INJECT_TICK=480` FORCE_MISMATCH soak — expect `BASELINE_WIRE_LOAD_ALIGN`, `resim baseline digest matched load_tick=442`, `resim replay gate open`, `resim complete`.
2. Regression: `INJECT_TICK=240` — no behavior change when physical load equals wire load.
