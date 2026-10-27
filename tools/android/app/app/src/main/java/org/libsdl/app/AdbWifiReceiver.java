package org.libsdl.app;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.provider.Settings;
import android.util.Log;

/**
 * Turns wireless debugging back on after a reboot, for a device with no USB
 * connection to the development machine (a Chromecast on a TV's HDMI port).
 *
 * Android switches wireless debugging off at every boot (adb_wifi_enabled
 * goes back to 0), and setting it again needs WRITE_SECURE_SETTINGS, which
 * an app only gets from adb, once:
 *
 *   adb shell pm grant com.melee.pcport android.permission.WRITE_SECURE_SETTINGS
 *
 * Without the grant this does nothing. With it: at boot, a Wi-Fi network
 * callback is registered through a PendingIntent that comes back here, and
 * each time Wi-Fi is up the setting is put back to 1 -- the system itself
 * turns it off whenever Wi-Fi drops. The network must already be one the
 * device trusts for wireless debugging ("Always allow on this network"),
 * and a host it is paired with reconnects by mDNS on its own.
 */
public class AdbWifiReceiver extends BroadcastReceiver {
    private static final String TAG = "melee";
    private static final String ADB_WIFI_ENABLED = "adb_wifi_enabled";
    private static final String ACTION_WIFI = "com.melee.pcport.WIFI_AVAILABLE";

    @Override
    public void onReceive(Context ctx, Intent intent) {
        if (ctx.checkSelfPermission("android.permission.WRITE_SECURE_SETTINGS")
                != PackageManager.PERMISSION_GRANTED) {
            return;
        }
        String action = intent.getAction();
        if (Intent.ACTION_BOOT_COMPLETED.equals(action)
                || Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)) {
            watchWifi(ctx);
        }
        enableIfOnWifi(ctx);
    }

    private static void watchWifi(Context ctx) {
        ConnectivityManager cm = ctx.getSystemService(ConnectivityManager.class);
        Intent i = new Intent(ACTION_WIFI).setClass(ctx, AdbWifiReceiver.class);
        PendingIntent pi = PendingIntent.getBroadcast(ctx, 0, i,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE);
        NetworkRequest req = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .build();
        cm.registerNetworkCallback(req, pi);
    }

    private static void enableIfOnWifi(Context ctx) {
        ConnectivityManager cm = ctx.getSystemService(ConnectivityManager.class);
        boolean wifi = false;
        for (Network n : cm.getAllNetworks()) {
            NetworkCapabilities nc = cm.getNetworkCapabilities(n);
            if (nc != null && nc.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                wifi = true;
            }
        }
        if (!wifi) {
            return;
        }
        try {
            if (Settings.Global.getInt(ctx.getContentResolver(), ADB_WIFI_ENABLED, 0) != 1) {
                Settings.Global.putInt(ctx.getContentResolver(), ADB_WIFI_ENABLED, 1);
                Log.i(TAG, "[ADBWIFI] wireless debugging turned on");
            }
        } catch (SecurityException e) {
            Log.w(TAG, "[ADBWIFI] cannot write " + ADB_WIFI_ENABLED + ": " + e);
        }
    }
}
