# netplay: FTPlayerInput latch not snapshotted — tap/hold stick counters fork across resims (figh-only FC diverge)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending).

## Symptom

Soak `1586836171` (Fox Firefox + Peach's Castle bumper): `FRAME_COMMIT_STATE_DIVERGE` at validation 600, `diverged=figh` only, **inputs MATCH** — `world`/`item`/`rng`/`eff` all identical cross-peer. Both peers report it symmetrically; the input-agree reanchor (`mismatch=481 resolved_load=480`) recovered and the match continued to tick 813 with no resync storm. No load-hash drift, no synctest failure, and the bumper item hash agreed throughout (the prior bumper item work held).

## Evidence

At the reanchor seed dump (`fighter_field_diff tag=frame_commit_seed tick=481`, ~245 field lines per peer), a cross-peer diff of player 1's **live** values shows exactly two differing fields:

- `fold_tap_stick_x`: host `0x4C` (76) vs guest `0x52` (82)
- `fold_hold_stick_x`: host `0x4C` vs guest `0x52`

Everything else — position, velocity, joints, status vars, collision — is bit-identical. Both counters are folded into `fhash_light` (`syNetSyncFoldFighterSlotFullContribution`), which is exactly why only `figh` diverged. The 6-tick gap matches the initiator/follower frontier drift at the FORCE_MISMATCH@520 resim (both peers `resim=2`; epoch 1 load 519 → target 522 landed identical `rollback_post` fighter hashes at 520, `0x3CB7E909`, so the fork occurred during the counters' post-resim forward re-derivation).

## Root cause

`ftMainProcessInput` (decomp `ftmain.c`) re-derives the tap/hold stick counters every tick from the fighter's **latched previous input**:

```1485:1501:decomp/src/ft/ftmain.c
        if (pl->stick_range.x >= 20)
        {
            if (pl->stick_prev.x >= 20)
            {
                this_fp->tap_stick_x++, this_fp->hold_stick_x++;
            }
            else this_fp->tap_stick_x = this_fp->hold_stick_x = 1;
        }
        else if (pl->stick_range.x <= -20)
        {
            if (pl->stick_prev.x <= -20)
            {
                this_fp->tap_stick_x++, this_fp->hold_stick_x++;
            }
            else this_fp->tap_stick_x = this_fp->hold_stick_x = 1;
        }
        else this_fp->tap_stick_x = this_fp->hold_stick_x = FTINPUT_STICKBUFFER_TICS_MAX;
```

The snapshot captured and restored the **counters** (`tap_stick_x/y`, `hold_stick_x/y`) but nothing captured `fp->input.pl` (`FTPlayerInput`: `button_hold`, `button_tap`, `button_release`, `stick_range`, `stick_prev` — zero references under `port/net`). After a rollback load, `pl->stick_prev` still holds each peer's **pre-load live frontier** stick value, not the load tick's. The initiator and follower roll back from different frontiers (here 6 ticks apart), so at the first replayed tick one peer saw a stale threshold crossing (reset-to-1) where the other saw a continuous hold (increment) — permanently forking the folded counters. The `button_hold` latch has the same hazard for `button_tap`/`button_release` edge derivation.

The user attributed the drift to the bumper mechanic changes; the bumper is exonerated — the fork merely *surfaced* on the bumper-hit → Firefox resim because that is what triggers rollbacks in this soak.

## Fix

`port/net/sys/netrollbacksnapshot.c`, fighter blob:

- Added `pl_button_hold/tap/release`, `pl_stick_range_x/y`, `pl_stick_prev_x/y` to `SYNetRbSnapFighterBlob`.
- Capture from `fp->input.pl` beside the existing tap/hold counter capture.
- Restore in `syNetRbSnapApplyFighterBlobToGObj`'s netmenu tail beside the counter restore, so the first `ftMainProcessInput` after any load (resim replay or verify) sees the load tick's latch instead of the frontier's.
- Added `pl_button_hold` / `pl_stick_prev_x/y` lines to `fighter_field_diff` (not folded, but they drive the folded counters — future drift lands next to the counter diff).

No hash-universe change (the latch itself is not folded; the counters already were).

## Verification

- Lints clean; netmenu + offline builds pass.
- Soak repro: FORCE_MISMATCH resim during a bumper-hit Firefox must now hold `figh` agreement through the following frame-commit validations.

## Related

- `netplay_castle_bumper_resim_baseline_item_load_fidelity_2026-07-03.md` — same soak family; its deeper-load/timeout recovery is the robustness net under this class of bug.
