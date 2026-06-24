# VS CSS P3/P4 Reactivation Crash After Match (2026-06-24) - FIXED

**Issue:** #244, "Game crashes on the Character Select Screen after a match if a Player 3 or Player 4 slot is selected."

## Symptom

On Android, with all four virtual controller cursors visible, play a VS free-for-all with two active players and return from the results screen to the Character Select Screen. Player 3 and Player 4 re-enter as connected `NA` slots. Moving a cursor to the 3P or 4P kind button and activating the slot crashes the app to the Android home screen.

## Root Cause

After a match, VS CSS re-enters through `mnPlayersVSInitPlayer` using the persisted `gSCManagerTransferBattleState`. Slots that did not participate in the prior two-player match have:

- `pkind == nFTPlayerKindNot`
- `fkind == nFTKindNull`
- `is_selected == FALSE`
- no CSS fighter GObj allocated yet

Because the Android touch overlay exposes four virtual controllers, `mnPlayersVSInitSlot` still creates cursors for P3/P4. Activating an `NA` slot cycles it back to `HMN` and calls `mnPlayersVSUpdateFighter`.

`mnPlayersVSUpdateFighter` only skipped empty slots inside `if (fighter_gobj != NULL)`. For an unused P3/P4 slot, `fighter_gobj` is `NULL`, so the skip never ran and the function fell through into:

```c
costume = mnPlayersVSGetFreeCostume(sMNPlayersVSSlots[player].fkind, player);
```

with `fkind == nFTKindNull`. That lets the null fighter kind index fighter costume/setup tables, producing the crash.

## Fix

Under `PORT`, `mnPlayersVSUpdateFighter` now treats `pkind == nFTPlayerKindNot` and `fkind == nFTKindNull && is_selected == FALSE` as skip conditions even when no fighter GObj exists yet. If a stale fighter GObj does exist, it is hidden as before; if none exists, the function simply avoids the invalid costume/fighter update.

The non-`PORT` branch keeps the original decomp control flow intact.

## Why This Is Android-Visible

The stale slot state is not Android-specific, but Android commonly shows all four virtual controller cursors. That makes P3/P4 connected while their persisted battle-state slots are still `NA`, exposing the `NULL` fighter creation path after returning from a two-player match.

## File

- `decomp/src/mn/mnplayers/mnplayersvs.c` - `mnPlayersVSUpdateFighter`
