package com.jrickey.battleship;

import android.content.Intent;
import android.os.Bundle;

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
    protected String[] getLibraries() {
        // Order matters: SDL2 first (so its symbols are visible when libmain
        // resolves DT_NEEDED), then libmain (the one with SDL_main).
        return new String[] {
            "SDL2",
            "main",
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // BootActivity already prepared storage; ensure cache/sentinel before native load.
        UserStoragePaths.prepareUserDataDir(getApplicationContext());
        super.onCreate(savedInstanceState);
        DebugSessionHelper.attach(this);
        // After libmain.so loads — must not use SDL from JNI_OnLoad (see android_user_storage.cpp).
        UserStoragePaths.publishUserDataDirToNative(getApplicationContext());
        // Lay the touch overlay on top of SDL's GLES surface. Has to come
        // after super.onCreate so SDLActivity has already installed its
        // surface as the root content view.
        TouchOverlay.install(this);
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
