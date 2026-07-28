# Multi-stick correction union (N remotes) — 2026-07-27

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Soak:** Android guest ↔ Linux host dual-stick mash (post Fix5 dual-hot window)  
**Bucket:** resim frequency / PEER / absorb coalesce  
**After:** [dual-hot window](netplay_stick_absorb_dual_hot_window_2026-07-27.md), [dual-slot ping-pong](netplay_stick_absorb_dual_slot_pingpong_2026-07-27.md)

## Symptom (Fix5 soak)

Still dense resims under dual-stick mash (~every 6.8t / ~9/s) with fat spans (max 16).  
`PEER_SNAPSHOT_DIVERGE` / `BASELINE_UNIVERSE` at load 425 and 576 (`replay_determinism`, inputs agree, figh diverge). Android logged soft-skip `absorb_coalesce_same_intent` @421 before the fat episode 413→425 — same class as soak `740113729` (predicted Promote without rewind).

## Root cause

1. **Predicted soft-own under absorb coalesce** (`absorb_coalesce_same_intent`) promoted History without rewind while a deferred episode was pending → PEER when invent ≠ owner.
2. **One deferred slot + one Begin** serialized cross-remote REPLACE into alternate-slot episodes (2P ping-pong; worse at N remotes).
3. **`DualStickHot` boolean** and slot-exempt NoteHard were 2P-shaped; absorb length did not scale with remote pressure.

## Fix

### 1. Determinism — no predicted soft-own under coalesce

Removed the `absorb_coalesce_same_intent` Promote-without-rewind branch in `syNetInputStickReplaceNeedsRewind`. Predicted → wire still requires `hash_confirm` or rewind (same contract as soak `740113729`).

### 2. Per-slot pending union → one Begin

Deferred GGPO keeps a **slot bitmask** (`DeferredMismatchSlotMask`). Queue/merge ORs the dirty remote into the mask and unions `[mismatch, target)`.

When `popcount(mask) > 1`, `TryBeginDeferredMismatch` calls `BeginResim(..., correction_player=-1)` so `syNetInputRollbackReconcileResimSpan` reconciles **all** remote humans in the span.

Hash-confirm cancel clears one bit; empty mask clears deferred. Stick-absorb NoteHard exempt only lifts coalesce when a **single** slot is dirty (multi-slot stays global).

### 3. Multi-stick absorb sizing

`syNetInputHotRemoteHumanSlotCount` + `syNetInputMultiStickHotActive` (≥2 remotes hot, or local hot + ≥1 remote / Restrict). Absorb window:

`clamp(phase_lock + 4*(pressure-1), …, 12)` with `pressure = max(hot_remotes, 2)` when MultiStickHot (so 2P local+remote still widens). Sticky refresh cap 16 from arm; deferred target clamped to mismatch+window.

`DualStickHotActive` remains as an alias of `MultiStickHotActive`.

## What not to do

- Re-enable mismatch−1 live-cap during absorb coalesce.
- Bare / absorb-only predicted same-intent micro-skip (PEER).
- Longer dual-hot absorb + soft-own as the primary smoothness tool.
- Assume exactly two remotes in episode policy.

## Re-soak expectations

- No PEER @425-class from soft-own Promote.
- Multi-remote REPLACE → one `EPISODE_EXEC` with `slot=-1` / `slot_mask` covering both remotes when both dirty.
- Episode cadence from global coalesce expiry, not cross-slot Begin ping-pong.
- Same metrics valid for 3–4P stick mash later.
