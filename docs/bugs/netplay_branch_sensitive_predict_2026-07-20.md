# Branch-sensitive evaluation (transactional) (2026-07-20)

**Status:** FRAMEWORK v2 — transactional eval + Turn/Dash consumer (`PORT && SSB64_NETMENU`, re-soak)  
**Soaks:**

| Session | Lesson |
|---------|--------|
| `1443903805` TURN_DASH@463 | Status commit on predicted smash (`did_dash`) — v1 gate fixed SetStatus |
| `941659182` TURN_DASH_LR_DASH@450 | v1 deferred Dash but `DashCheckTurn` still wrote `lr_dash` / entry — **prepare side effects are part of the branch** |

**Bucket:** forward prediction at interrupt branch evaluation (not History / provenance / resim source)

## Symptom evolution

| Phase | Failure | Framework response |
|-------|---------|-------------------|
| v1 | Remote `did_dash=1` vs owner `0` | Gate `SetStatus` → both `did_dash=0` |
| v2 need | Same soak class: `lr_dash` 0 vs −1 after defer | Discard **prepare writes** with the deferred status |

Linux@450 (`941659182`): `hold_last_smash_flip` → `DashCheckTurn` armed `lr_dash=-1` / `entry=-1` / `attacks4_buf=0` → `BRANCH_DEFERRED` blocked Dash → **arming state remained**. Android owner kept `lr_dash=0`.

## Architectural unit

```text
Protect status transitions          (v1 — necessary, insufficient)
        →
Protect branch evaluation           (v2 — prepare + decide + commit)
```

A branch is one transaction:

```text
BeginBranchEvaluation (capture preimage)
    ↓
candidate prepare writes   (lr_dash, entry, buffers, …)
    ↓
want status transition?
    ↓
authoritative / local  → keep writes + optional SetStatus
predicted remote       → discard writes + no SetStatus
```

Continuous sim stays outside this API.

## Contract

### Safe vs unsafe

| Kind | Examples | Predicted remote input |
|------|----------|------------------------|
| Continuous sim | vel integrate, soft lip, anim countdown | OK |
| Branch evaluation | Turn/DashCheckTurn+allow, JumpSquat exit, shield drop, ledge option, attack buffer arm | **Transactional** — commit or discard together |

### API (`port/net/sys/netplay_branch_predict.{h,c}`)

```c
SYNetplayBranchInputClass syNetplayBranchClassifyDrivingInput(s32 player);

sb32 syNetplayBranchEvalBegin(GObj *gobj, const char *name,
                              const void *preimage, u32 size);
sb32 syNetplayBranchEvalResolve(GObj *gobj, sb32 wants_branch,
                                SYNetplayBranchRestoreFn restore_fn);

/* Status-only (no prepare blob) */
sb32 syNetplayBranchSensitiveMayCommit(GObj *gobj, const char *name, sb32 wants_branch);

/* First consumer */
sb32 syNetplayBranchTurnDashEvalBegin(GObj *gobj);   /* before DashCheckTurn */
sb32 syNetplayBranchTurnDashEvalResolve(GObj *gobj, sb32 will_dash);
```

Policy (runtime: `syNetplayRollbackSemanticsActive()`):

1. Classify driving History for `fp->player` at `syNetInputGetTick()`.
2. **Local** / authoritative remote → speculative=FALSE; prepare writes stick; Resolve returns `wants_branch`.
3. **Predicted** remote → speculative=TRUE; retain preimage; Resolve **always** restores via `restore_fn` and returns FALSE for status commit.
4. **Unknown** → fail-open (non-speculative), same as inactive rollback.

Turn/Dash preimage fields: `lr_dash`, `attacks4_buffer`, `entry_lr_dash` (port sticky).

### Opt-in pattern (future consumers)

1. List prepare writes performed during evaluation (before SetStatus).
2. Define a snap struct + capture/apply.
3. `EvalBegin` with preimage immediately before those writes.
4. Run vanilla evaluation.
5. `EvalResolve(wants_branch, restore_fn)` then conditional SetStatus.

Do **not** convert every interrupt in one pass. Do **not** grow a denylist of single fields — wrap the evaluation transaction.

Candidates: KneeBend / JumpSquat, shield drop, ledge options, attack buffering, tech decisions.

### Diagnostics

| Tag | When |
|-----|------|
| `BRANCH_PREDICTED_INPUT` | Begin (speculative) and Resolve decision line |
| `BRANCH_DISCARD_SIDE_EFFECTS` | Speculative Resolve restored preimage |
| `BRANCH_DEFERRED` | Speculative + `wants_branch` (status suppressed) |
| `BRANCH_COMMITTED` | Non-speculative status commit allowed |

## Acceptance (re-soak dual-stick)

Around former TURN_DASH / `hold_last_smash_flip` allow windows:

- `BRANCH_DISCARD_SIDE_EFFECTS transition=turn_allow_dash` when remote predicted
- After resolve, peers agree on `lr_dash`, `entry_lr_dash`, `attacks4_buf`, and `did_dash` (not only `did_dash`)
- Local `BRANCH_COMMITTED input=local` still responsive
- Authoritative resim may commit Dash when confirmed smash is real
- No edits to History freeze, provenance, seal packing, or resim source selection

## Related

- [`netplay_resim_input_source_2026-07-20.md`](netplay_resim_input_source_2026-07-20.md)
- [`netplay_history_provenance_2026-07-20.md`](netplay_history_provenance_2026-07-20.md)
- [`netplay_turn_lr_dash_stomp_fc_2026-07-19.md`](netplay_turn_lr_dash_stomp_fc_2026-07-19.md) — entry sticky (still valid; now also inside the transaction preimage)
