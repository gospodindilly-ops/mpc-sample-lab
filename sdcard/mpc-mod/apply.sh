#!/bin/sh
# On-device applier for the MPC Sample mod. Runs from the SD card, under the
# device's busybox ash. Written to be SAFE BY DEFAULT:
#
#   * reads the stock app binary from the running system (never ships one),
#   * refuses unless its SHA-256 matches the patch set's target,
#   * applies patches to a COPY in RAM (/tmp) - internal flash is never written,
#   * activates the copy with a bind-mount, so a reboot ALWAYS restores stock,
#   * has a crash guard: if the previous attempt did not finish cleanly, it
#     backs off and leaves stock running.
#
# This script is a TEMPLATE. The spots that depend on the exact unit are marked
# TODO - you need a root shell / matching patch set to finish them. Nothing here
# removes signing or touches paid-content checks; that is out of scope on purpose.
set -eu

# ---- paths -----------------------------------------------------------------
# STOCK and WORK default to the real device paths. They can be overridden by the
# environment so the script can be dry-run or tested on a workstation without
# touching the real locations; the device never sets them, so the defaults apply.
HERE=$(cd "$(dirname "$0")" && pwd)
STOCK="${MPC_STOCK:-/usr/bin/MPC Sample}"   # launched by /usr/bin/launch-ac50
PATCH_LIST="$HERE/patches.list"       # produced by: mpcpatch.py emit-device
WORK="${MPC_WORK:-/tmp/mpc-mod}"            # tmpfs - gone on reboot
OUT="$WORK/MPC-Sample-patched"
PENDING="$HERE/.pending"              # crash-guard marker, lives on the SD
MAX_FAILS=2                           # after this many bad attempts, stop trying

log() { echo "[mpc-mod] $*"; }
die() { echo "[mpc-mod] ERROR: $*" >&2; exit 1; }

# ---- crash guard -----------------------------------------------------------
# If .pending exists, a previous run started but never cleared it (crash / bad
# patch / power loss). Count strikes; once we hit MAX_FAILS, refuse and leave
# the stock OS alone so the unit is never stuck in a boot loop of our making.
fails=0
[ -f "$PENDING" ] && fails=$(cat "$PENDING" 2>/dev/null || echo 0)
case "$fails" in *[!0-9]*) fails=0 ;; esac
if [ "$fails" -ge "$MAX_FAILS" ]; then
    die "previous attempts failed ${fails}x - staying on stock. Delete $PENDING to re-arm."
fi
echo $((fails + 1)) > "$PENDING"

# ---- preconditions ---------------------------------------------------------
[ -r "$STOCK" ]      || die "cannot read stock binary: $STOCK"
[ -r "$PATCH_LIST" ] || die "missing $PATCH_LIST (run: mpcpatch.py emit-device)"
command -v sha256sum >/dev/null 2>&1 || die "sha256sum not found"
command -v base64    >/dev/null 2>&1 || die "base64 not found"

TARGET_SHA=$(grep '^#target-sha256 ' "$PATCH_LIST" | cut -d' ' -f2)
RESULT_SHA=$(grep '^#result-sha256 ' "$PATCH_LIST" | cut -d' ' -f2)
[ -n "$TARGET_SHA" ] || die "patch list has no #target-sha256"

# ---- verify stock ----------------------------------------------------------
STOCK_SHA=$(sha256sum "$STOCK" | cut -d' ' -f1)
if [ "$STOCK_SHA" != "$TARGET_SHA" ]; then
    die "stock sha256 $STOCK_SHA != target $TARGET_SHA - wrong firmware, refusing."
fi
log "stock binary verified against target"

# ---- build patched copy in RAM --------------------------------------------
rm -rf "$WORK"; mkdir -p "$WORK"
cp "$STOCK" "$OUT"

# Apply each patch: verify the original bytes, then overwrite in place with dd.
# Line format: "offset_decimal base64(original) base64(patched)".
while read -r off b64o b64p; do
    case "$off" in ''|\#*) continue ;; esac   # skip blanks and header comments
    printf '%s' "$b64o" | base64 -d > "$WORK/orig.bin"
    printf '%s' "$b64p" | base64 -d > "$WORK/new.bin"
    len=$(wc -c < "$WORK/new.bin")
    cur=$(dd if="$OUT" bs=1 skip="$off" count="$len" 2>/dev/null | sha256sum | cut -d' ' -f1)
    exp=$(sha256sum "$WORK/orig.bin" | cut -d' ' -f1)
    [ "$cur" = "$exp" ] || die "patch at offset $off: bytes do not match 'original'"
    dd if="$WORK/new.bin" of="$OUT" bs=1 seek="$off" conv=notrunc 2>/dev/null
done < "$PATCH_LIST"
log "patches applied to $OUT"

# ---- optional result check -------------------------------------------------
if [ -n "$RESULT_SHA" ]; then
    GOT=$(sha256sum "$OUT" | cut -d' ' -f1)
    [ "$GOT" = "$RESULT_SHA" ] || die "patched result $GOT != expected $RESULT_SHA"
    log "patched result matches result.sha256"
fi
chmod +x "$OUT"

# ---- activate --------------------------------------------------------------
# Bind-mount the patched copy over the stock path. This lives only in RAM: a
# reboot drops the mount and you are back on stock.
#
# TODO: confirm these on your unit (needs a root shell):
#   - the rootfs allows a bind-mount over "$STOCK" (it does on a read-only
#     squashfs - the mount is in the kernel's VFS, not on flash),
#   - the service name is 'ac50' and systemctl is available.
log "activating patched app (bind-mount, RAM-only)"
mount --bind "$OUT" "$STOCK" || die "bind-mount failed (need root). Nothing changed on flash."

# The point of no return is now BEHIND us: the bind-mount is in place and is fully
# reversible by a reboot or rollback.sh. Clear the crash guard and record success
# *before* restarting the app, not after. Static RE of fw 1.3.0.12 says this
# script runs under az01-script-runner's cgroup (it is run by that root daemon via
# system()), not ac50's, so `systemctl restart ac50` should not kill us. But if
# that ever turns out wrong on a given unit, a restart that kills us here must not
# be able to strand a .pending marker - that would make the NEXT run read this
# success as a failure and, after MAX_FAILS, refuse to run at all. So: clear and
# log done first, then restart.
rm -f "$PENDING"
log "done. Reboot (or run rollback.sh) to return to stock."

# Kick the app so it re-execs through the bind-mounted path. Best-effort: by now
# activation is already complete and logged, so it is fine if this does not return
# (e.g. if we do share ac50's cgroup after all - we would be killed, having already
# cleared .pending and logged success).
if command -v systemctl >/dev/null 2>&1; then
    systemctl restart ac50.service \
        || log "WARN: could not restart ac50.service - relaunch the app or reboot to pick up the change"
else
    # Fallback: ac50.service has Restart=always, so killing the app relaunches
    # it - now through the bind-mounted (patched) path.
    pkill -f "/usr/bin/MPC Sample" || true
fi
