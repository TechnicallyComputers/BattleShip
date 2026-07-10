package com.jrickey.battleship;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * Debug session arming and SAF import/export for {@code ssb64-debug.log} / {@code debug.env}.
 *
 * <p>On Android, debug mode is armed by writing {@link UserStoragePaths#DEBUG_SESSION_SENTINEL};
 * the next launch from the launcher consumes it and opens {@code ssb64-debug.log}. There is no
 * in-process relaunch.
 */
public final class DebugSessionHelper {
    private static final String TAG = "ssb64.debug";

    private static final String TOAST_MANUAL_RELAUNCH =
        "Debug session armed. Close the app, then open it again from the launcher.";

    private static final int REQ_IMPORT_ENV = 9101;
    private static final int REQ_EXPORT_DEBUG_LOG = 9102;

    private static volatile DebugSessionHelper sInstance;

    private final Activity mActivity;

    public DebugSessionHelper(Activity activity) {
        mActivity = activity;
    }

    public static void attach(Activity activity) {
        sInstance = new DebugSessionHelper(activity);
    }

    public static DebugSessionHelper getInstance() {
        return sInstance;
    }

    public static void restartInDebugMode(Activity activity) {
        DebugSessionHelper helper = sInstance;
        if (helper == null) {
            Log.e(TAG, "DebugSessionHelper not attached");
            return;
        }
        activity.runOnUiThread(helper::armLogOnlySession);
    }

    public static void restartWithDebugEnv(Activity activity) {
        DebugSessionHelper helper = sInstance;
        if (helper == null) {
            Log.e(TAG, "DebugSessionHelper not attached");
            return;
        }
        activity.runOnUiThread(helper::launchImportEnv);
    }

    public static void exportDebugLog(Activity activity) {
        DebugSessionHelper helper = sInstance;
        if (helper == null) {
            Log.e(TAG, "DebugSessionHelper not attached");
            return;
        }
        activity.runOnUiThread(helper::launchExportDebugLog);
    }

    /**
     * @return true if the request was handled
     */
    public boolean onActivityResult(int requestCode, int resultCode, Intent data) {
        if (resultCode != Activity.RESULT_OK) {
            return requestCode == REQ_IMPORT_ENV || requestCode == REQ_EXPORT_DEBUG_LOG;
        }
        if (requestCode == REQ_IMPORT_ENV) {
            onEnvFilePicked(data != null ? data.getData() : null);
            return true;
        }
        if (requestCode == REQ_EXPORT_DEBUG_LOG) {
            onDebugLogExportDestination(data != null ? data.getData() : null);
            return true;
        }
        return false;
    }

    private void launchImportEnv() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        mActivity.startActivityForResult(intent, REQ_IMPORT_ENV);
    }

    private void launchExportDebugLog() {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/plain");
        intent.putExtra(Intent.EXTRA_TITLE, "ssb64-debug.log");
        mActivity.startActivityForResult(intent, REQ_EXPORT_DEBUG_LOG);
    }

    private void armLogOnlySession() {
        if (!writeDebugSession("log_only")) {
            toast("Could not arm debug session");
            return;
        }
        Log.i(TAG, "debug session armed (log_only); manual relaunch required");
        toast(TOAST_MANUAL_RELAUNCH);
    }

    private void onEnvFilePicked(Uri uri) {
        if (uri == null) {
            return;
        }
        File userDir = UserStoragePaths.resolveUserDataDir(mActivity);
        if (userDir == null) {
            toast("User data directory unavailable");
            return;
        }
        File dest = new File(userDir, "debug.env");
        try {
            copyUriToFile(mActivity.getContentResolver(), uri, dest);
        } catch (IOException e) {
            Log.e(TAG, "Import debug.env failed: " + e.getMessage());
            toast("Import failed: " + e.getMessage());
            return;
        }
        if (!writeDebugSession("env")) {
            toast("Saved debug.env but could not arm debug session");
            return;
        }
        Log.i(TAG, "debug session armed (env); manual relaunch required");
        toast(TOAST_MANUAL_RELAUNCH);
    }

    private void onDebugLogExportDestination(Uri destUri) {
        if (destUri == null) {
            return;
        }
        File userDir = UserStoragePaths.resolveUserDataDir(mActivity);
        if (userDir == null) {
            toast("User data directory unavailable");
            return;
        }
        File src = new File(userDir, "ssb64-debug.log");
        if (!src.isFile()) {
            toast("No ssb64-debug.log yet — arm Debug Mode, relaunch from launcher, then export");
            return;
        }
        try {
            copyFileToUri(mActivity.getContentResolver(), src, destUri);
            toast("Exported ssb64-debug.log");
        } catch (IOException e) {
            Log.e(TAG, "Export debug log failed: " + e.getMessage());
            toast("Export failed: " + e.getMessage());
        }
    }

    private boolean writeDebugSession(String mode) {
        File userDir = UserStoragePaths.resolveUserDataDir(mActivity);
        if (userDir == null) {
            return false;
        }
        File sentinel = new File(userDir, UserStoragePaths.DEBUG_SESSION_SENTINEL);
        try {
            Files.write(sentinel.toPath(), (mode + "\n").getBytes(StandardCharsets.UTF_8));
            return true;
        } catch (IOException e) {
            Log.e(TAG, "writeDebugSession: " + e.getMessage());
            return false;
        }
    }

    private static void copyUriToFile(ContentResolver resolver, Uri src, File dest)
        throws IOException {
        try (InputStream in = resolver.openInputStream(src)) {
            if (in == null) {
                throw new IOException("openInputStream returned null");
            }
            File parent = dest.getParentFile();
            if (parent != null) {
                Files.createDirectories(parent.toPath());
            }
            Files.copy(in, dest.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private static void copyFileToUri(ContentResolver resolver, File src, Uri dest)
        throws IOException {
        try (InputStream in = Files.newInputStream(src.toPath());
             OutputStream out = resolver.openOutputStream(dest)) {
            if (out == null) {
                throw new IOException("openOutputStream returned null");
            }
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) >= 0) {
                if (n > 0) {
                    out.write(buf, 0, n);
                }
            }
        }
    }

    private void toast(String msg) {
        Toast.makeText(mActivity, msg, Toast.LENGTH_LONG).show();
    }
}
