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
 * User-visible writable directory for saves and logs.
 *
 * <p>Primary location: {@code &lt;primary external storage&gt;/Documents/BattleShip/}
 * (file managers: Internal storage &gt; Documents &gt; BattleShip). Game archives
 * ({@code BattleShip.o2r}, {@code f3d.o2r}, etc.) stay in {@code externalFilesDir}
 * via {@link AssetExtractor}; only {@code ssb64_save.bin}, {@code ssb64.log}, and
 * other user-data files resolved through {@code ssb64_UserDataDirUtf8} use this tree.
 * Optional {@code debug.env} in this folder is for developer diagnostics only (not player settings).
 */
public final class UserStoragePaths {
    private static final String TAG = "ssb64.storage";

    /** Folder name under the public Documents directory on primary external storage. */
    public static final String USER_DATA_FOLDER = "BattleShip";

    /** Written under externalFilesDir for native JNI_OnLoad before Java calls JNI. */
    private static final String NATIVE_PATH_SENTINEL = ".battleship_user_data_dir";

    private static final String[] MIGRATE_FILES = {
        "ssb64_save.bin",
        "ssb64.log",
    };

    private static volatile String sCachedPathWithSlash;

    private UserStoragePaths() { /* static */ }

    /**
     * Creates the directory and migrates legacy files from {@code externalFilesDir}.
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

        migrateFromLegacyAppDir(ctx, dir);
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

    /**
     * {@code Environment.getExternalStoragePublicDirectory(DIRECTORY_DOCUMENTS)/BattleShip}.
     * Falls back to {@code externalFilesDir/BattleShip} if public Documents is unavailable.
     */
    static File resolveUserDataDir(Context ctx) {
        File docs = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
        if (docs != null) {
            return new File(docs, USER_DATA_FOLDER);
        }
        File ext = ctx.getExternalFilesDir(null);
        if (ext != null) {
            return new File(ext, USER_DATA_FOLDER);
        }
        return null;
    }

    /** One-time migration from the earlier {@code /Android/BattleShip} experiment. */
    private static void migrateFromLegacyAndroidDir(File dstDir) {
        File storage = Environment.getExternalStorageDirectory();
        if (storage == null) {
            return;
        }
        File legacy = new File(new File(storage, "Android"), USER_DATA_FOLDER);
        migrateFiles(legacy, dstDir, "Android/BattleShip");
    }

    private static void migrateFromLegacyAppDir(Context ctx, File dstDir) {
        File legacy = ctx.getExternalFilesDir(null);
        if (legacy == null || !legacy.isDirectory()) {
            return;
        }
        migrateFiles(legacy, dstDir, "externalFilesDir");
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
