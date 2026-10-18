#!/bin/sh
# Return to the stock app WITHOUT rebooting. A reboot does the same thing on its
# own (the bind-mount lives only in RAM), so this is just the faster path.
set -eu

STOCK="/usr/bin/MPC Sample"

echo "[mpc-mod] removing patched override"
# Drop the bind-mount(s). Loop in case it was stacked more than once.
while mountpoint -q "$STOCK" 2>/dev/null || grep -q " $STOCK " /proc/mounts 2>/dev/null; do
    umount "$STOCK" || { echo "[mpc-mod] umount failed - just reboot to restore stock" >&2; exit 1; }
done

if command -v systemctl >/dev/null 2>&1; then
    systemctl restart ac50.service || true
else
    pkill -f "/usr/bin/MPC Sample" || true
fi

echo "[mpc-mod] back on stock."
