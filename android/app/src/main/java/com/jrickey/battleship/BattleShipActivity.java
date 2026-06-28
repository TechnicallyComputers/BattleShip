package com.jrickey.battleship;

import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import org.libsdl.app.SDLActivity;

/**
 * BattleShipActivity — entry point for the SSB64 PC port on Android.
 *
 * Subclasses SDLActivity (vendored at android/app/src/main/java/org/libsdl/app/
 * from SDL release-2.32.10). SDLActivity:
 *   1. Calls getLibraries() to know which .so files to System.loadLibrary
 *      (in order). The last one is treated as "the main library" and SDL
 *      dlopens it + dlsym's the entry function.
 *   2. Looks up getMainSharedObject() — defaults to libmain.so in our
 *      nativeLibraryDir, which is exactly what we ship.
 *   3. Looks up getMainFunction() — defaults to "SDL_main", which our
 *      port.cpp now exports thanks to SDL_main.h's `#define main SDL_main`
 *      on Android.
 *   4. Invokes nativeRunMain(library, function, argv) on a dedicated thread.
 *
 * That's it. All the GLES surface, audio, controller, IME, and lifecycle
 * plumbing is handled by SDLActivity itself.
 */
public class BattleShipActivity extends SDLActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        UserStoragePaths.prepareUserDataDir(getApplicationContext());
        super.onCreate(savedInstanceState);
        UserStoragePaths.publishUserDataDirToNative(getApplicationContext());
        DebugSessionHelper.attach(this);
        TouchOverlay.install(this);
        NetworkMonitor.bindContext(this);
        // SDLActivity.onCreate() calls setWindowStyle(false), which forces the
        // system bars visible (FLAG_FORCE_NOT_FULLSCREEN) and clobbers the
        // manifest's .Fullscreen theme. SDL only re-enters immersive mode if the
        // SDL window itself goes fullscreen, which never happens on Android. So we
        // drive immersive sticky directly, decoupled from SDL's window state — no
        // surface mode change, so it can't retrigger the BLAST surface-resize abort
        // (see docs/android_orientation_landscape_lock.md).
        enableImmersiveMode();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // The OS restores the system bars whenever we lose and regain focus
        // (returning from recents, dismissing a dialog, swipe-revealed bars
        // timing out). Re-assert immersive sticky each time focus comes back.
        if (hasFocus) {
            enableImmersiveMode();
        }
    }

    /**
     * Hide the status and navigation bars in immersive-sticky mode: bars stay
     * hidden during play and reappear only as a transient overlay when the user
     * swipes from the edge, then auto-hide again. Uses the modern
     * WindowInsetsController on API 30+ and falls back to the legacy
     * setSystemUiVisibility flags (same set SDLActivity uses) below that.
     */
    private void enableImmersiveMode() {
        Window window = getWindow();
        if (window == null) {
            return;
        }

        if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            int flags = View.SYSTEM_UI_FLAG_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
            window.getDecorView().setSystemUiVisibility(flags);
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "main",
        };
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        DebugSessionHelper helper = DebugSessionHelper.getInstance();
        if (helper != null && helper.onActivityResult(requestCode, resultCode, data)) {
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    protected void onDestroy() {
        // Unregister the InputManager listener and stop the menu poller —
        // both hold strong references into this Activity's view hierarchy
        // and would leak it (and keep ticking) across recreations.
        TouchOverlay.uninstall();
        super.onDestroy();
    }
}
