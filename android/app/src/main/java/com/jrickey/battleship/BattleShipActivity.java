package com.jrickey.battleship;

import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.util.concurrent.atomic.AtomicBoolean;

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
    private static final String TAG = "ssb64.debug";

    /** AMS teardown cushion before BootActivity (debug restart). */
    private static final long BOOT_LAUNCH_DELAY_MS = 150L;

    /** Fire-and-forget join telemetry only — never gates Boot. */
    private static final long BACKGROUND_SDL_JOIN_MS = 2_000L;

    private static final Handler sMainHandler = new Handler(Looper.getMainLooper());

    /** Starts BootActivity after cooperative restart teardown. */
    private static volatile Runnable sPendingBootLaunch;

    /** Set when cooperative SDL_main returns (JNI or SDLMain hook). */
    private static final AtomicBoolean sCooperativeMainExited = new AtomicBoolean(false);

    private static native void nativeBeginInProcessRestart();

    public static void onNativeMainReturnedForDebugRestart() {
        sCooperativeMainExited.set(true);
        Log.i(TAG, "cooperative shutdown: SDL_main returned (JNI)");
    }

    public static void onSdlMainNativeRunMainReturned() {
        if (sCooperativeMainExited.compareAndSet(false, true)) {
            Log.i(TAG, "cooperative shutdown: SDL_main returned (SDLMain)");
        }
    }

    public static void cooperativeShutdownForRestart(Runnable onComplete) {
        if (mSingleton == null) {
            Log.e(TAG, "cooperative shutdown: no activity");
            return;
        }
        sCooperativeMainExited.set(false);
        sPendingBootLaunch = onComplete;
        mSingleton.runOnUiThread(() -> {
            if (mBrokenLibraries) {
                launchBootAfterDestroy(onComplete);
                return;
            }
            nativeBeginInProcessRestart();
            if (!mSingleton.isFinishing()) {
                Log.i(TAG, "cooperative shutdown: finishing activity");
                mSingleton.finish();
            } else {
                Log.w(TAG, "cooperative shutdown: activity already finishing");
            }
        });
    }

    private static void launchBootAfterDestroy(Runnable launchBoot) {
        if (launchBoot == null) {
            return;
        }
        Log.i(TAG, "debug restart: launching BootActivity after destroy");
        launchBoot.run();
    }

    private static void postBootLaunch(final Runnable launchBoot, long delayMs) {
        if (launchBoot == null) {
            return;
        }
        if (delayMs <= 0) {
            sMainHandler.post(() -> launchBootAfterDestroy(launchBoot));
        } else {
            sMainHandler.postDelayed(() -> launchBootAfterDestroy(launchBoot), delayMs);
        }
    }

    /** Best-effort join for logs only; does not block relaunch. */
    private static void logBackgroundJoinAsync(final Thread sdlThread) {
        if (sdlThread == null) {
            return;
        }
        new Thread(() -> {
            if (!sdlThread.isAlive()) {
                Log.i(TAG, "background join: SDLThread already stopped");
                return;
            }
            Log.i(TAG, "background join: waiting up to " + BACKGROUND_SDL_JOIN_MS + "ms (telemetry)");
            try {
                sdlThread.join(BACKGROUND_SDL_JOIN_MS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                Log.e(TAG, "background join: interrupted: " + e.getMessage());
            }
            if (sdlThread.isAlive()) {
                Log.w(TAG, "background join: SDLThread still alive after "
                        + BACKGROUND_SDL_JOIN_MS + "ms");
            } else {
                Log.i(TAG, "background join: SDLThread joined");
            }
        }, "ssb64-restart-join-log").start();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        sPendingBootLaunch = null;
        sCooperativeMainExited.set(false);
        UserStoragePaths.prepareUserDataDir(getApplicationContext());
        super.onCreate(savedInstanceState);
        DebugSessionHelper.attach(this);
        UserStoragePaths.publishUserDataDirToNative(getApplicationContext());
        TouchOverlay.install(this);
    }

    @Override
    protected void onDestroy() {
        final Runnable launchBoot = sPendingBootLaunch;
        sPendingBootLaunch = null;
        final boolean restartAttempt = launchBoot != null;
        final boolean cooperativeExited = sCooperativeMainExited.get();

        Log.i(TAG, "onDestroy begin (restartAttempt=" + restartAttempt
                + " cooperativeExited=" + cooperativeExited + ")");

        if (restartAttempt) {
            final Thread sdlThread = mSDLThread;
            mSDLThread = null;
            super.onDestroy();
            logBackgroundJoinAsync(sdlThread);
            Log.i(TAG, "debug restart: posting Boot in " + BOOT_LAUNCH_DELAY_MS + "ms"
                    + " (not gated on join)");
            postBootLaunch(launchBoot, BOOT_LAUNCH_DELAY_MS);
        } else {
            super.onDestroy();
        }

        Log.i(TAG, "onDestroy end");
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
}
