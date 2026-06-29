# Training Mode Extra-Stage Scroll

**Status:** Fixed (issue #243).

## Symptom

In Training Mode's stage-select screen, pressing R at the right edge of
the original stage page did not scroll to the port-added extra-stage page.
The same page scroll worked in VS mode.

## Root Cause

The port page-scroll code uses `mnMapsCheckLocked` for both icon visibility
and cursor/page navigation. A Training Mode-specific guard treated Final
Destination, Metal Cavern, and Battlefield as locked because older training
wallpaper paths had not been wired up for them yet.

That made page 1 appear to contain no selectable slots while
`sMNMapsIsTrainingMode` was true. `mnMapsTryPageJump(TRUE)` reached the
target page, scanned the landing slots, found every extra stage "locked",
and returned `FALSE`, so the input fell back to in-page cursor movement.

## Fix

Remove the Training Mode-only lockout for the three port-introduced stages.
The stage-select page table, preview, nameplate, and training wallpaper paths
now already handle those gkinds, so Training Mode can share the same page
navigation as VS mode.

`nMNMapsEmptyGKind` remains locked so blank page slots are still skipped.

## Audit Hook

Any future stage-select filtering must distinguish "should not render an
icon" from "cannot be used as a page-scroll landing slot." Reusing
`mnMapsCheckLocked` for both is convenient but turns visual gating into
navigation gating.
