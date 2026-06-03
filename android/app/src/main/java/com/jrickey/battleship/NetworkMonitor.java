package com.jrickey.battleship;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;

/**
 * Registers a default-network callback and notifies native code when the active
 * network changes (WiFi ↔ LTE handoff, etc.) so mid-game reconnect can begin early.
 *
 * Registration is deferred until native arms transport after VS boot frontier sync.
 */
public final class NetworkMonitor {
    private static ConnectivityManager.NetworkCallback sCallback;
    private static ConnectivityManager sConnectivityManager;

    private NetworkMonitor() {}

    /** Called from native once mid-match transport is armed (not from Activity onCreate). */
    public static void install(Context context) {
        if (context == null || sCallback != null) {
            return;
        }
        ConnectivityManager cm =
                (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) {
            return;
        }
        sConnectivityManager = cm;
        sCallback =
                new ConnectivityManager.NetworkCallback() {
                    @Override
                    public void onAvailable(Network network) {
                        nativeNotifyNetworkChange();
                    }

                    @Override
                    public void onLost(Network network) {
                        nativeNotifyNetworkChange();
                    }

                    @Override
                    public void onCapabilitiesChanged(Network network, NetworkCapabilities caps) {
                        nativeNotifyNetworkChange();
                    }
                };
        try {
            cm.registerDefaultNetworkCallback(sCallback);
        } catch (RuntimeException ignored) {
            NetworkRequest req =
                    new NetworkRequest.Builder()
                            .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                            .build();
            cm.registerNetworkCallback(req, sCallback);
        }
    }

    public static void uninstall() {
        if (sCallback == null) {
            return;
        }
        ConnectivityManager cm = sConnectivityManager;
        if (cm != null) {
            try {
                cm.unregisterNetworkCallback(sCallback);
            } catch (RuntimeException ignored) {
            }
        }
        sCallback = null;
        sConnectivityManager = null;
    }

    /** Retain Activity context for deferred install (native calls install when armed). */
    public static native void bindContext(Context context);

    private static native void nativeNotifyNetworkChange();
}
