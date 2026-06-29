# Android Landscape Lock Overridden by SDL FULL_USER (2026-06-29)

**Status:** Fixed (on `agent/android-orientation-touch`, found during on-device review).

## Symptom

With the "lock Android activities to landscape" change applied, the game
(`BattleShipActivity`) still rendered **portrait** on a portrait-held device —
the 3D scene vertically stretched and the landscape touch overlay squished into
a tall window. The boot/extraction screen (`BootActivity`, a plain
`ComponentActivity`) *was* landscape, which masked the bug.

The branch was never run on a portrait device; the author's emulator happened to
be held landscape, so `FULL_USER` (below) resolved to landscape by luck.

## Root Cause

Two layers both *intend* landscape, but a third overrides them at runtime:

1. The manifest already pins both activities to `android:screenOrientation="landscape"`.
2. The branch added `setRequestedOrientation(SCREEN_ORIENTATION_LANDSCAPE)` in
   `BattleShipActivity.onCreate()` (redundant with the manifest).
3. `BattleShipActivity` extends the vendored `SDLActivity`, which **re-asserts**
   orientation at runtime. `SDLActivity.setOrientationBis()` (SDLActivity.java
   ~1000) runs during SDL window creation:

   ```java
   if (!is_portrait_allowed && !is_landscape_allowed) {   // empty SDL_HINT_ORIENTATIONS
       if (resizable) {
           req = ActivityInfo.SCREEN_ORIENTATION_FULL_USER;   // <-- our case
       } else {
           req = (w > h ? SENSOR_LANDSCAPE : SENSOR_PORTRAIT);
       }
   }
   mSingleton.setRequestedOrientation(req);
   ```

   The SSB64 window is **resizable** (for wide-aspect presentation, see the
   `android_ultrawide_letterbox` fix) and the port sets no `SDL_HINT_ORIENTATIONS`,
   so SDL requests `SCREEN_ORIENTATION_FULL_USER`. `FULL_USER` lets the **device's
   physical rotation** win, overriding both the manifest and the `onCreate` call.
   On a portrait device the game then locks portrait.

Confirmed in logcat: `setOrientation() requestedOrientation=13 ... resizable=true hint=`
(`13` = `SCREEN_ORIENTATION_FULL_USER`).

## Fix

`BattleShipActivity` overrides `setRequestedOrientation(int)` to coerce **every**
request (SDL's `FULL_USER` and any system call) to
`SCREEN_ORIENTATION_SENSOR_LANDSCAPE`:

```java
@Override
public void setRequestedOrientation(int requestedOrientation) {
    super.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
}
```

`SENSOR_LANDSCAPE` keeps the window wide (preserving the wide-aspect / letterbox
behavior) while allowing either landscape facing. The redundant `onCreate` call
was removed. Verified on an API-34 emulator booted portrait: game locks to
`ROTATION_90` / `2400x1080`.

Adversarial review (Codex) confirmed the override does not recurse — `SDLActivity`
does not override `setRequestedOrientation`, so `super.setRequestedOrientation`
goes straight to `Activity`, and SDL has only one orientation-request path.

## Related hardening (same review)

The fixed-anchor touch stick sized its left-half capture region with a one-shot
`stickHost.post()`. Because the activity survives `screenSize` config changes
(manifest `configChanges`), that width went stale on live resize (multi-window /
foldable), leaving the fixed anchor centered in a dead or oversized region.
Replaced with an `OnLayoutChangeListener` on the parent that re-derives the
half-width on every layout (guarded against 0-width passes and re-layout loops).

## Audit Hook

A manifest `screenOrientation` does **not** hold for an `SDLActivity` with a
resizable window — SDL re-requests `FULL_USER` at window creation. To lock a
landscape-only SDL game, either set `SDL_HINT_ORIENTATIONS` (e.g.
`"LandscapeLeft LandscapeRight"`) before window creation, or override
`setRequestedOrientation` on the activity. Verify orientation on a portrait-held
device, not just whatever the emulator booted into.
