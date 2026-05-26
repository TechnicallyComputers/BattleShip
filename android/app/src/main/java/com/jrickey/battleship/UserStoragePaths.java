package com.jrickey.battleship;

import android.content.Context;
import android.os.Environment;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/**
 * App-private writable directory for saves, logs, and matchmaking credentials.
 *
 * <p>Primary location: {@code externalFilesDir} ({@code Android/data/<package>/files/}),
 * same tree as {@code BattleShip.o2r}. Optional {@code debug.env} here is for developer
 * diagnostics only (not player settings).
 */
public final class UserStoragePaths {
    private static final String TAG = "ssb64.storage";

    /** Legacy subfolder name used by older builds (migration source only). */
    public static final String LEGACY_USER_DATA_FOLDER = "BattleShip";

    /** Written under externalFilesDir for native JNI_OnLoad before Java calls JNI. */
    public static final String NATIVE_PATH_SENTINEL = ".battleship_user_data_dir";

    /** One-shot debug session kind written before restart ({@code log_only} or {@code env}). */
    public static final String DEBUG_SESSION_SENTINEL = ".battleship_debug_session";

    private static final String[] MIGRATE_FILES = {
        "ssb64_save.bin",
        "ssb64.log",
        "ssb64-debug.log",
        "matchmaking.cred",
        "debug.env",
    };

    private static volatile String sCachedPathWithSlash;

    private UserStoragePaths() { /* static */ }

    /**
     * Ensures the user-data directory exists and migrates legacy files from older locations.
     * Safe to call from {@link BootActivity} before native libraries are loaded.
     */
    public static void prepareUserDataDir(Context ctx) {
        File dir = resolveUserDataDir(ctx);
        if (dir == null) {
            Log.e(TAG, "Could not resolve user-data directory");
            return;
        }

        if (!dir.exists() && !dir.mkdirs()) {
            Log.e(TAG, "Failed to create user-data directory: " + dir);
        } else if (!dir.isDirectory()) {
            Log.e(TAG, "User-data path is not a directory: " + dir);
            return;
        }

        migrateFromLegacyDocumentsDir(dir);
        migrateFromLegacyBattleShipSubdir(ctx, dir);
        migrateFromLegacyAndroidDir(dir);

        String path = dir.getAbsolutePath();
        if (!path.endsWith("/")) {
            path += "/";
        }
        sCachedPathWithSlash = path;
        writeNativePathSentinel(ctx, path);
        Log.i(TAG, "User-data directory prepared: " + path);
    }

    private static void writeNativePathSentinel(Context ctx, String pathWithSlash) {
        File ext = ctx.getExternalFilesDir(null);
        if (ext == null) {
            return;
        }
        File sentinel = new File(ext, NATIVE_PATH_SENTINEL);
        try {
            Files.write(sentinel.toPath(), pathWithSlash.getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.w(TAG, "Could not write native path sentinel: " + e.getMessage());
        }
    }

    /**
     * Publishes the prepared path to native code. Call from {@link BattleShipActivity}
     * after {@code super.onCreate()} so {@code libSDL2.so} is loaded before {@code libmain.so}.
     */
    public static void publishUserDataDirToNative(Context ctx) {
        if (sCachedPathWithSlash == null) {
            prepareUserDataDir(ctx);
        }
        if (sCachedPathWithSlash != null) {
            nativeSetUserDataDir(sCachedPathWithSlash);
        }
    }

    /** {@code externalFilesDir}, falling back to {@code filesDir}. */
    public static File resolveUserDataDir(Context ctx) {
        File ext = ctx.getExternalFilesDir(null);
        if (ext != null) {
            return ext;
        }
        return ctx.getFilesDir();
    }

    /** Path with trailing slash for native callers. */
    public static String getCachedPathWithSlash(Context ctx) {
        if (sCachedPathWithSlash == null) {
            prepareUserDataDir(ctx);
        }
        return sCachedPathWithSlash;
    }

    /** Migration from public Documents/BattleShip (older builds). */
    private static void migrateFromLegacyDocumentsDir(File dstDir) {
        File docs = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
        if (docs == null) {
            return;
        }
        File legacy = new File(docs, LEGACY_USER_DATA_FOLDER);
        migrateFiles(legacy, dstDir, "Documents/BattleShip");
    }

    /** Migration from {@code externalFilesDir/BattleShip/} subfolder layout. */
    private static void migrateFromLegacyBattleShipSubdir(Context ctx, File dstDir) {
        File ext = ctx.getExternalFilesDir(null);
        if (ext == null) {
            return;
        }
        File legacy = new File(ext, LEGACY_USER_DATA_FOLDER);
        if (legacy.getAbsolutePath().equals(dstDir.getAbsolutePath())) {
            return;
        }
        migrateFiles(legacy, dstDir, "externalFilesDir/BattleShip");
    }

    /** One-time migration from the earlier {@code /Android/BattleShip} experiment. */
    private static void migrateFromLegacyAndroidDir(File dstDir) {
        File storage = Environment.getExternalStorageDirectory();
        if (storage == null) {
            return;
        }
        File legacy = new File(new File(storage, "Android"), LEGACY_USER_DATA_FOLDER);
        migrateFiles(legacy, dstDir, "Android/BattleShip");
    }

    private static void migrateFiles(File legacyDir, File dstDir, String label) {
        if (!legacyDir.isDirectory()) {
            return;
        }
        for (String name : MIGRATE_FILES) {
            File src = new File(legacyDir, name);
            File dst = new File(dstDir, name);
            if (!src.isFile() || dst.isFile()) {
                continue;
            }
            try {
                Files.copy(src.toPath(), dst.toPath(), StandardCopyOption.REPLACE_EXISTING);
                if (!src.delete()) {
                    Log.w(TAG, "Migrated " + name + " from " + label
                        + " but could not remove legacy copy at " + src);
                } else {
                    Log.i(TAG, "Migrated " + name + " from " + label + " -> " + dst);
                }
            } catch (IOException e) {
                Log.w(TAG, "Could not migrate " + name + " from " + label + " (" + src + "): "
                    + e.getMessage());
            }
        }
    }

    private static native void nativeSetUserDataDir(String absolutePathWithTrailingSlash);
}
