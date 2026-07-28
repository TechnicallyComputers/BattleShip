# Netplay — FC onset older than ring / RING_CLAMP abandon is a hard state fork

**Date:** 2026-07-27
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)
**Soak:** Android guest ↔ Linux host, Dream Land — `validation=841` / `snap_tick=840`;
follow-up seed `4086048930` `validation=481`
**Follow-on to:** [FC input-agree onset ring clamp](netplay_fc_input_agree_onset_ring_clamp_2026-07-19.md)

## Symptom

```
inp_local=0x6D705EC9 inp_peer=0x6D705EC9   ← inputs agree
local figh=0x922E4830  peer figh=0xC3334AB8 ← state already forked
```

`class=replay_determinism` / eventual `PEER_SNAPSHOT_DIVERGE`. World/RNG match; fighter+anim+camera do not.

Onset scan found agree from **519**, then:

```
FRAME_COMMIT_INPUT_AGREE_ONSET_RING_CLAMP … onset=519 → clamped_load=830
```

Resim only covered ~830–842. Load baselines already disagreed (`agree_through_load=1`). Linux ran **167** `resim begin`; Android **2**. Same terminal diverge.

Follow-up after older-than-ring harden (seed `4086048930`): permanent live fork from **454**, FC@**481** inp MATCH, onset **in-ring** (`onset=454 min_load=354`) but Resolve miss → RING_CLAMP to **480/477** (asymmetric) → doomed short resim → PEER@477. `ONSET_UNRECOVERABLE` did not fire under the older-than-ring-only rule.

## Root cause

1. **RING_CLAMP claimed input-agree recovery** when the true onset was not the load used — either outside the ring, or in-ring but Resolve failed so load jumped to the frontier. Short resim cannot rewrite state forked at the abandoned onset.
2. **`inp_*` MATCH does not certify fighter stick counters.** Frame-commit input digest hashes authority sticks (`buttons`/`sx`/`sy`), not `tap_stick_*` / `hold_stick_*`.
3. **±1 analog chatter** on local enqueue can fork physics across Android/Linux.

## Fix

`PORT && SSB64_NETMENU`:

1. **`FRAME_COMMIT_INPUT_AGREE_ONSET_UNRECOVERABLE`** — fail closed via `PEER_SNAPSHOT_DIVERGE` (no short FC resim) when:
   - `onset_load < min_load` (**fork older than ring**), or
   - RING_CLAMP yields `clamped_load > onset_load` (**ring clamp abandoned onset**).
   Still logs `ONSET_RING_CLAMP` first in the abandon case for soak diagnosis.
2. **`FRAME_COMMIT_INPUT_APPLY_AUDIT`** — on figh diverge with matching input digests, log published history sticks at `snap_tick` alongside local/peer `tap`/`hold`.
3. **`NET_STICK_CHATTER_QUANT`** (default **1**) — snap ±1 same-intent local enqueue noise to the prior gameplay row. Env: `SSB64_NETPLAY_NET_STICK_CHATTER_QUANT` (`0` disables, max 3).

RING_CLAMP that does **not** move past `onset_load` may still arm recovery.

## Follow-on (2026-07-28)

1. Soak `190673804`: light resim **skipped FC grids 1200/1320** → first compare past ring
   depth. Fixed in [fc late mint resim grid skip](netplay_fc_late_mint_resim_grid_skip_2026-07-28.md).
2. Soak `2028838966` / seed `3538623210`: onset **in-ring** but Resolve raised min_load to
   `EpisodeResolvedThrough-1` → false miss → RING_CLAMP abandon. Fixed in
   [fc onset Resolve episode floor](netplay_fc_onset_resolve_episode_floor_2026-07-28.md).

Fail-closed here remains correct when the onset genuinely predates the ring or no load-safe
slot exists at/before onset.

## Acceptance

- [ ] Re-soak deep / abandoned-onset FC: `ONSET_UNRECOVERABLE` (`older than ring` or `ring clamp abandoned onset`) + fail-closed **without** doomed short state resim / asymmetric clamp loads.
- [ ] Successful in-ring Resolve at onset still arms normal FC recovery.
- [ ] `INPUT_APPLY_AUDIT` when inp MATCH ∧ figh diverge; chatter logs on ±1 noise when present.
