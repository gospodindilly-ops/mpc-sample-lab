#!/bin/sh
# Receive-only capture of the control surface for 3 minutes; ac50 is always started again on exit.
# Usage (from the Mac):
#   ssh ... root@192.168.155.1 'cat > /run/surface_capture.sh' < tools/surface_capture.sh
#   ssh ... root@192.168.155.1 'systemd-run --unit=mpc-capture --collect --wait /bin/sh /run/surface_capture.sh; cat /run/mpc-capture/run.log'
#   ssh ... root@192.168.155.1 'cat /run/mpc-capture/dump.txt' > captures/surface-capture-NEW.txt
# The app stops 45 s after start (time to save the project); press controls once the screen freezes.
OUT=/run/mpc-capture
mkdir -p $OUT
exec > $OUT/run.log 2>&1
trap "systemctl start ac50.service; echo ac50=\$(systemctl is-active ac50.service) \$(date +%T)" EXIT
echo "grace $(date +%T)"; sleep 45
echo "stop ac50 $(date +%T)"; systemctl stop ac50.service; sleep 1
amidi -p hw:Surface,0 -d -a -T monotonic > $OUT/dump.txt 2>&1 &
P=$!
echo "capture start $(date +%T) mono=$(cut -d" " -f1 /proc/uptime) pid=$P"
sleep 180
kill $P 2>/dev/null; wait $P 2>/dev/null
echo "capture end $(date +%T) lines=$(wc -l < $OUT/dump.txt)"
