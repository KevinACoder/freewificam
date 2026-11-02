#!/usr/bin/env bash
# Channel occupancy scan for the 2.4 GHz bench: is the chosen channel actually
# quiet, or does the throughput number depend on somebody else's traffic?
#
# For every channel it tunes a monitor interface, captures a few seconds, and
# sums the per-frame airtime that Wireshark computed (`wlan_radio.duration`) --
# that sum over the capture window is the channel busy ratio, which is the
# number that matters, not the frame count.
#
# Requires dumpcap + tshark on this host (the Pi in this lab).  The monitor must
# be a separate phy from the AP: a card cannot capture its own transmissions.
#
# Exit: 0 ok, 1 no frames captured on some channel, 2 usage/environment.
set -u

MON_IF=""
CHANNELS="1 3 6 9 11 13"
SECS=5
OUT="${OUT:-./channel-busy.json}"

usage() {
	sed -n '2,17p' "$0"
	cat <<'EOF'

Options:
  --mon-if IF      monitor netdev (required)
  --channels LIST  space separated channel numbers, default "1 3 6 9 11 13"
  --secs N         capture window per channel, default 5
  --out PATH       JSON result, default ./channel-busy.json
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--mon-if) MON_IF="${2:-}"; shift 2 ;;
	--channels) CHANNELS="${2:-}"; shift 2 ;;
	--secs) SECS="${2:-}"; shift 2 ;;
	--out) OUT="${2:-}"; shift 2 ;;
	-h|--help) usage; exit 0 ;;
	*) echo "unknown argument: $1" >&2; usage; exit 2 ;;
	esac
done

die() { echo "channel-busy: $*" >&2; exit 2; }
[ -n "$MON_IF" ] || die "--mon-if is required"
IW="${IW:-$(command -v iw || echo /usr/sbin/iw)}"
DUMPCAP="${DUMPCAP:-$(command -v dumpcap || echo /usr/bin/dumpcap)}"
TSHARK="${TSHARK:-$(command -v tshark || echo /usr/bin/tshark)}"
[ -x "$DUMPCAP" ] || die "dumpcap not found"
[ -x "$TSHARK" ] || die "tshark not found"

# tuning a monitor and opening a capture both need root; keep the script usable
# as a normal user with passwordless sudo (the lab Pi's setup)
if [ "$(id -u)" -eq 0 ]; then
	PRIV=""
else
	command -v sudo >/dev/null 2>&1 || die "need root or sudo"
	sudo -n true 2>/dev/null || die "need root or passwordless sudo"
	PRIV="sudo -n"
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# dumpcap's AppArmor profile denies /tmp and /srv; captures must land under a
# root-owned directory or dumpcap silently writes an empty file
CAPDIR="${CAPDIR:-/var/tmp/bench-capture}"
case "$CAPDIR" in
/tmp/*) die "CAPDIR must not be under /tmp (dumpcap writes 0 bytes there)" ;;
esac
$PRIV mkdir -p "$CAPDIR"
$PRIV chmod 777 "$CAPDIR" 2>/dev/null || true

echo "== channel occupancy on $MON_IF (${SECS}s per channel)"
rc=0
: >"$TMP/rows.tsv"
for ch in $CHANNELS; do
	f=$((2407 + 5 * ch))
	$PRIV $IW dev "$MON_IF" set channel "$ch" HT20 >/dev/null 2>&1
	if [ $? -ne 0 ]; then
		# some cards cannot tune every channel of the regulatory domain
		echo "ch=$ch skipped (retune failed)" >&2
		sleep 1
		continue
	fi
	out="$CAPDIR/ch$ch.pcapng"
	rm -f "$out"
	timeout $((SECS + 4)) $PRIV $DUMPCAP -i "$MON_IF" -w "$out" -a duration:"$SECS" >/dev/null 2>&1
	$PRIV chmod 644 "$out" 2>/dev/null || true
	frames=$($TSHARK -r "$out" 2>/dev/null | wc -l)
	busy_ms=$($TSHARK -r "$out" -T fields -e wlan_radio.duration 2>/dev/null |
		awk '{s += $1} END {printf "%.1f", s / 1000.0}')
	beacons=$($TSHARK -r "$out" -Y "wlan.fc.type_subtype==0x08" 2>/dev/null | wc -l)
	other_ssids=$($TSHARK -r "$out" -Y "wlan.fc.type_subtype==0x08" -T fields -e wlan.ssid 2>/dev/null |
		sort -u | tr '\n' ',' | sed 's/,$//')
	[ "$frames" -eq 0 ] && rc=1
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$ch" "$f" "$frames" "$busy_ms" "$beacons" "$other_ssids" >>"$TMP/rows.tsv"
	echo "  ch=$ch freq=$f frames=$frames air_ms=$busy_ms beacons=$beacons"
done

python3 - "$TMP/rows.tsv" "$OUT" "$SECS" <<'PY'
import json, sys
rows_path, out, secs = sys.argv[1:4]
secs = float(secs)
chans = []
for line in open(rows_path):
    p = line.rstrip("\n").split("\t")
    if len(p) < 6:
        continue
    ch, freq, frames, busy_ms, beacons, ssids = p
    chans.append({
        "channel": int(ch), "freq_mhz": int(freq), "frames": int(frames),
        "busy_pct": round(float(busy_ms or 0) / (secs * 1000.0) * 100.0, 2),
        "beacons": int(beacons),
        "other_ssids": [s for s in ssids.split(",") if s],
    })
quiet = min(chans, key=lambda c: c["busy_pct"]) if chans else None
res = {"window_s": secs, "channels": chans,
       "quietest": quiet["channel"] if quiet else None}
json.dump(res, open(out, "w"), indent=2)
print(json.dumps({"quietest": res["quietest"],
                  "busy_pct": {c["channel"]: c["busy_pct"] for c in chans}}, indent=2))
PY
exit $rc
