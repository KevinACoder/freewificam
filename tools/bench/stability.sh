#!/usr/bin/env bash
# AP bench stability test: prove the access point + link can carry a steady
# stream for minutes before any throughput number is taken.
#
# Why this exists: a bench can look fine for single 20 s cells and then drop the
# station every 1-3 minutes.  Every number measured in that window is worthless.
# A bench that cannot hold a link is not a bench.
#
# The test: send a fixed-rate UDP stream from this host to the DUT (a closed
# port is enough -- the DUT kernel counts it in Udp:NoPorts), sample the AP's
# per-station counters once a second, and decide PASS/FAIL on four criteria:
#   1. link never drops (station stays in `iw station dump`);
#   2. the AP transmits >= 99.5% of what the sender handed to the air;
#   3. AP `tx failed` does not grow;
#   4. the station's signal does not wander more than 10 dB.
#
# Exit: 0 PASS, 1 FAIL, 2 usage/environment.
set -u

AP_IF=""
MON_IF=""
FREQ="2432"
STA_IP=""
DUT_SSH=""
DUT_MAC=""
PPS=400
SIZE=1440
SECS=240
SENDER="${SENDER:-$(cd "$(dirname "$0")/../netio" && pwd)/ppsgen}"
OUT="${OUT:-./bench-stability.json}"
CHANNEL_LOG=""
DROP_TOL_PPM=5000      # 0.5%
SIGNAL_SPREAD_DB=10

usage() {
	sed -n '2,22p' "$0"
	cat <<'EOF'

Options:
  --ap-if IF       AP netdev on this host (e.g. wlan2)
  --mon-if IF      monitor netdev, only used to record the channel (optional)
  --freq MHz       channel centre frequency, default 2432 (channel 5)
  --sta-ip IP      DUT address that receives the UDP stream (closed port)
  --port N         destination port, default 19201
  --pps N          offered rate, default 400
  --secs N         duration, default 240
  --sender PATH    ppsgen binary, default tools/netio/ppsgen
  --out PATH       JSON result, default ./bench-stability.json
  --dut-mac MAC    sample only this station (the AP may carry other bench cards)
  --dut-ssh TARGET optional, read the DUT's Udp:NoPorts before/after
EOF
}

PORT=19201
while [ $# -gt 0 ]; do
	case "$1" in
	--ap-if) AP_IF="${2:-}"; shift 2 ;;
	--mon-if) MON_IF="${2:-}"; shift 2 ;;
	--freq) FREQ="${2:-}"; shift 2 ;;
	--sta-ip) STA_IP="${2:-}"; shift 2 ;;
	--port) PORT="${2:-}"; shift 2 ;;
	--pps) PPS="${2:-}"; shift 2 ;;
	--secs) SECS="${2:-}"; shift 2 ;;
	--sender) SENDER="${2:-}"; shift 2 ;;
	--out) OUT="${2:-}"; shift 2 ;;
	--dut-mac) DUT_MAC="${2:-}"; shift 2 ;;
	--dut-ssh) DUT_SSH="${2:-}"; shift 2 ;;
	-h|--help) usage; exit 0 ;;
	*) echo "unknown argument: $1" >&2; usage; exit 2 ;;
	esac
done

die() { echo "bench-stability: $*" >&2; exit 2; }
[ -n "$AP_IF" ] || die "--ap-if is required"
[ -n "$STA_IP" ] || die "--sta-ip is required"
[ -x "$SENDER" ] || die "sender not executable: $SENDER (build tools/netio/ppsgen.c)"
IW="${IW:-$(command -v iw || echo /usr/sbin/iw)}"
[ -x "$IW" ] || die "iw not found (set IW=/path/to/iw)"
if [ "$(id -u)" -eq 0 ]; then
	PRIV=""
else
	command -v sudo >/dev/null 2>&1 || die "need root or sudo"
	sudo -n true 2>/dev/null || die "need root or passwordless sudo"
	PRIV="sudo -n"
fi

station() {
	if [ -n "$DUT_MAC" ]; then
		$PRIV $IW dev "$AP_IF" station dump 2>/dev/null | \
			awk -v mac="$DUT_MAC" '
				/^Station / { p = (index($0, "Station " mac) == 1) }
				p { print }
			'
	else
		$PRIV $IW dev "$AP_IF" station dump 2>/dev/null
	fi
}
field() { station | awk -v k="$1" '$0 ~ k {gsub(/ /,"",$0); sub("^[^:]*:","",$0); print; exit}'; }
num() { field "$1" | tr -dc '0-9.-'; }

AP_MAC=$($PRIV $IW dev "$AP_IF" info 2>/dev/null | awk '/addr /{print $2; exit}')
FREQ_NOW=$($PRIV $IW dev "$AP_IF" info 2>/dev/null | sed -n 's/.*channel [0-9]* (\([0-9]*\) MHz).*/\1/p' | head -1)
[ -n "$AP_MAC" ] || die "cannot read $AP_IF info; is the AP up?"
[ "$FREQ_NOW" = "$FREQ" ] 2>/dev/null || echo "warn: AP is on ${FREQ_NOW:-?} MHz, expected $FREQ" >&2

dut_noport() {
	[ -n "$DUT_SSH" ] || return 1
	ssh -o BatchMode=yes "$DUT_SSH" "grep '^Udp:' /proc/net/snmp | tail -1 | awk '{print \$3}'" 2>/dev/null
}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
echo "== bench stability: AP=$AP_IF mac=$AP_MAC ${FREQ} MHz -> $STA_IP:$PORT, ${PPS} pps x ${SECS}s dut=${DUT_MAC:-any}"

NP0=$(dut_noport || true)
TX0=$(num "tx packets"); FAIL0=$(num "tx failed"); RETRY0=$(num "tx retries")

# sample the station once a second for the whole run
(
	: >"$TMP/samples.tsv"
	i=0
	while [ $i -lt "$SECS" ]; do
		s=$(station)
		if [ -z "$s" ]; then
			printf '%s\tMISSING\t\t\t\n' "$i" >>"$TMP/samples.tsv"
		else
			printf '%s\t%s\t%s\t%s\t%s\n' "$i" \
				"$(printf '%s\n' "$s" | awk '/tx bitrate/{print $3; exit}')" \
				"$(printf '%s\n' "$s" | awk '/tx failed/{print $3; exit}')" \
				"$(printf '%s\n' "$s" | awk '/tx retries/{print $3; exit}')" \
				"$(printf '%s\n' "$s" | awk '/signal:/{print $2; exit}')" >>"$TMP/samples.tsv"
		fi
		i=$((i + 1))
		sleep 1
	done
) &
SAMPLER=$!

"$SENDER" -d "$STA_IP" -p "$PORT" -s "$SIZE" -r "$PPS" -t "$SECS" -B 64 -k 0 \
	>"$TMP/sender.log" 2>&1
SENT=$(sed -n 's/.*sent=\([0-9]*\).*/\1/p' "$TMP/sender.log" | tail -1)
SECS_ACT=$(sed -n 's/.*secs=\([0-9.]*\).*/\1/p' "$TMP/sender.log" | tail -1)
wait $SAMPLER 2>/dev/null

NP1=$(dut_noport || true)
TX1=$(num "tx packets"); FAIL1=$(num "tx failed"); RETRY1=$(num "tx retries")

MISSING=$(awk -F'\t' '$2=="MISSING"' "$TMP/samples.tsv" | wc -l)
DROP_LOG=""
if [ -n "${CHANNEL_LOG:-}" ] && [ -f "$CHANNEL_LOG" ]; then
	CHANNEL_LOG="$CHANNEL_LOG"
fi

python3 - "$TMP/samples.tsv" "$OUT" "$AP_IF" "$AP_MAC" "$DUT_MAC" "$FREQ" "$PPS" "$SECS" \
	"${SENT:-0}" "${SECS_ACT:-0}" "${TX0:-0}" "${TX1:-0}" "${FAIL0:-0}" "${FAIL1:-0}" \
	"${RETRY0:-0}" "${RETRY1:-0}" "${NP0:-}" "${NP1:-}" "$MISSING" \
	"$DROP_TOL_PPM" "$SIGNAL_SPREAD_DB" <<'PY'
import json, sys
(args) = sys.argv[1:]
(samples, out, ap_if, ap_mac, dut_mac, freq, pps, secs, sent, secs_act,
 tx0, tx1, fail0, fail1, retry0, retry1, np0, np1, missing, drop_tol, spread_dbg) = args
rows = []
for line in open(samples):
    p = line.rstrip("\n").split("\t")
    if len(p) < 5:
        continue
    rows.append({"t": int(p[0]), "bitrate": p[1], "failed": p[2],
                 "retries": p[3], "signal": p[4]})
sig = []
for r in rows:
    try:
        sig.append(float(r["signal"]))
    except ValueError:
        pass
d_tx = int(tx1) - int(tx0)
spread = (max(sig) - min(sig)) if len(sig) >= 2 else None
sent = int(sent)
loss_ppm = 0 if not sent else max(0, sent - d_tx) * 1_000_000 // sent
dut_ok = None
if np0.isdigit() and np1.isdigit():
    delivered = int(np1) - int(np0)
    dut_ok = {"delivered": delivered, "sent": sent,
              "loss_ppm": 0 if not sent else max(0, sent - delivered) * 1_000_000 // sent}
checks = {
    "link_never_dropped": int(missing) == 0,
    "ap_transmitted_enough": loss_ppm <= int(drop_tol),
    "no_tx_fail_growth": int(fail1) - int(fail0) == 0,
    "signal_stable": spread is not None and spread <= float(spread_dbg),
}
res = {
    "ap_if": ap_if, "ap_mac": ap_mac, "dut_mac": dut_mac or None,
    "freq_mhz": int(freq),
    "pps": int(pps), "secs": float(secs_act or secs), "sent": sent,
    "ap_tx_packets": d_tx, "ap_tx_loss_ppm": loss_ppm,
    "ap_tx_failed_delta": int(fail1) - int(fail0),
    "ap_tx_retries_delta": int(retry1) - int(retry0),
    "station_missing_samples": int(missing),
    "signal_range_dbm": spread, "dut": dut_ok,
    "checks": checks,
    "verdict": "PASS" if all(checks.values()) else "FAIL",
}
json.dump(res, open(out, "w"), indent=2)
print(json.dumps(res, indent=2))
sys.exit(0 if res["verdict"] == "PASS" else 1)
PY
