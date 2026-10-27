#!/bin/sh
# Linux ground truth for the U-disk ladder: same disk, same window, same
# command sizes as the RTOS `usb_msc` case.
#
# Run on the board after booting the Linux reference kernel and the ramdisk.
# The script takes its output directory as the first argument and its block
# device from DEV; both are the caller's, so the same script runs from a
# network share, a USB stick or the ramdisk itself:
#   DEV=/dev/sda sh fio-udisk.sh ./linux-gt
#
# Window (identical to usb_msc.c): the disk's last 32 MiB, aligned down to
# 1 MiB.  Every arm stays inside it; the first 1 MiB (partition table) is never
# touched.  The disk is a test disk: the window's old contents are overwritten.
#
# Output: <out>/fio.tsv (one row per run) + <out>/meta.txt (device, controller,
# queue knobs, /sys/block stat deltas) + the per-run fio reports.
# Columns: op size_kib rep bw_mib_s iops cmds bw_raw
# `bw_mib_s` is the number to compare with the RTOS median; `cmds` is the
# block-layer command count (from /sys/block/<dev>/stat), not "bytes / size" -
# max_sectors_kb splits large requests and hides the real command count.
set -u

OUT=${1:-./linux-gt}
DEV=${DEV:-/dev/sda}
MIB=1048576
WINDOW_BYTES=$((32 * 1024 * 1024))
SECTOR=512
WINDOW_SECTORS=$((WINDOW_BYTES / SECTOR))

mkdir -p "$OUT"

BLOCKS=$(blockdev --getsz "$DEV" 2>/dev/null || echo 0)
if [ "$BLOCKS" -le 0 ] 2>/dev/null; then
	echo "FIO_UDISK: no block device at $DEV" >&2
	exit 1
fi
if [ "$BLOCKS" -le $((WINDOW_SECTORS + 8192)) ]; then
	echo "FIO_UDISK: $DEV too small for the 32 MiB window ($BLOCKS sectors)" >&2
	exit 1
fi
START=$(( (BLOCKS - WINDOW_SECTORS) & ~2047 ))
START_BYTES=$((START * SECTOR))
END_BYTES=$(( (START + WINDOW_SECTORS) * SECTOR ))

SYS=/sys/block/$(basename "$DEV")

stat_line() { cat "$SYS/stat" 2>/dev/null | tr '\n' ' '; echo; }

{
	echo "dev=$DEV"
	echo "blocks_512=$BLOCKS"
	echo "sector=$SECTOR"
	echo "lba_start=$START"
	echo "lba_end=$((START + WINDOW_SECTORS))"
	echo "window_bytes=$WINDOW_BYTES"
	echo "kernel=$(uname -r)"
	echo "boot_id=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)"
	echo "date=$(date -Is 2>/dev/null)"
	echo "max_sectors_kb=$(cat $SYS/queue/max_sectors_kb 2>/dev/null)"
	echo "nr_requests=$(cat $SYS/queue/nr_requests 2>/dev/null)"
	echo "scheduler=$(cat $SYS/queue/scheduler 2>/dev/null)"
	echo "rotational=$(cat $SYS/queue/rotational 2>/dev/null)"
	echo "vendor=$(cat $SYS/device/vendor 2>/dev/null)"
	echo "model=$(cat $SYS/device/model 2>/dev/null)"
	echo "serial=$(cat $SYS/device/serial 2>/dev/null)"
	echo "--- lsusb -t ---"
	lsusb -t 2>/dev/null || true
	echo "--- usb speed/ids ---"
	for d in /sys/bus/usb/devices/*; do
		[ -f "$d/idVendor" ] || continue
		v=$(cat "$d/idVendor" 2>/dev/null)
		p=$(cat "$d/idProduct" 2>/dev/null)
		s=$(cat "$d/speed" 2>/dev/null)
		m=$(cat "$d/manufacturer" 2>/dev/null)
		pr=$(cat "$d/product" 2>/dev/null)
		echo "$d $v:$p speed=$s $m $pr"
	done
	echo "--- stat before ---"
	stat_line
} > "$OUT/meta.txt" 2>&1

# fio is part of the mfs package set; psync + direct is the one-command-at-a-time
# shape the RTOS MSC path issues.
ENGINE=psync
if ! fio --enghelp 2>/dev/null | grep -qx "$ENGINE"; then
	ENGINE=libaio
fi
echo "engine=$ENGINE" >> "$OUT/meta.txt"

printf 'op\tsize_kib\trep\tbw_mib_s\tiops\tcmds\tbw_raw\n' > "$OUT/fio.tsv"

field() { # $1 = stat line, $2 = field index (1-based)
	echo "$1" | awk -v n="$2" '{print $n}'
}

run_one() { # op size_kib rep offset_bytes size_bytes
	op=$1; size_kib=$2; rep=$3; off=$4; sz=$5
	report="$OUT/fio-${op}-${size_kib}k-r${rep}.txt"
	before=$(stat_line)
	rm -f "$report"
	fio --name="udisk-${op}-${size_kib}-r${rep}" \
		--filename="$DEV" --rw="$op" --bs=$((size_kib * 1024)) \
		--iodepth=1 --ioengine="$ENGINE" --direct=1 \
		--size="$sz" --offset="$off" --numjobs=1 \
		--group_reporting --output="$report" >/dev/null 2>&1
	after=$(stat_line)
	bw_raw=$(grep -o 'bw=[0-9.]*[KMG]iB/s' "$report" 2>/dev/null | tail -1)
	iops=$(grep -o 'iops=[0-9.]*[kM]*' "$report" 2>/dev/null | tail -1 | cut -d= -f2)
	case "$bw_raw" in
	*bw=*KiB/s*) bw=$(echo "$bw_raw" | sed 's/.*bw=\([0-9.]*\)KiB\/s/\1/' | awk '{printf "%.3f", $1/1024}') ;;
	*bw=*MiB/s*) bw=$(echo "$bw_raw" | sed 's/.*bw=\([0-9.]*\)MiB\/s/\1/') ;;
	*bw=*GiB/s*) bw=$(echo "$bw_raw" | sed 's/.*bw=\([0-9.]*\)GiB\/s/\1/' | awk '{printf "%.3f", $1*1024}') ;;
	*) bw=0 ;;
	esac
	case "$op" in
	read) c0=$(field "$before" 1); c1=$(field "$after" 1) ;;
	write) c0=$(field "$before" 5); c1=$(field "$after" 5) ;;
	*) c0=0; c1=0 ;;
	esac
	cmds=$((c1 - c0))
	printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$op" "$size_kib" "$rep" \
		"$bw" "${iops:-0}" "$cmds" "${bw_raw:-none}" >> "$OUT/fio.tsv"
	echo "FIO $op ${size_kib}KiB r$rep $bw MiB/s ${iops:-?} iops cmds=$cmds"
}

# The throughput arms: 8 MiB per run at each command size, three runs, rotating
# through the window so a run never reads the bytes the previous one wrote.
for size_kib in 4 64 1024; do
	size_bytes=$((size_kib * 1024))
	cmds=$(( (8 * MIB) / size_bytes ))
	[ "$cmds" -lt 1 ] && cmds=1
	run_bytes=$((cmds * size_bytes))
	for rep in 1 2 3; do
		off=$((START_BYTES + (rep - 1) * run_bytes))
		[ $((off + run_bytes)) -gt "$END_BYTES" ] && off=$START_BYTES
		run_one read "$size_kib" "$rep" "$off" "$run_bytes"
	done
	for rep in 1 2 3; do
		off=$((START_BYTES + (rep - 1) * run_bytes))
		[ $((off + run_bytes)) -gt "$END_BYTES" ] && off=$START_BYTES
		run_one write "$size_kib" "$rep" "$off" "$run_bytes"
	done
done

# The size sweep: the RTOS case's fixed command count per size, which is what
# the per-command fixed cost is fitted from.
for size_kib in 8 16 32 64 128; do
	size_bytes=$((size_kib * 1024))
	run_bytes=$((size_bytes * 256))
	for rep in 1 2; do
		off=$START_BYTES
		run_one read "$size_kib" "$rep" "$off" "$run_bytes"
	done
done

echo "--- stat after ---" >> "$OUT/meta.txt"
stat_line >> "$OUT/meta.txt"
echo "FIO_UDISK_DONE out=$OUT"
