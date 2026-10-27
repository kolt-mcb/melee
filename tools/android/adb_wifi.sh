#!/bin/bash
# Connect adb to a device's wireless debugging by its serial, for a device
# with no USB cable to this machine (the Chromecast on the TV).
#
#   tools/android/adb_wifi.sh [serial]     (default: the Chromecast, 26071HFDD6FZ39)
#
# The port changes at every boot, so it is looked up from the device's mDNS
# advertisement. The app's AdbWifiReceiver turns wireless debugging back on
# after each reboot; it needs, once, over USB:
#
#   adb shell pm grant com.melee.pcport android.permission.WRITE_SECURE_SETTINGS
#
# and the app launched once after that (a force-stopped app gets no boot
# broadcast until it is launched again). This machine must already be paired
# with the device (Developer options > Wireless debugging > Pair).
set -euo pipefail
SERIAL=${1:-26071HFDD6FZ39}
ADB=${ADB:-$(command -v adb || echo ~/Android/Sdk/platform-tools/adb)}
# adb's own mDNS auto-connect may already have it.
CUR=$("$ADB" devices | awk -v s="adb-$SERIAL-" 'index($1, s) == 1 && $2 == "device" {print $1; exit}')
[ -n "$CUR" ] && { echo "already connected; use: adb -s $CUR ..."; exit 0; }
lookup() {
    for _ in $(seq 15); do
        ADDR=$("$ADB" mdns services | awk -v s="adb-$SERIAL-" 'index($1, s) == 1 && $2 ~ /_adb-tls-connect/ {print $3; exit}')
        [ -n "$ADDR" ] && return 0
        sleep 2
    done
    return 1
}
try_connect() {
    # The first connect just after a disconnect can be refused; a retry is not.
    for _ in 1 2 3; do
        "$ADB" connect "$ADDR" | grep -q "^connected\|^already connected" && { echo "use: adb -s $ADDR ..."; exit 0; }
        sleep 2
    done
}
lookup || { echo "no wireless debugging advertised for $SERIAL"; exit 1; }
try_connect
# The adb server keeps an mDNS answer from before the device rebooted (last
# boot's port, now closed); a fresh server asks again.
echo "$ADDR refused; restarting the adb server for a fresh mDNS lookup"
"$ADB" disconnect "$ADDR" > /dev/null 2>&1 || true
"$ADB" kill-server; "$ADB" start-server
sleep 5
CUR=$("$ADB" devices | awk -v s="adb-$SERIAL-" 'index($1, s) == 1 && $2 == "device" {print $1; exit}')
[ -n "$CUR" ] && { echo "use: adb -s $CUR ..."; exit 0; }
lookup || { echo "no wireless debugging advertised for $SERIAL"; exit 1; }
try_connect
echo "could not connect to $ADDR"; exit 1
