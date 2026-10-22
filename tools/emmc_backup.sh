#!/bin/sh
# Read-only eMMC backup for MPC Sample. Usage: ssh ... root@192.168.155.1 'sh -s' < tools/emmc_backup.sh
# Reads /dev/mmcblk0*; writes ONLY under $O on the SD card. No global sync.
set -eu
set -o pipefail 2>/dev/null || true
trap '' HUP PIPE  # keep going if the SSH session drops; the log on SD is the record

O="/media/MPC SAMPLE/emmc-backup-$(date +%F)"
D=/dev/mmcblk0
S=/sys/block/mmcblk0

[ "$(cat $S/size)" = 15269888 ] || { echo "ABORT: unexpected disk size"; exit 1; }
mkdir -p "$O"
LOG="$O/backup.log"
log() { m="$(date -u +%H:%M:%S) $*"; echo "$m" >> "$LOG"; echo "$m" 2>/dev/null || true; }

rm -f "$O/.probe" "$O/SHA256SUMS" "$O/layout.txt"  # leftovers of the aborted first run only

{ echo "disk_sectors=$(cat $S/size) sector=512"
  for p in $S/mmcblk0p*; do echo "${p##*/} start=$(cat $p/start) size=$(cat $p/size) $(grep PARTNAME $p/uevent)"; done
  echo "emmc=$(cat $S/device/name) manfid=$(cat $S/device/manfid) date=$(cat $S/device/date)"
} > "$O/layout.txt"
: > "$O/SHA256SUMS"

# piece NAME SRC SKIP COUNT  (512-byte sectors; empty COUNT = whole SRC device)
piece() {
  n=$1; src=$2; skip=$3; cnt=$4; f="$O/$n.img"; try=1
  while :; do
    if [ -n "$cnt" ]; then
      dd if="$src" of="$f" bs=512 skip="$skip" count="$cnt" 2>/dev/null
      hs=$(dd if="$src" bs=512 skip="$skip" count="$cnt" 2>/dev/null | sha256sum | cut -d' ' -f1)
      want=$((cnt * 512))
    else
      dd if="$src" of="$f" bs=1M 2>/dev/null
      hs=$(sha256sum "$src" | cut -d' ' -f1)
      want=$(( $(cat /sys/class/block/${src##*/}/size) * 512 ))
    fi
    hf=$(sha256sum "$f" | cut -d' ' -f1)
    got=$(stat -c %s "$f")
    if [ "$hs" = "$hf" ] && [ "$got" = "$want" ]; then
      echo "$hf  $n.img" >> "$O/SHA256SUMS"; log "OK $n $got $hf"; return 0
    fi
    if [ $try -ge 2 ]; then
      echo "$hf  $n.img" >> "$O/SHA256SUMS"; log "MISMATCH $n size=$got/$want src=$hs file=$hf"; return 0
    fi
    log "retry $n (src=$hs file=$hf size=$got/$want)"; try=2
  done
}

# expected layout, checked against sysfs before reading each partition
check() { [ "$(cat $S/$1/start) $(cat $S/$1/size)" = "$2 $3" ] || { log "ABORT: $1 layout changed"; exit 1; }; }

log "START"
piece 00-head_s0-1087 $D 0 1088
check mmcblk0p1 1088 960;      piece p01-spl1     ${D}p1 0 ""
piece 01b-gap_s2048-2111 $D 2048 64
check mmcblk0p2 2112 960;      piece p02-spl2     ${D}p2 0 ""
check mmcblk0p3 3072 1024;     piece p03-env1     ${D}p3 0 ""
check mmcblk0p4 4096 1024;     piece p04-env2     ${D}p4 0 ""
check mmcblk0p5 5120 8192;     piece p05-uboot1   ${D}p5 0 ""
check mmcblk0p6 13312 8192;    piece p06-uboot2   ${D}p6 0 ""
check mmcblk0p7 21504 131072;  piece p07-factory  ${D}p7 0 ""
check mmcblk0p8 152576 262144; piece p08-boot     ${D}p8 0 ""
check mmcblk0p9 414720 262144; piece p09-recovery ${D}p9 0 ""
check mmcblk0p10 676864 2097152;   piece p10-rootfs  ${D}p10 0 ""
check mmcblk0p11 2774016 8388608;  piece p11-content ${D}p11 0 ""
check mmcblk0p12 11162624 4103168; piece p12-data    ${D}p12 0 ""
piece 99-tail_s15265792-15269887 $D 15265792 4096
piece hw-mmcblk0boot0 ${D}boot0 0 ""
piece hw-mmcblk0boot1 ${D}boot1 0 ""
# BusyBox dd has no conv=fsync and global sync is avoided (kit notes a hang); wait for normal writeback instead
i=0
while [ $i -lt 180 ]; do
  d=$(awk '/^(Dirty|Writeback):/{s+=$2} END{print s}' /proc/meminfo)
  [ "$d" -lt 1024 ] && break
  sleep 1; i=$((i + 1))
done
log "writeback: dirty+writeback=${d}kB after ${i}s"
log "DONE"
