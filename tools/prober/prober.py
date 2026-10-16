#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""prober - the wireless measurement peer for the board ladders.

One script, four radio roles and one traffic role, so a Linux host with a
monitor-capable USB card can play the far end of a board test: it can capture
the air, join an access point the board hosts, host an access point the board
joins, and drive the traffic that a throughput phase is measured on. It
replaces the bench's Windows-side client and capture tooling with something
that lives in this repository and runs on any Linux host.

The card roles are resolved at run time from the USB identity (vendor:product)
rather than from an interface name, because interface numbering is not stable
when more than one card is attached. Nothing here is host-specific: the same
script runs on the measurement host and, with --host, on this machine driving
that host over ssh.

Usage:
  prober.py radios [--host user@addr]
  prober.py mon up   --iface <if> --channel <mhz> [--width HT20] [--out <pcap>]
                     [--duration <s>] [--host user@addr]
  prober.py mon down --iface <if> [--host user@addr]
  prober.py mon status --iface <if> [--host user@addr]
  prober.py sta up   --iface <if> --wpa-conf <file> [--host user@addr]
  prober.py sta down --iface <if> [--host user@addr]
  prober.py ap  up   --iface <if> --ssid <name> --psk <key> --channel <mhz>
                     [--addr <cidr>] [--host user@addr]
  prober.py ap  down [--host user@addr]
  prober.py traffic --peer <addr> --port <p> --seconds <s> [--tool iperf|iperf3]
                    [--reverse] [--server] [--host user@addr]
  prober.py run --serial-hub <url> --out <dir> [--phases <n>] [--seconds <s>]
                [--uplink-phases <n>] [--iface <if>] [--channel <mhz>]
                [--width HT20] [--wpa-conf <file>] [--peer <addr>]
                [--udp] [--window <size>] [--phase-actions <spec>]
                [--ping-during] [--ss-during] [--host user@addr]
  prober.py analyze <pcap> [--sta <mac>] [--out <json>] [--host user@addr]
  prober.py selftest

Exit status: 0 ok, 1 a measurement or verdict failed, 2 usage, configuration
or environment error (missing tool, missing radio, unreachable hub).

Credentials are never read from this repository: the access point's SSID and
key, and any wpa_supplicant configuration, are passed as arguments or as a
file kept outside it.
"""

from __future__ import annotations

import base64
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

# --------------------------------------------------------------------------
# constants
# --------------------------------------------------------------------------

# The USB identities this tool knows how to place. The identity, not the
# interface name, is what the roles are resolved from (see the module note).
CHIPSETS = (
    ("0cf3:9271", "ar9271", "ath9k_htc", "2.4GHz", "11n"),
    ("0e8d:7961", "mt7921au", "mt7921u", "2.4+5GHz", "ax"),
    ("0e8d:7902", "mt7921au", "mt7921u", "2.4+5GHz", "ax"),
    ("0e8d:7612", "mt7612u", "mt76x2u", "2.4+5GHz", "ac"),
)

# 802.11 frame type/subtype values as the capture reports them (the 2-bit type
# in the high nibble). Named so the analysis reads as protocol, not as numbers.
SUBTYPE_NAMES = {
    0x00: "assoc-req", 0x01: "assoc-resp", 0x04: "probe-req",
    0x05: "probe-resp", 0x08: "beacon", 0x0B: "auth", 0x0C: "deauth",
    0x0D: "action",
    0x18: "blockack-req", 0x19: "blockack", 0x1B: "rts", 0x1C: "cts",
    0x1D: "ack",
    0x20: "data", 0x24: "null", 0x28: "qos-data", 0x2C: "qos-null",
}
MANAGEMENT = (0x00, 0x01, 0x04, 0x05, 0x08, 0x0B, 0x0C, 0x0D)
CONTROL = (0x18, 0x19, 0x1B, 0x1C, 0x1D)
DATA = (0x20, 0x24, 0x28, 0x2C)

# The capture fields the analysis wants, most-wanted first per concept. The set
# that actually exists is discovered from the capture tool's own field list, so
# a build that names one of them differently degrades to "not available"
# instead of silently reading zeros.
FIELD_CANDIDATES = {
    "time": ("frame.time_epoch",),
    "subtype": ("wlan.fc.type_subtype",),
    "retry": ("wlan.fc.retry",),
    "flen": ("frame.len",),
    "wlen": ("wlan.len",),
    "ta": ("wlan.ta",),
    "ra": ("wlan.ra",),
    "bssid": ("wlan.bssid",),
    # The decoded radio fields come first and the raw ones second: a capture
    # tool that carries the 802.11 layer exposes the decoded names, and the
    # radiotap ones are what is left when it does not. The set that exists is
    # discovered from the tool's own field list, never assumed.
    "rate": ("wlan_radio.data_rate", "radiotap.datarate"),
    "mcs": ("wlan_radio.11n.mcs_index", "radiotap.mcs.index"),
    "bw": ("wlan_radio.11n.bandwidth",),
    "sgi": ("wlan_radio.11n.short_gi",),
    "ampdu": ("wlan_radio.aggregate", "wlan_radio.a_mpdu_aggregate_id",
              "wlan_radio.11n.is_aggregated", "wlan.aggregate"),
    "freq": ("wlan_radio.channel", "radiotap.channel.freq"),
    "rssi": ("radiotap.dbm_antsignal",),
}

# The board's measurement phases, as the board image prints them. The parser
# keeps every field verbatim because the phase line is the measurement's
# identity: two windows whose lines differ are not comparable.
PHASE_READY_RE = re.compile(
    r"wlan8189: phase (?P<phase>\d+) ready"
    r"(?: lever=(?P<lever>\d+)"
    r" arm=(?P<arm>[AB])"
    r" pump=(?P<pump>\w+)"
    r" poll_us=(?P<poll_us>\d+)"
    r"(?: data_us=(?P<data_us>\d+))?"
    r" agg=0x(?P<agg>[0-9a-fA-F]+)"
    r" yield=(?P<yield>\d+)"
    r" lps=(?P<lps>-?\d+))?"
)
PHASE_DONE_RE = re.compile(r"wlan8189: phase (?P<phase>\d+) done")
DHCP_BOUND_RE = re.compile(
    r"wlan8189: dhcp bound ip=(?P<ip>\d+\.\d+\.\d+\.\d+)/"
)
JOIN_WAIT_RE = re.compile(r"wlan8189: join waiting for the supplicant")
JOIN_OK_RE = re.compile(r"wlan8189: join-ok bssid=|Key negotiation completed")
CENSUS_RE = re.compile(r"perf census: ")
BW_MODE_RE = re.compile(r"RTW: bw_mode : (?P<bw>\S+), ra_bw_mode : (?P<ra>\S+)")

# The uplink sheet (the reverse direction): the board's own iperf client runs
# each window and the far end is this host, so this tool's part is only to put a
# server up and hand the board this host's address. The lines are the stage's
# own; the skip line is the stage saying why it did not run (no peer configured
# - a boot that did not ask for the measurement is not a failure).
UPLINK_STANDBY_RE = re.compile(r"wlan8189: uplink standby")
UPLINK_READY_RE = re.compile(
    r"wlan8189: uplink phase (?P<phase>\d+) ready .*?arm=(?P<arm>[AB])")
UPLINK_RESULT_RE = re.compile(
    r"wlan8189: uplink phase (?P<phase>\d+) result bytes=(?P<bytes>\d+) "
    r"ms=(?P<ms>\d+) kbit=(?P<kbit>\d+)")
UPLINK_DONE_RE = re.compile(r"wlan8189: uplink phase (?P<phase>\d+) done")
UPLINK_SKIP_RE = re.compile(r"wlan8189: uplink skipped reason=(?P<reason>\S+)")

# The host server's own per-test summary (the receiver-side number the uplink
# figures are quoted from): iperf2 prints one line per completed connection.
UPLINK_SERVER_SUM_RE = re.compile(
    r"\]\s+\d+\.\d+-\s*(?P<secs>[\d.]+)\s*sec\s+"
    r"(?P<val>[\d.]+)\s*(?P<vunit>[KMG])?Bytes\s+"
    r"(?P<rate>[\d.]+)\s*(?P<runit>[KMG])?bits/sec"
)

IPERF_SUM_RE = re.compile(
    r"\]\s+(?P<lo>[\d.]+)-\s*(?P<hi>[\d.]+)\s+sec\s+"
    r"(?P<bytes>[\d.]+)\s+(?P<unit>K|M|G)?Bytes\s+"
    r"(?P<rate>[\d.]+)\s+(?P<rate_unit>[KMG])?bits/sec"
)
IPERF_UDP_RE = re.compile(
    r"\]\s+(?P<lo>[\d.]+)-\s*(?P<hi>[\d.]+)\s+sec\s+.*?(?P<lost>\d+)/\s*(?P<total>\d+)"
    r"\s+\((?P<pct>[\d.]+)%\)"
)

TOOL = "prober"

# How long a run waits for the board to say it is waiting for a join before
# sending the join command anyway. The board's own trigger line is printed once
# and can be behind the reader's starting point. Counted from the board's first
# line, not from this reader's start (a reader started before the boot must not
# fire into a board that is still in U-Boot), and longer than the board's own
# sweep, so an un-anchored attempt lands after the scan rather than inside it.
JOIN_GRACE_S = 45

# How often a join command is retried until the board reports the association
# or the first phase opens. A connect typed before the board's shell is up is
# lost, and one that lands inside the sweep can fail - both leave the board
# still waiting, so the retry is what makes the earlier attempt harmless.
JOIN_RETRY_S = 20


# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------

def out(line: str) -> None:
    print(line, flush=True)


def info(fmt: str, *a) -> None:
    out("%s: %s" % (TOOL, fmt % a if a else fmt))


def warn(fmt: str, *a) -> None:
    print("%s: %s" % (TOOL, fmt % a if a else fmt), file=sys.stderr, flush=True)


def die(fmt: str, *a) -> int:
    warn(fmt, *a)
    return 2


#: options that take a value, so that a positional-word scan can tell a value
#: from a command. Anything not listed is a switch.
FLAGS_WITH_VALUE = {
    "--host", "--iface", "--channel", "--width", "--out", "--duration",
    "--ssid", "--psk", "--bssid", "--addr", "--lease", "--workdir", "--peer",
    "--port", "--seconds", "--tool", "--bandwidth", "--serial-hub", "--phases",
    "--deadline", "--sta", "--field-cache", "--wpa-conf",
}


def positional(argv: list[str]) -> list[str]:
    """The command words in argv, with options and their values removed.

    Options may appear before, after or between the words, so the handlers
    cannot index argv directly; they ask for the words instead.
    """

    words = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a.startswith("--"):
            if "=" not in a and a in FLAGS_WITH_VALUE:
                i += 2
                continue
            i += 1
            continue
        if a.startswith("-") and len(a) > 1:
            i += 1
            continue
        words.append(a)
        i += 1
    return words


def arg_value(argv: list[str], name: str, default: str | None = None) -> str | None:
    """The value of --name (either form), or the default."""

    if name in argv:
        i = argv.index(name)
        if i + 1 >= len(argv):
            return default
        return argv[i + 1]
    for a in argv:
        if a.startswith(name + "="):
            return a.split("=", 1)[1]
    return default


def has(argv: list[str], name: str) -> bool:
    return name in argv


def hist(values: list[float], edges: list[float]) -> dict[str, int]:
    """Buckets by the given upper edges; anything above the last edge lands in
    the overflow bucket, which is reported rather than dropped."""

    h = ["<=%g" % e for e in edges] + [">%g" % edges[-1]]
    out_h = {k: 0 for k in h}
    for v in values:
        placed = False
        for i, e in enumerate(edges):
            if v <= e:
                out_h[h[i]] += 1
                placed = True
                break
        if not placed:
            out_h[h[-1]] += 1
    return out_h


def pct(sorted_vals: list[float], q: float) -> float | None:
    if not sorted_vals:
        return None
    i = min(len(sorted_vals) - 1, int(q * (len(sorted_vals) - 1) + 0.5))
    return sorted_vals[i]


def num(x: str):
    """A capture field as an int when it is one; None when empty (the capture
    said nothing) - never 0 for missing, which would read as a measurement."""

    x = (x or "").strip()
    if not x:
        return None
    try:
        return int(x, 0)
    except ValueError:
        try:
            return int(float(x))
        except ValueError:
            return None


def fnum(x: str):
    x = (x or "").strip()
    if not x:
        return None
    try:
        return float(x)
    except ValueError:
        return None


# --------------------------------------------------------------------------
# the runner: every system command, local or over ssh
# --------------------------------------------------------------------------

class Runner:
    """Executes commands on the measurement host.

    Without --host the commands run here. With --host they run there over ssh,
    so the same script serves both "run it on the box" and "drive the box from
    the development machine". --dry-run prints what would run and executes
    nothing, which is how the command sequence gets reviewed before it touches
    a radio.
    """

    def __init__(self, host: str | None, dry_run: bool, use_sudo: bool):
        self.host = host
        self.dry_run = dry_run
        self.sudo = "sudo -n " if use_sudo else ""

    def _wrap(self, cmd: str, priv: bool) -> str:
        # The tools this drives live in the administrative directories, which a
        # non-interactive login does not put on PATH; export it so the same
        # command works locally or over ssh. It is exported (a statement) and
        # not prefixed (an assignment) because the probes are compound
        # statements, which an assignment prefix cannot introduce. Only the
        # commands that change a radio or open a capture run with privilege,
        # so the read-only probes need no sudo rule at all.
        prefix = "export PATH=/usr/local/sbin:/usr/sbin:/sbin:$PATH; "
        return prefix + (self.sudo if priv else "") + cmd

    def run(self, cmd: str, check: bool = True, timeout: int = 60,
            priv: bool = False) -> tuple[int, str]:
        full = self._wrap(cmd, priv)
        if self.host:
            argv = ["ssh", "-o", "BatchMode=yes", self.host, full]
        else:
            argv = ["sh", "-c", full]
        if self.dry_run:
            info("dry-run: %s", full)
            return 0, ""
        try:
            p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            if check:
                raise RuntimeError("timed out: %s" % full)
            return 124, ""
        text = (p.stdout or "") + (p.stderr or "")
        if check and p.returncode != 0:
            raise RuntimeError("command failed (%d): %s\n%s"
                               % (p.returncode, full, text.strip()))
        return p.returncode, text

    def out(self, cmd: str, **kw) -> str:
        return self.run(cmd, **kw)[1]

    def start(self, cmd: str, priv: bool = False) -> subprocess.Popen | None:
        """Start a command alongside a phase window; dry-run starts nothing.

        The alongside-commands (the loaded-link ping, the mid-window socket
        read) are the tool's own instruments, so they are built and wrapped
        here rather than with a bare Popen at the call site: dry-run must not
        execute them either, and an offline test can then exercise the branch
        without a radio or a board."""

        full = self._wrap(cmd, priv)
        if self.dry_run:
            info("dry-run (bg): %s", full)
            return None
        argv = (["ssh", "-o", "BatchMode=yes", self.host, full] if self.host
                else ["sh", "-c", full])
        return subprocess.Popen(argv, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)

    def ok(self, cmd: str, priv: bool = False) -> bool:
        if self.dry_run:
            info("dry-run: %s", self._wrap(cmd, priv))
            return True
        return self.run(cmd, check=False, priv=priv)[0] == 0


def make_runner(argv: list[str]) -> Runner:
    host = arg_value(argv, "--host")
    use_sudo = not has(argv, "--no-sudo")
    if not host and os.geteuid() == 0:
        use_sudo = False
    return Runner(host, has(argv, "--dry-run"), use_sudo)


# --------------------------------------------------------------------------
# radios: resolve the cards by USB identity, not by interface name
# --------------------------------------------------------------------------

def radio_table(r: Runner) -> list[dict]:
    """One probe, on the measurement host, for every interface that carries a
    wireless device: its name, the USB identity behind it, and its driver.

    This has to run where the interfaces are - reading sysfs from the driving
    machine would describe the wrong host when --host is used.
    """

    probe = (
        'for w in /sys/class/net/*/wireless; do '
        '[ -d "$w" ] || continue; '
        'n=$(basename "$(dirname "$w")"); '
        'dev=$(readlink -f "/sys/class/net/$n/device"); '
        'vid=""; pid=""; '
        'for c in "$dev" "$dev/.." "$dev/../.."; do '
        'if [ -f "$c/idVendor" ]; then vid=$(cat "$c/idVendor"); '
        'pid=$(cat "$c/idProduct"); break; fi; done; '
        'drv=$(basename "$(readlink -f "/sys/class/net/$n/device/driver")" '
        '2>/dev/null); '
        'printf "%s %s:%s %s\\n" "$n" "$vid" "$pid" "$drv"; '
        'done'
    )
    text = r.out(probe, check=False)
    rows = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        usb = parts[1] if parts[1] != ":" else ""
        rows.append({"iface": parts[0], "usb": usb.lower(),
                     "driver": parts[2]})
    return rows


def short_id(text: str) -> str:
    """The USB identity in the form the chipset table uses. Some hosts print
    four digits, some two; normalise both."""

    if not text or ":" not in text:
        return ""
    vid, pid = text.split(":", 1)
    vid = vid.strip().lower().zfill(4)
    pid = pid.strip().lower().zfill(4)
    return "%s:%s" % (vid, pid)


def iw_phy_of(r: Runner, iface: str) -> str:
    _, text = r.run("iw dev %s info" % shlex.quote(iface), check=False)
    m = re.search(r"wiphy (\d+)", text)
    return ("phy%s" % m.group(1)) if m else ""


def parse_phy_caps(text: str) -> dict:
    """What a phy can do, from `iw phy <phy> info`."""

    caps = {"modes": [], "bands": [], "monitor": False, "ap": False, "managed": False}
    in_modes = False
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("Supported interface modes:"):
            in_modes = True
            continue
        if in_modes:
            if s.startswith("*"):
                caps["modes"].append(s.lstrip("* ").strip())
                continue
            # the run of starred lines ends at the first line that is not one
            in_modes = False
        if s.startswith("Band "):
            caps["bands"].append(s.split(":")[0])
    caps["monitor"] = "monitor" in caps["modes"]
    caps["ap"] = "AP" in caps["modes"]
    caps["managed"] = "managed" in caps["modes"]
    return caps


def ensure_outdir(r: Runner, path: str) -> None:
    """Create a capture output directory on the measurement host.

    The capture engine drops privileges after opening the radio, so it writes
    as the original user and cannot reach a directory that user cannot traverse
    - a private home directory is the usual case. The directory is created
    world-traversable so the capture can land in it whichever way it runs.
    """

    parent = os.path.dirname(os.path.abspath(path)) or "."
    if r.dry_run:
        info("dry-run: mkdir -p %s (mode 777 if created)", parent)
        return
    if r.host:
        # Widen only a directory this tool creates. Widening one that already
        # existed would touch a home directory, and a group- or world-writable
        # home is exactly what makes sshd refuse to authenticate against it.
        r.run('if [ ! -d %s ]; then mkdir -p %s && chmod 777 %s; fi'
              % (shlex.quote(parent), shlex.quote(parent), shlex.quote(parent)),
              check=False, priv=True)
        return
    existed = os.path.isdir(parent)
    os.makedirs(parent, exist_ok=True)
    if not existed:
        try:
            os.chmod(parent, 0o777)
        except OSError:
            pass


def capture_workpath(r: Runner, tag: str) -> str:
    """Where the capture engine writes, on the measurement host.

    The engine may run privileged and then drop to the invoking user, so it
    cannot be relied on to write into whatever directory the caller named -
    a private home directory is the usual case, and the failure reads as a
    permission error from inside the capture rather than as a path problem
    here. The capture therefore lands in a directory this tool creates and
    widens, and the artifact is copied to the caller's path afterwards.
    """

    name = "prober-%s-%d-%d" % (tag, os.getpid(), int(time.time()))
    if r.dry_run:
        return "<work>/%s/capture.pcapng" % name
    if r.host:
        # Asked on the host, never assumed: the working directory has to be
        # there, and naming it from here would describe the wrong machine.
        work = r.out("mktemp -d -t %s-XXXXXX" % name, check=False).strip()
        if not work:
            warn("could not create a working directory on %s", r.host)
            work = name
    else:
        base = os.environ.get("PROBER_WORK_BASE") or tempfile.gettempdir()
        work = tempfile.mkdtemp(prefix=name + "-", dir=base)
    # The capture engine opens the radio with privilege and then drops to an
    # unprivileged user, so the directory has to be writable by that user and
    # not only by whoever started the capture.
    r.run("chmod 777 %s" % shlex.quote(work), check=False, priv=True)
    return work + "/capture.pcapng"


def finish_capture(r: Runner, work: str, wanted: str) -> bool:
    """Make the capture readable and put it where the caller asked for it."""

    if r.dry_run:
        info("dry-run: chmod 644 %s; copy to %s", work, wanted)
        return True
    r.run("chmod 644 %s" % shlex.quote(work), check=False, priv=True)
    if not r.host:
        try:
            import shutil
            shutil.move(work, wanted)
            info("capture written to %s", wanted)
            return True
        except OSError as e:
            warn("could not move the capture to %s: %s (it is at %s)",
                 wanted, e, work)
            return False
    try:
        p = subprocess.run(["scp", "-q", "-o", "BatchMode=yes",
                            "%s:%s" % (r.host, work), wanted], timeout=600)
        if p.returncode == 0:
            info("capture written to %s", wanted)
            return True
        warn("could not copy the capture back; read it from %s on %s",
             work, r.host)
    except Exception as e:
        warn("could not copy the capture back (%s); it is at %s on %s",
             e, work, r.host)
    return False


def rfkill_state(r: Runner) -> dict[str, dict]:
    """The radio kill switches, per phy.

    A soft block is the most common reason a monitor interface will not come up
    or take a channel, and from the command that fails it looks exactly like a
    driver failure - so it is read and reported rather than guessed at.
    """

    probe = (
        'for d in /sys/class/rfkill/rfkill*; do '
        '[ -d "$d" ] || continue; '
        'printf "%s %s %s %s\\n" "$(cat "$d/name" 2>/dev/null)" '
        '"$(cat "$d/type" 2>/dev/null)" "$(cat "$d/soft" 2>/dev/null)" '
        '"$(cat "$d/hard" 2>/dev/null)"; '
        'done'
    )
    state: dict[str, dict] = {}
    for line in r.out(probe, check=False).splitlines():
        parts = line.split()
        if len(parts) >= 4:
            state[parts[0]] = {"type": parts[1], "soft": parts[2] == "1",
                               "hard": parts[3] == "1"}
    return state


def prepare_radio(r: Runner, iface: str, phy: str) -> str:
    """Clear what commonly owns or blocks a radio before a capture: the rfkill
    soft block, and a network manager that keeps the interfaces it manages from
    being retyped. Returns why it could not, or an empty string."""

    entry = rfkill_state(r).get(phy)
    if entry and (entry["soft"] or entry["hard"]):
        if entry["hard"]:
            return ("%s is hard-blocked (a physical switch or firmware state); "
                    "no software action clears it" % phy)
        if not r.ok("rfkill unblock wifi", priv=True):
            return ("%s is soft-blocked and unblocking was refused; add rfkill "
                    "to the privileged command list" % phy)
        if rfkill_state(r).get(phy, {}).get("soft", True):
            return "%s is still soft-blocked after unblocking" % phy
        info("radio %s was soft-blocked; unblocked", phy)

    if r.ok("command -v nmcli >/dev/null 2>&1"):
        st, text = r.run("nmcli -t -f DEVICE,STATE dev status", check=False)
        if st == 0:
            for line in text.splitlines():
                parts = line.split(":")
                if parts and parts[0] == iface and len(parts) > 1 \
                        and parts[1].startswith("connected"):
                    if r.ok("nmcli dev set %s managed no" % shlex.quote(iface),
                            priv=True):
                        info("radio %s was held by the network manager; released",
                             iface)
    return ""


def channel_token(value: str) -> str:
    """The channel in the form the control tool takes.

    It wants a channel number while the rest of this tool speaks in MHz; both
    are accepted so a caller who knows the frequency does not have to convert.
    """

    try:
        n = int(value)
    except ValueError:
        return value
    return str(channel_to_number(n)) if n > 1000 else str(n)


def cmd_radios(argv: list[str], r: Runner) -> int:
    """The pre-flight: which cards are here, what can each do, and which roles
    they can carry. Also reports a card that is present on the USB bus but not
    bound to a network interface - the state a dongle is in before its mode
    switch, which otherwise looks like a missing card."""

    if r.dry_run:
        info("dry-run: radios is read-only, nothing to print")
        return 0

    # A card can be on the USB bus without carrying a network interface - the
    # state a dongle sits in before its mode switch, which otherwise reads as a
    # missing card. Collect the identities the bus has, then subtract the ones
    # an interface already claimed.
    on_bus: dict[str, str] = {}
    _, lsusb = r.run("lsusb", check=False)
    for vidpid, chip, _driver, _bands, _gen in CHIPSETS:
        if vidpid in lsusb.lower():
            on_bus[vidpid] = chip

    seen_any = False
    for row in radio_table(r):
        usb = short_id(row["usb"])
        if usb:
            seen_any = True
            on_bus.pop(usb, None)
        chip, bands, gen = "unknown", "unknown", "unknown"
        for vidpid, c, _d, b, g in CHIPSETS:
            if usb == vidpid:
                chip, bands, gen = c, b, g
                break
        phy = iw_phy_of(r, row["iface"])
        caps = (parse_phy_caps(r.out("iw phy %s info" % shlex.quote(phy),
                                     check=False)) if phy else {})
        kill = rfkill_state(r).get(phy, {})
        info("radio iface=%s usb=%s chip=%s driver=%s bands=%s gen=%s "
             "modes=[%s] monitor=%d ap=%d rfkill=%s",
             row["iface"], usb or "-", chip, row["driver"] or "-", bands, gen,
             ",".join(caps.get("modes", [])) or "-",
             1 if caps.get("monitor") else 0,
             1 if caps.get("ap") else 0,
             "hard" if kill.get("hard") else
             ("soft" if kill.get("soft") else "none"))
    for vidpid, chip in sorted(on_bus.items()):
        info("radio usb=%s chip=%s present=yes bound=no "
             "(no network interface carries this identity)", vidpid, chip)
    if not seen_any and not on_bus:
        warn("no known capture card found; attach one and re-run")
        return 1
    return 0


# --------------------------------------------------------------------------
# monitor mode and capture
# --------------------------------------------------------------------------

def cmd_mon(argv: list[str], r: Runner) -> int:
    words = positional(argv)
    action = words[1] if len(words) > 1 else ""
    iface = arg_value(argv, "--iface")
    if action in ("up", "down", "status") and not iface:
        return die("mon %s needs --iface", action)

    if action == "status":
        _, text = r.run("iw dev %s info" % shlex.quote(iface), check=False)
        kind = "-"
        m = re.search(r"type (\S+)", text)
        if m:
            kind = m.group(1)
        info("mon iface=%s type=%s", iface, kind)
        return 0 if kind == "monitor" else 1

    if action == "down":
        r.run("ip link set %s down" % shlex.quote(iface), check=False)
        try:
            r.run("iw dev %s set type managed" % shlex.quote(iface), priv=True)
        except RuntimeError as e:
            return die("could not restore managed mode: %s", e)
        r.run("ip link set %s up" % shlex.quote(iface), check=False)
        info("mon iface=%s restored=managed", iface)
        return 0

    if action != "up":
        return die("usage: prober.py mon up|down|status --iface <if>")

    channel = arg_value(argv, "--channel")
    if not channel:
        return die("mon up needs --channel (MHz, e.g. 2412)")
    width = arg_value(argv, "--width", "HT20")
    pcap = arg_value(argv, "--out")
    duration = arg_value(argv, "--duration", "0")

    # The sequence the reference design fixed: bring the link down, set the
    # monitor type, bring it up, then pin the channel. Setting the type on a
    # running link is what returns EBUSY on some drivers.
    phy = iw_phy_of(r, iface)
    why = prepare_radio(r, iface, phy) if phy else ""
    if why:
        return die("radio not usable: %s", why)
    r.run("ip link set %s down" % shlex.quote(iface), check=False, priv=True)
    try:
        r.run("iw dev %s set type monitor" % shlex.quote(iface), priv=True)
    except RuntimeError as e:
        return die("could not enter monitor mode: %s", e)
    r.run("ip link set %s up" % shlex.quote(iface), check=False, priv=True)
    try:
        r.run("iw dev %s set channel %s %s"
              % (shlex.quote(iface), channel_token(channel), width), priv=True)
    except RuntimeError as e:
        return die("could not pin channel %s %s (EBUSY means another user owns "
                   "the radio): %s", channel, width, e)
    info("mon iface=%s channel=%s width=%s", iface, channel, width)

    if not pcap:
        return 0
    if duration == "0":
        return 0
    work = capture_workpath(r, "mon")
    cmd = ("dumpcap -i %s -a duration:%s -w %s"
           % (shlex.quote(iface), duration, shlex.quote(work)))
    info("mon capture pcap=%s duration=%ss", pcap, duration)
    try:
        r.run(cmd, timeout=int(duration) + 60, priv=True)
    except RuntimeError as e:
        return die("capture failed: %s", e)
    return 0 if finish_capture(r, work, pcap) else 1


# --------------------------------------------------------------------------
# station mode: join an access point the board hosts
# --------------------------------------------------------------------------

def cmd_sta(argv: list[str], r: Runner) -> int:
    words = positional(argv)
    action = words[1] if len(words) > 1 else ""
    iface = arg_value(argv, "--iface")
    if action in ("up", "down") and not iface:
        return die("sta %s needs --iface", action)

    if action == "down":
        r.run("pkill -f 'wpa_supplicant.*%s'" % shlex.quote(iface), check=False, priv=True)
        r.run("ip link set %s down" % shlex.quote(iface), check=False, priv=True)
        r.run("iw dev %s set type managed" % shlex.quote(iface), check=False, priv=True)
        r.run("ip link set %s up" % shlex.quote(iface), check=False, priv=True)
        info("sta iface=%s stopped", iface)
        return 0

    if action != "up":
        return die("usage: prober.py sta up|down --iface <if> --wpa-conf <file>")

    conf = arg_value(argv, "--wpa-conf")
    if not conf:
        return die("sta up needs --wpa-conf <file> (kept outside this repository)")
    r.run("ip link set %s down" % shlex.quote(iface), check=False, priv=True)
    r.run("iw dev %s set type managed" % shlex.quote(iface), check=False, priv=True)
    r.run("ip link set %s up" % shlex.quote(iface), check=False, priv=True)
    r.run("pkill -f 'wpa_supplicant.*%s'" % shlex.quote(iface), check=False, priv=True)
    try:
        r.run("wpa_supplicant -B -i %s -c %s"
              % (shlex.quote(iface), shlex.quote(conf)), priv=True)
    except RuntimeError as e:
        return die("wpa_supplicant did not start: %s", e)
    _, text = r.run("iw dev %s link" % shlex.quote(iface), check=False)
    info("sta iface=%s link: %s", iface, " ".join(text.split()))
    return 0


# --------------------------------------------------------------------------
# access point mode: host an AP the board joins
# --------------------------------------------------------------------------

def hostapd_conf(iface: str, ssid: str, psk: str, channel: str) -> str:
    return (
        "interface=%s\n"
        "driver=nl80211\n"
        "ssid=%s\n"
        "hw_mode=g\n"
        "channel=%s\n"
        "ieee80211n=1\n"
        "wpa=2\n"
        "wpa_key_mgmt=WPA-PSK\n"
        "rsn_pairwise=CCMP\n"
        "wpa_passphrase=%s\n"
    ) % (iface, ssid, channel, psk)


def dnsmasq_conf(iface: str, addr: str, lease: str | None) -> str:
    text = (
        "interface=%s\n"
        "bind-interfaces\n"
        "dhcp-range=%s\n"
        "dhcp-option=3\n"
        "dhcp-option=6\n"
    ) % (iface, addr)
    if lease:
        text += "dhcp-host=%s\n" % lease
    return text


def cmd_ap(argv: list[str], r: Runner) -> int:
    words = positional(argv)
    action = words[1] if len(words) > 1 else ""

    if action == "down":
        r.run("pkill -f 'hostapd .*prober-ap'", check=False, priv=True)
        r.run("pkill -f 'dnsmasq .*prober-ap'", check=False, priv=True)
        info("ap stopped")
        return 0

    if action != "up":
        return die("usage: prober.py ap up|down --iface <if> --ssid <n> --psk <k> "
                   "--channel <mhz>")

    iface = arg_value(argv, "--iface")
    ssid = arg_value(argv, "--ssid")
    psk = arg_value(argv, "--psk")
    channel = arg_value(argv, "--channel")
    addr = arg_value(argv, "--addr", "192.168.90.1/24")
    lease = arg_value(argv, "--lease")
    if not (iface and ssid and psk and channel):
        return die("ap up needs --iface, --ssid, --psk and --channel")

    workdir = arg_value(argv, "--workdir") or tempfile.mkdtemp(prefix="prober-ap.")
    r.run("mkdir -p %s" % shlex.quote(workdir), check=False)
    # The channel number, not the frequency, is what hostapd takes; accept
    # either and convert so the caller can pass what the capture side uses.
    ch_num = channel
    if int(channel) > 1000:
        ch_num = str(channel_to_number(int(channel)))
    ha = os.path.join(workdir, "hostapd.prober-ap.conf")
    dm = os.path.join(workdir, "dnsmasq.prober-ap.conf")
    write_remote(r, ha, hostapd_conf(iface, ssid, psk, ch_num))
    write_remote(r, dm, dnsmasq_conf(iface, addr, lease))
    r.run("ip link set %s down" % shlex.quote(iface), check=False, priv=True)
    r.run("iw dev %s set type __ap" % shlex.quote(iface), check=False, priv=True)
    r.run("ip addr add %s dev %s" % (addr, shlex.quote(iface)), check=False, priv=True)
    r.run("ip link set %s up" % shlex.quote(iface), check=False, priv=True)
    try:
        r.run("hostapd -B -P %s %s" % (shlex.quote(os.path.join(workdir, "hostapd.pid")),
                                       shlex.quote(ha)), priv=True)
    except RuntimeError as e:
        return die("hostapd did not start: %s", e)
    r.run("dnsmasq --conf-file=%s" % shlex.quote(dm), check=False, priv=True)
    info("ap iface=%s ssid=%s channel=%s addr=%s", iface, ssid, ch_num, addr)
    return 0


def write_remote(r: Runner, path: str, text: str) -> None:
    """Write a generated config on the measurement host. The content travels
    over stdin so nothing has to be quoted through two shells."""

    if r.dry_run:
        info("dry-run: write %s (%d bytes)", path, len(text))
        return
    if r.host:
        p = subprocess.run(["ssh", "-o", "BatchMode=yes", r.host,
                            "cat > %s" % shlex.quote(path)],
                           input=text, text=True, capture_output=True)
        if p.returncode != 0:
            raise RuntimeError("could not write %s: %s" % (path, p.stderr))
    else:
        with open(path, "w") as f:
            f.write(text)


def channel_to_number(freq: int) -> int:
    if freq == 2484:
        return 14
    if 2412 <= freq <= 2472:
        return (freq - 2407) // 5
    if 5000 <= freq <= 5900:
        return (freq - 5000) // 5
    return freq


# --------------------------------------------------------------------------
# traffic: the far end the board's servers are measured against
# --------------------------------------------------------------------------

def parse_iperf(text: str) -> dict:
    """The receiver-side number and the summary line, from either client.

    The last matching line is the summary; `bytes` and `rate` are parsed from
    it rather than recomputed, so the tool reports what the client reported.
    A UDP run carries the loss figures too.
    """

    res = {"summary": None, "mbit": None, "mb": None, "udp_loss_pct": None,
           "raw_tail": text.strip().splitlines()[-3:]}
    for line in text.splitlines():
        m = IPERF_SUM_RE.search(line)
        if m:
            scale = {"K": 1e-3, "M": 1.0, "G": 1e3}.get(m.group("unit") or "", 1.0)
            rscale = {"K": 1e-3, "M": 1.0, "G": 1e3}.get(m.group("rate_unit") or "", 1.0)
            res["summary"] = line.strip()
            res["mb"] = round(float(m.group("bytes")) * scale, 3)
            res["mbit"] = round(float(m.group("rate")) * rscale, 3)
        u = IPERF_UDP_RE.search(line)
        if u:
            res["udp_loss_pct"] = float(u.group("pct"))
    return res


def cmd_traffic(argv: list[str], r: Runner) -> int:
    peer = arg_value(argv, "--peer")
    port = arg_value(argv, "--port", "5001")
    seconds = arg_value(argv, "--seconds", "8")
    tool = arg_value(argv, "--tool", "iperf")
    reverse = has(argv, "--reverse")
    server = has(argv, "--server")

    if tool not in ("iperf", "iperf3"):
        return die("--tool must be iperf (the board's lwIP server speaks the "
                   "iperf2 protocol) or iperf3")
    if not server and not peer:
        return die("traffic needs --peer unless --server")

    if server:
        cmd = "%s -s -p %s" % (tool, port)
        if tool == "iperf3" and has(argv, "--one-off"):
            cmd += " -1"
        info("traffic server port=%s tool=%s", port, tool)
        r.run(cmd, check=False, timeout=3600)
        return 0

    if tool == "iperf":
        cmd = "iperf -c %s -p %s -t %s -i 2" % (peer, port, seconds)
        if reverse:
            cmd += " -d"
    else:
        cmd = "iperf3 -c %s -p %s -t %s -i 2" % (peer, port, seconds)
        if reverse:
            cmd += " -R"
    if has(argv, "--udp"):
        cmd += " -u -b %s" % arg_value(argv, "--bandwidth", "60M")
    info("traffic client peer=%s port=%s seconds=%s tool=%s",
         peer, port, seconds, tool)
    try:
        _, text = r.run(cmd, timeout=int(seconds) + 60)
    except RuntimeError as e:
        warn("client failed: %s", e)
        return 1
    res = parse_iperf(text)
    info("traffic mbit=%s mb=%s loss_pct=%s", res["mbit"], res["mb"],
         res["udp_loss_pct"])
    if has(argv, "--json"):
        out(json.dumps(res, indent=2))
    return 0 if res["mbit"] is not None else 1


# --------------------------------------------------------------------------
# the serial hub: the board's console, over plain HTTP
# --------------------------------------------------------------------------

class SerialHub:
    """Reads the board console and can send a command to it.

    The hub is the same one the board runner reads; it exposes a cursor so a
    watcher can start from "now" instead of replaying the buffer, which is what
    keeps a late-started watcher from missing the first phase.
    """

    def __init__(self, base: str, dry_run: bool = False):
        self.base = base.rstrip("/")
        self.dry_run = dry_run

    def read(self, cursor: int) -> tuple[str, int]:
        url = "%s/api/serial/buffer?after=%d" % (self.base, cursor)
        if self.dry_run:
            return "", cursor
        with urllib.request.urlopen(url, timeout=10) as r:
            d = json.load(r)
        data = base64.b64decode(d.get("data", ""))
        return data.decode("utf-8", "replace"), int(d.get("next", cursor))

    def cursor(self) -> int:
        return self.read(0)[1]

    def send(self, line: str) -> bool:
        """One command plus CR. The console wants one command at a time and
        takes a while to emit a prompt, so callers space their sends."""

        if self.dry_run:
            info("dry-run: serial send %r", line)
            return True
        body = json.dumps({"data": line + "\r", "append_newline": False}).encode()
        req = urllib.request.Request(
            "%s/api/serial/send" % self.base, data=body,
            headers={"Content-Type": "application/json"}, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return 200 <= r.status < 300
        except urllib.error.URLError as e:
            warn("serial send failed: %s", e)
            return False


# --------------------------------------------------------------------------
# run: the uplink sheet's host side (server + the address to hand over)
# --------------------------------------------------------------------------

def local_addr_for(r: Runner, target: str) -> str | None:
    """The local IPv4 address that reaches `target`, as the host sees it.

    The uplink stage needs the address the *board* should send to, and that is
    this host's address on the path to the board - not necessarily the first one
    a lookup returns (a host can sit on several networks, and the measurement
    host is expected to be one Linux box among others)."""

    try:
        _, text = r.run("ip -4 route get %s" % shlex.quote(target))
    except RuntimeError:
        text = ""
    m = re.search(r"\bsrc\s+(\d+\.\d+\.\d+\.\d+)", text)
    if m:
        return m.group(1)
    try:
        _, text = r.run("ip -4 -o addr show scope global")
    except RuntimeError:
        return None
    m = re.search(r"inet (\d+\.\d+\.\d+\.\d+)", text)
    return m.group(1) if m else None


def uplink_server_start(r: Runner, work: str) -> bool:
    """Put an iperf2 server on this host for the uplink sheet.

    iperf2, not iperf3: the board's client is lwIP's lwiperf, which speaks the
    iperf2 control protocol. The server's own report is the receiver-side
    number, so its output is kept and fetched with the round's products.

    `work` is the capture's own path (the same value dumpcap is pointed at), so
    the log goes beside it - in the working directory the tool created.

    The restart waits the previous server out instead of sleeping a fixed
    moment: pkill's SIGTERM is asynchronous, and a fixed 0.3 s was measured
    losing the race on the Raspberry Pi (the new iperf died with "Address
    already in use" and the round then found no listener at all). Both waits
    are bounded; the check reads the process name AND the listening port, so
    a stale server cannot mask a failed start."""

    log = "%s/uplink-server.log" % os.path.dirname(work)
    cmd = ("sh -c 'pkill -x iperf 2>/dev/null; "
           "i=0; while [ $i -lt 40 ]; do "
           "pgrep -x iperf >/dev/null 2>&1 || ss -ltn 2>/dev/null | grep -q :5001 || break; "
           "sleep 0.25; i=$((i+1)); done; "
           "nohup iperf -s -p 5001 > %s 2>&1 & "
           "i=0; while [ $i -lt 20 ]; do "
           "pgrep -x iperf >/dev/null 2>&1 && ss -ltn 2>/dev/null | grep -q :5001 && break; "
           "sleep 0.25; i=$((i+1)); done; "
           "pgrep -x iperf >/dev/null 2>&1 && ss -ltn 2>/dev/null | grep -q :5001 "
           "&& echo uplink-server-listening'"
           % shlex.quote(log))
    try:
        _, text = r.run(cmd)
    except RuntimeError as e:
        warn("uplink server did not start: %s", e)
        return False
    if "uplink-server-listening" not in text:
        warn("uplink server not visible after start")
        return False
    info("uplink server listening on 5001 (log kept with the products)")
    return True


def uplink_server_fetch(r: Runner, work: str, outdir: str) -> str:
    log = "%s/uplink-server.log" % os.path.dirname(work)
    try:
        _, text = r.run("cat %s" % shlex.quote(log))
    except RuntimeError as e:
        warn("uplink server log not readable: %s", e)
        return ""
    with open(os.path.join(outdir, "uplink-server.log"), "w") as f:
        f.write(text)
    return text


def uplink_server_stop(r: Runner) -> None:
    try:
        r.run("pkill -x iperf 2>/dev/null; true")
    except RuntimeError:
        pass


def uplink_server_totals(text: str) -> list[dict]:
    """One row per test the server saw - the receiver-side figure.

    iperf2 also prints a `[SUM]` row aggregating connections that overlapped in
    time; that is not a test of its own, so it is skipped here."""

    rows = []
    for line in text.splitlines():
        if "[SUM]" in line:
            continue
        m = UPLINK_SERVER_SUM_RE.search(line)
        if not m:
            continue
        vmult = {"K": 1e3, "M": 1e6, "G": 1e9, None: 1.0}[m.group("vunit")]
        rmult = {"K": 1e3, "M": 1e6, "G": 1e9, None: 1.0}[m.group("runit")]
        rows.append({
            "seconds": float(m.group("secs")),
            "bytes": round(float(m.group("val")) * vmult),
            "kbit": round(float(m.group("rate")) * rmult / 1e3),
        })
    return rows


# --------------------------------------------------------------------------
# run: one measurement round against the board's phase sheet
# --------------------------------------------------------------------------

def cmd_run(argv: list[str], r: Runner) -> int:
    hub_url = arg_value(argv, "--serial-hub")
    outdir = arg_value(argv, "--out")
    if not (hub_url and outdir):
        return die("run needs --serial-hub <url> and --out <dir>")

    phases_want = int(arg_value(argv, "--phases", "4"))
    uplink_phases_want = int(arg_value(argv, "--uplink-phases", "4"))
    seconds = int(arg_value(argv, "--seconds", "8"))
    iface = arg_value(argv, "--iface")
    channel = arg_value(argv, "--channel")
    width = arg_value(argv, "--width", "HT20")
    wpa_conf = arg_value(argv, "--wpa-conf")
    peer_opt = arg_value(argv, "--peer")
    udp = has(argv, "--udp")
    ping_during = has(argv, "--ping-during")
    ss_during = has(argv, "--ss-during")
    window = arg_value(argv, "--window")
    if window is not None and not re.match(r"^\d+[KMkm]?$", window):
        return die("--window wants a size like 256K, got %r" % window)
    try:
        phase_actions = parse_phase_actions(arg_value(argv, "--phase-actions", ""))
    except ValueError as e:
        return die("%s" % e)
    if len(phase_actions) > phases_want:
        return die("--phase-actions names %d phases but --phases is %d"
                   % (len(phase_actions), phases_want))
    ssid = arg_value(argv, "--ssid")
    psk = arg_value(argv, "--psk")
    bssid = arg_value(argv, "--bssid")
    deadline_s = int(arg_value(argv, "--deadline", "900"))

    pcap = os.path.join(outdir, "capture.pcapng")
    work = capture_workpath(r, "run")
    hub = SerialHub(hub_url, r.dry_run)

    # The capture is started first so every phase window is inside it; the
    # phase boundaries recorded below then slice it by time.
    if iface and channel:
        rc = cmd_mon(["mon", "up", "--iface", iface, "--channel", channel,
                      "--width", width], r)
        if rc != 0:
            return rc
        cmd = ("dumpcap -i %s -a duration:%d -w %s"
               % (shlex.quote(iface), deadline_s, shlex.quote(work)))
        info("run capture pcap=%s deadline=%ss", pcap, deadline_s)
        if not r.dry_run:
            wrapped = r._wrap(cmd, priv=True)
            dumpcap = subprocess.Popen(
                (["ssh", "-o", "BatchMode=yes", r.host, wrapped] if r.host
                 else ["sh", "-c", wrapped]),
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            info("dry-run: %s", cmd)
            dumpcap = None
    else:
        dumpcap = None
        info("run capture skipped (no --iface/--channel): phases still measured")

    try:
        return _run_phases(r, hub, outdir, phases_want, seconds, deadline_s,
                           peer_opt, wpa_conf, ssid, psk, bssid, work,
                           uplink_phases_want, udp, ping_during,
                           phase_actions=phase_actions, window=window,
                           ss_during=ss_during)
    finally:
        if dumpcap is not None:
            # dumpcap ends on its own duration; if the round finished early,
            # stop it so the file is closed and readable.
            time.sleep(1.0)
            try:
                dumpcap.terminate()
                dumpcap.wait(timeout=15)
            except Exception:
                pass
        if dumpcap is not None:
            finish_capture(r, work, pcap)


def _run_phases(r: Runner, hub: SerialHub, outdir: str, phases_want: int,
                seconds: int, deadline_s: int, peer_opt: str | None,
                wpa_conf: str | None, ssid: str | None,
                psk: str | None, bssid: str | None = None,
                work: str = "", uplink_phases_want: int = 4,
                udp: bool = False, ping_during: bool = False,
                phase_actions: list[dict] | None = None,
                window: str | None = None,
                ss_during: bool = False) -> int:
    cursor = hub.cursor()
    info("run serial cursor=%d", cursor)
    peer = peer_opt
    phases: dict[int, dict] = {}
    pending: dict[int, str] = {}

    # The uplink server is brought up NOW, not when the stage announces itself:
    # the board's first uplink window opens ~2 s after the standby line, and a
    # server started at that moment was measured losing that window (the board
    # connected into a port with no listener and its first window counted
    # 0.6 s). One server serves the whole round - it is idle until the board
    # connects - and the standby line only re-checks it.
    uplink_server_up = False
    if uplink_phases_want > 0 and work:
        uplink_server_up = uplink_server_start(r, work)
    # the join's own state: the command is sent when the board says it is
    # waiting, and retried on a period until the board reports the association
    # (or the first phase opens). Two failure modes make the retry load-bearing:
    # a command typed before the board's shell is up is simply lost, and one
    # sent too early can start the connect while the sweep is still in flight
    # (the dwell drags the replies off-channel). Both end the same way - the
    # board still waiting - so the retry is what turns a lost attempt into a
    # later good one.
    join_ok = False
    join_wait_seen = False
    join_last_send = -1.0e9
    join_deadline: float | None = None
    started = time.time()
    phase_open: int | None = None
    t_open = 0.0
    seen_bw: list[str] = []
    # the uplink sheet's state: the stage announces itself with its standby
    # line, this side answers with a server and this host's address, and the
    # stage's own ready/result/done lines then read like the downlink's
    uplink_seen = False
    uplink_done = False
    uplink_peer_sent = False
    uplink_peer_tried = False
    uplink_addr: str | None = None
    uplink_phases: dict[int, dict] = {}
    uplink_open: int | None = None
    uplink_skip: str | None = None

    while time.time() - started < deadline_s and (
            len(phases) < phases_want or
            (uplink_seen and not uplink_done and not uplink_skip)):
        text, cursor = hub.read(cursor)
        # The join command, sent when the anchor is seen and retried on a period
        # otherwise. The grace is counted from the first line of the board's own
        # suite (not from this reader's start, which can be minutes earlier - a
        # reader started before the boot must not fire into a board that is
        # still in U-Boot), and it is longer than the scan, so an un-anchored
        # attempt lands after the sweep rather than inside it.
        if (ssid and psk and not join_ok and not phases
                and join_deadline is not None
                and (join_wait_seen or time.time() > join_deadline)
                and time.time() - join_last_send >= JOIN_RETRY_S):
            info("run sending join for ssid=%s (anchor=%s)", ssid, join_wait_seen)
            # the console face takes the access point's address too when it is
            # given, which is what keeps a same-name neighbour from being joined
            cmd = "wpa connect %s %s" % (ssid, psk)
            if bssid:
                cmd += " " + bssid
            if hub.send(cmd):
                join_last_send = time.time()
        if not text:
            time.sleep(0.5)
            continue
        with open(os.path.join(outdir, "serial.log"), "a") as f:
            f.write(text)
        for line in text.splitlines():
            if "wlan8189:" in line:
                # the board is running its suite: arm the join grace (once),
                # which is what lets an un-anchored reader still send the join
                if join_deadline is None:
                    join_deadline = time.time() + JOIN_GRACE_S
            if JOIN_OK_RE.search(line):
                join_ok = True
            if JOIN_WAIT_RE.search(line):
                join_wait_seen = True

            m = DHCP_BOUND_RE.search(line)
            if m and peer is None:
                peer = m.group("ip")
                info("run peer from dhcp lease: %s", peer)

            # The uplink stage's handover: once the board has announced the
            # stage and its own address is known (the lease above, or --peer),
            # this host's reachable address goes to the board's console and the
            # board's own client drives the windows from there.
            if uplink_seen and peer and not uplink_peer_tried:
                uplink_peer_tried = True
                addr = local_addr_for(r, peer)
                if addr and hub.send("wlan8189 uplink %s" % addr):
                    uplink_addr = addr
                    uplink_peer_sent = True
                    info("run uplink peer sent: the board sends to %s", addr)
                else:
                    warn("uplink peer not sent: no local address reaching %s",
                         peer)

            m = BW_MODE_RE.search(line)
            if m:
                seen_bw.append(m.group("bw"))

            if UPLINK_STANDBY_RE.search(line):
                uplink_seen = True
                if work and not uplink_server_up:
                    info("run uplink stage standby: bringing the host server up")
                    uplink_server_up = uplink_server_start(r, work)
                elif uplink_server_up:
                    info("run uplink stage standby: the host server is already up")
                else:
                    warn("uplink stage seen but no work directory; the server "
                         "was not started")
                continue

            m = PHASE_READY_RE.search(line)
            if m:
                n = int(m.group("phase"))
                phase_open = n
                t_open = time.time()
                pending[n] = line.strip()
                info("run phase %d open at %.3f", n, t_open)
                if peer is None:
                    warn("phase %d opened before a peer address was known; "
                         "pass --peer to measure without waiting for the lease", n)
                    continue
                act = (phase_actions[n - 1]
                       if phase_actions and n <= len(phase_actions)
                       else {"udp": udp, "window": window})
                res = measure_phase(r, outdir, n, peer, seconds, act["udp"],
                                    ping_during=ping_during,
                                    window=act.get("window"),
                                    ss_during=ss_during)
                pending[n] = pending.get(n, "") + "\n" + json.dumps(res)
                continue

            m = PHASE_DONE_RE.search(line)
            if m:
                n = int(m.group("phase"))
                if n in pending:
                    phases[n] = {
                        "phase_line": pending[n].split("\n")[0],
                        "client": json.loads(pending[n].split("\n", 1)[1])
                        if "\n" in pending[n] else None,
                        "t_open": t_open,
                        "t_close": time.time(),
                        "census": [],
                    }
                    info("run phase %d closed", n)
                phase_open = None

            # the uplink sheet's own lines: the board drives the windows, so
            # these are read, not driven - the ready line carries the arm, the
            # result line what the board's client moved, and the host server's
            # log (fetched at the end) carries the receiver-side figure
            m = UPLINK_READY_RE.search(line)
            if m:
                n = int(m.group("phase"))
                uplink_open = n
                uplink_phases[n] = {
                    "phase_line": line.strip(),
                    "arm": m.group("arm"),
                    "t_open": time.time(),
                    "census": [],
                }
                info("run uplink phase %d open arm=%s", n, m.group("arm"))
                continue

            m = UPLINK_RESULT_RE.search(line)
            if m:
                n = int(m.group("phase"))
                if n in uplink_phases:
                    uplink_phases[n]["result"] = {
                        "bytes": int(m.group("bytes")),
                        "ms": int(m.group("ms")),
                        "kbit": int(m.group("kbit")),
                    }
                    info("run uplink phase %d result bytes=%s kbit=%s",
                         n, m.group("bytes"), m.group("kbit"))
                continue

            m = UPLINK_DONE_RE.search(line)
            if m:
                n = int(m.group("phase"))
                if n in uplink_phases:
                    uplink_phases[n]["t_close"] = time.time()
                uplink_open = None
                if n >= uplink_phases_want:
                    uplink_done = True
                continue

            m = UPLINK_SKIP_RE.search(line)
            if m:
                uplink_skip = m.group("reason")
                uplink_done = True
                info("run uplink stage skipped by the board: %s", uplink_skip)
                continue

            if CENSUS_RE.search(line):
                if uplink_open is not None and uplink_open in uplink_phases:
                    uplink_phases[uplink_open]["census"].append(line.strip())
                elif phases:
                    last = max(phases)
                    phases[last]["census"].append(line.strip())

    server_log = uplink_server_fetch(r, work, outdir) if uplink_seen else ""
    if uplink_seen:
        uplink_server_stop(r)
    result = {
        "phases": [phases[k] for k in sorted(phases)],
        "phases_wanted": phases_want,
        "bandwidth_mode_seen": sorted(set(seen_bw)),
        "peer": peer,
        "host": r.host or "local",
        "uplink": {
            "stage_seen": uplink_seen,
            "skipped": uplink_skip,
            "host_addr": uplink_addr,
            "phases": [uplink_phases[k] for k in sorted(uplink_phases)],
            "server": uplink_server_totals(server_log),
            "server_log": "uplink-server.log" if server_log else None,
            "note": ("the board's own client drives the uplink windows; the "
                     "server rows are the receiver-side figures, the phase "
                     "result lines are the sender's own account"),
        },
        "note": ("the client window is the same 8 s inside the board's 20 s "
                 "window the earlier rounds used, so the numbers stay "
                 "comparable"),
    }
    with open(os.path.join(outdir, "result.json"), "w") as f:
        json.dump(result, f, indent=2)
    for p in result["phases"]:
        c = p.get("client") or {}
        out("%s: phase %s mbit=%s" % (
            TOOL, p["phase_line"].split("ready")[0].split()[-1], c.get("mbit")))
    for p in result["uplink"]["phases"]:
        res = p.get("result") or {}
        out("%s: uplink phase %s arm=%s kbit=%s" % (
            TOOL, p["phase_line"].split("phase")[1].split()[0],
            p.get("arm"), res.get("kbit")))
    for row in result["uplink"]["server"]:
        out("%s: uplink server row seconds=%.1f kbit=%s" % (
            TOOL, row["seconds"], row["kbit"]))
    ok = len(phases) >= phases_want
    if uplink_seen and not uplink_skip:
        ok = ok and len(uplink_phases) >= uplink_phases_want and all(
            "result" in p for p in uplink_phases.values())
    info("run complete phases=%d/%d uplink=%d/%d result=%s", len(phases),
         phases_want, len(uplink_phases),
         uplink_phases_want if uplink_seen else 0,
         os.path.join(outdir, "result.json"))
    return 0 if ok else 1


PHASE_ACTION_RE = re.compile(r"^(tcp|udp)(?:-w(\d+[KMkm]?))?$")


def parse_phase_actions(spec: str) -> list[dict]:
    """Per-phase client actions, one token per phase, left to right.

    A token is `tcp` or `udp`, and a TCP token may pin the socket buffer it
    asks the sender for (`tcp-w256K`). That pin exists because the sender's own
    window is one of the limits a bulk transfer runs into, and the run-level
    flags cannot vary it inside one boot - an A/B/A/B needs the variation per
    phase. Phases beyond the spec keep the run-level defaults."""

    acts = []
    for tok in [t.strip() for t in spec.split(",") if t.strip()]:
        m = PHASE_ACTION_RE.match(tok)
        if not m:
            raise ValueError("bad phase action %r (want tcp, udp or tcp-w<size>)"
                             % tok)
        acts.append({"udp": m.group(1) == "udp", "window": m.group(2)})
    return acts


def phase_client_cmd(peer: str, seconds: int, udp: bool, udp_port: int,
                     udp_bw: str, window: str | None) -> str:
    """The iperf command one phase window is measured on."""

    if udp:
        return "iperf -c %s -p %d -t %d -i 2 -u -b %s" % (peer, udp_port,
                                                          seconds, udp_bw)
    cmd = "iperf -c %s -p 5001 -t %d -i 2" % (peer, seconds)
    if window:
        cmd += " -w %s" % window
    return cmd


def measure_phase(r: Runner, outdir: str, n: int, peer: str, seconds: int,
                  udp: bool = False, udp_port: int = 5002,
                  udp_bw: str = "60M", ping_during: bool = False,
                  window: str | None = None, ss_during: bool = False) -> dict:
    """Drive one phase window and collect what it moved.

    The default action is the TCP client the rounds have always used. Three
    optional actions answer the question the TCP number alone cannot: `udp`
    blasts datagrams at the board's own sink (no acknowledgement clock, so the
    receive path's own ceiling), `ping_during` runs a ping alongside the
    transfer (the round-trip time while the link is loaded, which is what any
    window-versus-flight-size arithmetic needs), and `ss_during` reads the
    sender's own socket mid-window (congestion window, smoothed round trip,
    unacknowledged bytes, and the send buffer it was given - the four numbers a
    "which window binds" question is decided on)."""

    info("run phase %d: client vs %s for %ss udp=%s window=%s ping=%s ss=%s",
         n, peer, seconds, udp, window, ping_during, ss_during)
    ping = None
    if ping_during:
        # alongside the transfer; its summary lands in phaseN-ping.txt
        pcmd = "ping -c %d -i 0.2 %s" % (max(1, seconds * 5), peer)
        ping = r.start(pcmd)
    ss = None
    if ss_during and not udp:
        # mid-window, so the transfer is up and steady when it is read
        scmd = "sleep %d; ss -tin dst %s" % (max(1, seconds // 2), peer)
        ss = r.start(scmd)
    cmd = phase_client_cmd(peer, seconds, udp, udp_port, udp_bw, window)
    try:
        _, text = r.run(cmd, timeout=seconds + 60)
    except RuntimeError as e:
        warn("phase %d client failed: %s", n, e)
        if ping is not None:
            ping.terminate()
        if ss is not None:
            ss.terminate()
        return {"error": str(e)}
    with open(os.path.join(outdir, "phase%d.txt" % n), "w") as f:
        f.write(text)
    if ping is not None:
        try:
            out_text, _ = ping.communicate(timeout=seconds + 60)
        except Exception:
            ping.terminate()
            out_text = ""
        with open(os.path.join(outdir, "phase%d-ping.txt" % n), "w") as f:
            f.write(out_text)
    if ss is not None:
        try:
            out_text, _ = ss.communicate(timeout=seconds + 60)
        except Exception:
            ss.terminate()
            out_text = ""
        with open(os.path.join(outdir, "phase%d-ss.txt" % n), "w") as f:
            f.write(out_text)
    return parse_iperf(text)


# --------------------------------------------------------------------------
# analyze: what the air says, per direction
# --------------------------------------------------------------------------

def fields_available(r: Runner, cache: str | None) -> dict[str, str]:
    """Which of the wanted capture fields this tool actually has, from its own
    field list. A name that is absent is reported as absent rather than read as
    zero."""

    if cache and os.path.exists(cache):
        with open(cache) as f:
            known = set(f.read().split())
    else:
        text = r.out("tshark -G fields", check=False, timeout=120)
        known = set()
        for line in text.splitlines():
            parts = line.split("\t")
            if len(parts) >= 3:
                known.add(parts[2])
        if cache and not r.dry_run:
            with open(cache, "w") as f:
                f.write("\n".join(sorted(known)))
    picked = {}
    for concept, names in FIELD_CANDIDATES.items():
        for n in names:
            if n in known:
                picked[concept] = n
                break
    return picked


def cmd_analyze(argv: list[str], r: Runner) -> int:
    words = positional(argv)
    if len(words) < 2:
        return die("usage: prober.py analyze <pcap> [--sta <mac>] [--out <json>]")
    pcap = words[1]
    sta = (arg_value(argv, "--sta") or "").lower()
    out_path = arg_value(argv, "--out")

    # The capture is read where the analysis runs, which is the measurement
    # host when one is named. A path that only exists on the driving machine
    # would read as an empty capture, so it is moved over first (or refused
    # loudly) rather than reported as no frames.
    if r.host and not r.ok("test -r %s" % shlex.quote(pcap)):
        if os.path.exists(pcap):
            work = capture_workpath(r, "analyze")
            r.run("mkdir -p %s && chmod 777 %s"
                  % (shlex.quote(os.path.dirname(work)),
                     shlex.quote(os.path.dirname(work))),
                  check=False, priv=True)
            if not r.dry_run:
                cp = subprocess.run(["scp", "-q", "-o", "BatchMode=yes", pcap,
                                     "%s:%s" % (r.host, work)], timeout=600)
                if cp.returncode != 0:
                    return die("could not place the capture on %s for analysis "
                               "(scp returned %d)", r.host, cp.returncode)
                r.run("chmod 644 %s" % shlex.quote(work), check=False, priv=True)
            pcap = work
            info("analyze copied the capture to %s on %s", pcap, r.host)
        else:
            return die("%s is readable on neither this machine nor %s", pcap,
                       r.host)
    elif not r.host and not os.path.exists(pcap):
        return die("%s does not exist", pcap)

    cache = arg_value(argv, "--field-cache")
    fields = fields_available(r, cache)
    if "time" not in fields or "subtype" not in fields:
        return die("the capture tool does not expose the fields the analysis "
                   "needs (time/subtype); nothing can be concluded")
    wanted = ["time", "subtype", "retry", "flen", "ta", "ra", "rate", "mcs",
              "bw", "sgi", "freq", "rssi", "ampdu"]
    cols = [c for c in wanted if c in fields]
    cmd = ("tshark -r %s -T fields -E separator=/t -E occurrence=f %s"
           % (shlex.quote(pcap), " ".join("-e " + fields[c] for c in cols)))
    rc, text = r.run(cmd, check=False, timeout=600)
    if not text.strip():
        return die("no frames read from %s (the capture tool returned %d; an "
                   "unreadable or empty file reports as no frames otherwise)",
                   pcap, rc)

    frames = []
    for line in text.splitlines():
        v = line.split("\t")
        row = dict(zip(cols, v))
        t = fnum(row.get("time", ""))
        st = num(row.get("subtype", ""))
        if t is None or st is None:
            continue
        frames.append({
            "t": t, "st": st,
            "retry": num(row.get("retry", "")) or 0,
            "len": num(row.get("flen", "")) or 0,
            "ta": (row.get("ta", "") or "").lower(),
            "ra": (row.get("ra", "") or "").lower(),
            "rate": fnum(row.get("rate", "")),
            "mcs": num(row.get("mcs", "")),
            "bw": num(row.get("bw", "")),
            "sgi": num(row.get("sgi", "")),
            "ampdu": num(row.get("ampdu", "")),
        })
    summary = analyze_frames(frames, sta, fields_available=fields)
    if out_path:
        with open(out_path, "w") as f:
            json.dump(summary, f, indent=2)
    for line in summary["verdicts"]:
        out("%s: %s" % (TOOL, line))
    for line in summary["unavailable"]:
        warn("%s", line)
    info("analyze frames=%d pcap=%s", len(frames), pcap)
    return 0


def analyze_frames(frames: list[dict], sta: str, fields_available: dict) -> dict:
    """Everything the next tuning round needs, per direction.

    The honesty rules this follows: a retry here is the share of frames the
    capture saw with the retry bit set, not a loss rate; a missing field is
    reported, never counted as zero; a count of zero acknowledgements does not
    mean the peer did not acknowledge, only that this capture did not carry
    them; and the absence of an aggregation marker does not prove aggregation
    was absent.
    """

    if not sta:
        sta = infer_sta(frames)
    sta = (sta or "").lower()
    counts: dict[str, int] = {}
    for f in frames:
        name = SUBTYPE_NAMES.get(f["st"], "0x%02x" % f["st"])
        counts[name] = counts.get(name, 0) + 1

    def direction(f: dict) -> str:
        if not sta:
            return "unknown"
        ta = (f["ta"] or "").lower()
        ra = (f["ra"] or "").lower()
        if ta == sta:
            return "from_sta"
        if ra == sta:
            return "to_sta"
        return "other"

    per_dir: dict[str, dict] = {}
    for d in ("from_sta", "to_sta", "other", "unknown"):
        sub = [f for f in frames if direction(f) == d]
        if not sub:
            continue
        retry = sum(1 for f in sub if f["retry"])
        data = [f for f in sub if f["st"] in DATA]
        per_dir[d] = {
            "frames": len(sub),
            "data": len(data),
            "ack": sum(1 for f in sub if f["st"] == 0x1D),
            "blockack": sum(1 for f in sub if f["st"] == 0x19),
            "blockack_req": sum(1 for f in sub if f["st"] == 0x18),
            "retry": retry,
            "retry_share_pct": round(100.0 * retry / len(sub), 1) if sub else None,
            "aggregated": sum(1 for f in sub if f.get("ampdu") not in (None, 0)),
            "aggregated_share_pct": round(
                100.0 * sum(1 for f in sub if f.get("ampdu") not in (None, 0))
                / len(sub), 1) if sub else None,
            "bw": hist([f["bw"] for f in sub if f.get("bw") is not None], [0, 1, 2, 3]),
            "sgi": sum(1 for f in sub if f.get("sgi") == 1),
            "data_len_bytes": hist([f["len"] for f in data],
                                   [64, 256, 512, 1024, 1536, 2048, 4096, 8192, 16384])
            if data else {},
            "mcs": hist([f["mcs"] for f in sub if f["mcs"] is not None],
                        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]),
        }

    # The ACK turn-around: how long after a data frame the acknowledgement for
    # it appeared, and how far apart consecutive acknowledgements were. Both
    # answer "is the returning path immediate or batched".
    ack_deltas: list[float] = []
    ack_gaps: list[float] = []
    last_ack = None
    for f in frames:
        if f["st"] == 0x1D:
            if last_ack is not None:
                ack_gaps.append((f["t"] - last_ack) * 1000.0)
            last_ack = f["t"]
    for i, f in enumerate(frames):
        if f["st"] not in DATA or not f["ta"]:
            continue
        for g in frames[i + 1:i + 6]:
            # the acknowledgement for this frame is the one addressed to its
            # sender - anything else on the air belongs to another exchange
            if g["st"] == 0x1D and g["ra"] == f["ta"]:
                ack_deltas.append((g["t"] - f["t"]) * 1e6)
                break

    # Inter-arrival of the downlink data frames: the burst period the earlier
    # rounds saw in the board's own counters should show up here too.
    down = [f["t"] for f in frames
            if f["st"] in DATA and (not sta or f["ra"] == sta)]
    gaps_ms = [ (b - a) * 1000.0 for a, b in zip(down, down[1:]) ]

    unavailable = []
    if not fields_available.get("bw"):
        unavailable.append("bandwidth: not available in this capture's field set")
    if not fields_available.get("ampdu"):
        unavailable.append("aggregation marker: not available in this capture's "
                           "field set, so aggregation is reported only through "
                           "the block-ack counts")
    if not fields_available.get("mcs"):
        unavailable.append("mcs: not available; rates come from the rate field")
    if not sta:
        unavailable.append("station address: not given and not inferable, so "
                           "the directions are not split")

    named = ", ".join("%s:%s" % (k, v)
                      for k, v in sorted(counts.items(), key=lambda kv: -kv[1])[:6])
    verdicts = ["air: frames=%d top=[%s]" % (len(frames), named)]
    talkers = top_talkers(frames)
    if talkers:
        verdicts.append("air talkers: "
                        + " ".join("%s=%d" % (t["addr"], t["frames"])
                                   for t in talkers))
    for d in ("from_sta", "to_sta"):
        v = per_dir.get(d)
        if not v:
            continue
        who = "board" if d == "from_sta" else "peer"
        verdicts.append(
            "dp %s: frames=%d data=%d ack=%d blockack=%d/%d retry=%d(%s%%) "
            "aggregated=%s(%s%%)"
            % (who, v["frames"], v["data"], v["ack"], v["blockack"],
               v["blockack_req"], v["retry"], v["retry_share_pct"],
               v["aggregated"], v["aggregated_share_pct"]))
    if ack_deltas:
        s = sorted(ack_deltas)
        verdicts.append("ack-turn: n=%d p50=%.1fus p90=%.1fus max=%.1fus"
                        % (len(s), pct(s, 0.5) or 0, pct(s, 0.9) or 0, s[-1]))
    if ack_gaps:
        s = sorted(ack_gaps)
        verdicts.append("ack-gap: n=%d p50=%.3fms p90=%.3fms max=%.3fms"
                        % (len(s), pct(s, 0.5) or 0, pct(s, 0.9) or 0, s[-1]))
    if gaps_ms:
        s = sorted(gaps_ms)
        verdicts.append("down-arrival: n=%d p50=%.3fms p90=%.3fms"
                        % (len(s), pct(s, 0.5) or 0, pct(s, 0.9) or 0))

    return {
        "sta": sta or None,
        "frames": len(frames),
        "by_subtype": counts,
        "per_direction": per_dir,
        "ack_turn_us": {"n": len(ack_deltas),
                        "p50": pct(sorted(ack_deltas), 0.5),
                        "p90": pct(sorted(ack_deltas), 0.9)},
        "ack_gap_ms": {"n": len(ack_gaps),
                       "p50": pct(sorted(ack_gaps), 0.5),
                       "p90": pct(sorted(ack_gaps), 0.9)},
        "down_arrival_ms": {"n": len(gaps_ms),
                            "hist": hist(gaps_ms, [0.5, 1, 2, 4, 8, 16, 32])
                            if gaps_ms else {}},
        "air": {"frames": len(frames),
                "by_subtype": counts,
                "talkers": talkers},
        "fields_available": fields_available,
        "verdicts": verdicts,
        "unavailable": unavailable,
        "honesty": ("retry is the observed share of frames carrying the retry "
                    "bit, not a loss rate; a zero acknowledgement count means "
                    "this capture carried none, not that none was sent; the "
                    "absence of an aggregation marker does not prove "
                    "aggregation was absent"),
        "channel_pinned": channel_pinned(frames),
    }


def channel_pinned(frames: list[dict], want_bssid: str = "") -> str | None:
    """The branch the reference design fixed: frames captured but no beacon of
    the target is the signature of a channel that was never pinned, and must
    not be judged as empty air."""

    beacons = [f for f in frames if f["st"] == 0x08]
    if not frames:
        return "no-frames"
    if not beacons:
        return "channel-not-pinned"
    return "ok"


def infer_sta(frames: list[dict]) -> str:
    """The station behind the traffic, from the pair evidence.

    A station is the address that both sends data and is the addressee of the
    acknowledgements for it; an access point does the opposite (it receives data
    and sends the acknowledgements). Scoring both roles and excluding whoever
    beacons names the station without being told, and the answer is reported so
    it can be checked against the address the board prints.
    """

    sent: dict[str, int] = {}
    acked: dict[str, int] = {}
    beacon_src: dict[str, int] = {}
    for f in frames:
        if f["st"] == 0x08 and f["ta"]:
            beacon_src[f["ta"]] = beacon_src.get(f["ta"], 0) + 1
        elif f["st"] in DATA and f["ta"]:
            sent[f["ta"]] = sent.get(f["ta"], 0) + 1
        elif f["st"] in (0x1D, 0x19) and f["ra"]:
            acked[f["ra"]] = acked.get(f["ra"], 0) + 1
    score = {}
    for addr in set(sent) | set(acked):
        if addr in beacon_src or addr in ("ff:ff:ff:ff:ff:ff", ""):
            continue
        score[addr] = sent.get(addr, 0) + acked.get(addr, 0)
    if not score:
        return ""
    return max(score.items(), key=lambda kv: kv[1])[0]


def top_talkers(frames: list[dict], limit: int = 6) -> list[dict]:
    """Who is on the air, by transmitted frames. This is how a caller finds the
    board's address when the capture was taken before the board was identified."""

    counts: dict[str, int] = {}
    for f in frames:
        if f["ta"]:
            counts[f["ta"]] = counts.get(f["ta"], 0) + 1
    ranked = sorted(counts.items(), key=lambda kv: kv[1], reverse=True)[:limit]
    return [{"addr": a, "frames": n} for a, n in ranked]


# --------------------------------------------------------------------------
# selftest: the red controls, offline
# --------------------------------------------------------------------------

def selftest() -> int:
    fails = []

    def check(name: str, cond: bool) -> None:
        out("%s: selftest %-28s %s" % (TOOL, name, "ok" if cond else "FAIL"))
        if not cond:
            fails.append(name)

    # the phase-line parser: the full line, the minimal line, and a line with a
    # missing field must not be read as a valid one with defaults
    full = ("[  55.145] wlan8189: phase 2 ready lever=3 arm=B pump=adaptive "
            "poll_us=100 agg=0x010f yield=0 lps=2")
    m = PHASE_READY_RE.search(full)
    check("phase-parse-full", bool(m) and m.group("phase") == "2"
          and m.group("arm") == "B" and m.group("agg") == "010f")
    minimal = "[  34.447] wlan8189: phase 1 ready"
    m2 = PHASE_READY_RE.search(minimal)
    check("phase-parse-minimal", bool(m2) and m2.group("arm") is None)
    broken = "[  55.145] wlan8189: phase ready arm=B"
    check("phase-parse-rejects-broken", PHASE_READY_RE.search(broken) is None)

    # the phase line's data-step field: parsed when present, absent (not
    # defaulted) on an older image's line
    pre = ("[  55.145] wlan8189: phase 2 ready lever=3 arm=B pump=adaptive "
           "poll_us=100 agg=0x010f yield=0 lps=2")
    m = PHASE_READY_RE.search(pre)
    check("phase-parse-pre-datastep", bool(m) and m.group("arm") == "B"
          and m.group("data_us") is None)
    new = ("[  55.145] wlan8189: phase 2 ready lever=5 arm=B pump=adaptive "
           "poll_us=100 data_us=5 agg=0x200f yield=0 lps=2")
    m = PHASE_READY_RE.search(new)
    check("phase-parse-datastep", bool(m) and m.group("data_us") == "5"
          and m.group("agg") == "200f")

    # the uplink sheet's parsers: the ready/result lines, the skip line (the
    # only legal way of not running), and the host server's own row
    up_ready = ("[ 127.107] wlan8189: uplink phase 1 ready lever=5 arm=A "
                "pump=adaptive poll_us=100 data_us=100 agg=0x200f yield=0 lps=2")
    m = UPLINK_READY_RE.search(up_ready)
    check("uplink-ready-parse", bool(m) and m.group("phase") == "1"
          and m.group("arm") == "A")
    up_res = ("[ 127.405] wlan8189: uplink phase 1 result bytes=20830574 "
              "ms=10234 kbit=16280 type=1")
    m = UPLINK_RESULT_RE.search(up_res)
    check("uplink-result-parse", bool(m) and m.group("bytes") == "20830574"
          and m.group("kbit") == "16280")
    check("uplink-skip-parse",
          UPLINK_SKIP_RE.search("wlan8189: uplink skipped reason=no-peer")
          is not None)
    check("uplink-ready-rejects-skip",
          UPLINK_READY_RE.search(
              "wlan8189: uplink skipped reason=no-peer") is None)
    rows = uplink_server_totals(
        "[  1] 0.0000-11.0440 sec  19.9 MBytes  15.1 Mbits/sec")
    check("uplink-server-row", len(rows) == 1 and rows[0]["kbit"] == 15100
          and rows[0]["bytes"] == 19900000)
    check("uplink-server-empty", uplink_server_totals("no rows") == [])

    # the join's completion latch: the board's own line and the supplicant's
    # handshake line both count, and the wait line must not read as completion
    check("join-ok-parse",
          JOIN_OK_RE.search(
              "wlan8189: join-ok bssid=14:a3:2f:18:07:2c state=RUN") is not None
          and JOIN_OK_RE.search(
              "wlan0: WPA: Key negotiation completed with aa") is not None
          and JOIN_OK_RE.search(
              "wlan8189: join waiting for the supplicant") is None)

    # the channel-pinned branch: frames without a beacon is the pinned-failure
    # signature, and an empty capture is a different verdict again
    no_beacon = [{"st": 0x28, "ta": "aa", "ra": "bb"}]
    check("verdict-channel-not-pinned",
          channel_pinned(no_beacon) == "channel-not-pinned")
    with_beacon = [{"st": 0x08, "ta": "aa", "ra": "ff"}]
    check("verdict-channel-ok", channel_pinned(with_beacon) == "ok")
    check("verdict-empty-air", channel_pinned([]) == "no-frames")

    # the traffic parser: a real summary line, and a line that only looks like
    # one must not produce a number
    sample = ("[  3] local 10.0.0.2 port 5001 connected with 10.0.0.9 port 40000\n"
              "[  3]  0.0- 8.0 sec  19.4 MBytes  20.3 Mbits/sec\n")
    r1 = parse_iperf(sample)
    check("iperf-parse", r1["mbit"] == 20.3 and r1["mb"] == 19.4)
    r2 = parse_iperf("[  3]  0.0- 8.0 sec  0.00 Bytes  0.00 bits/sec\n")
    check("iperf-parse-zero", r2["mbit"] == 0.0)
    check("iperf-parse-empty", parse_iperf("no summary here")["mbit"] is None)

    # per-phase actions: the token grammar both ways, and the command each
    # token builds - the window pin is the point of the whole flag
    pa = parse_phase_actions("tcp,tcp-w256K,udp")
    check("phase-actions-parse",
          [a["udp"] for a in pa] == [False, False, True]
          and pa[1]["window"] == "256K" and pa[0]["window"] is None)
    check("phase-actions-empty", parse_phase_actions("") == [])
    bad = 0
    for spec in ("bogus", "tcp-w2X5", "udp-w", "tcp,tcp-w"):
        try:
            parse_phase_actions(spec)
        except ValueError:
            bad += 1
    check("phase-actions-rejects-bad", bad == 4)
    check("phase-cmd-window",
          phase_client_cmd("10.0.0.5", 8, False, 5002, "60M", "256K")
          == "iperf -c 10.0.0.5 -p 5001 -t 8 -i 2 -w 256K")
    check("phase-cmd-udp-no-window",
          phase_client_cmd("10.0.0.5", 8, True, 5002, "60M", "256K")
          == "iperf -c 10.0.0.5 -p 5002 -t 8 -i 2 -u -b 60M")
    check("phase-cmd-default-no-window",
          phase_client_cmd("10.0.0.5", 8, False, 5002, "60M", None)
          == "iperf -c 10.0.0.5 -p 5001 -t 8 -i 2")

    # a dry-run phase must build every command it would run - the client, the
    # alongside ping and the mid-window socket read - and execute none of them.
    # This is the branch an offline test can reach, and it is where a broken
    # call in that construction is caught instead of on the board.
    tmpd = tempfile.mkdtemp(prefix="prober-selftest-")
    dry = Runner(None, True, False)
    res = measure_phase(dry, tmpd, 1, "10.0.0.9", 1, False,
                        ping_during=True, ss_during=True, window="256K")
    check("phase-dryrun-builds-actions",
          res.get("mbit") is None
          and os.path.exists(os.path.join(tmpd, "phase1.txt"))
          and not os.path.exists(os.path.join(tmpd, "phase1-ping.txt"))
          and not os.path.exists(os.path.join(tmpd, "phase1-ss.txt")))
    os.remove(os.path.join(tmpd, "phase1.txt"))
    os.rmdir(tmpd)

    # the field discovery: only names the tool actually has are used
    known = set("frame.time_epoch wlan.fc.type_subtype frame.len wlan.ta".split())
    picked = {}
    for concept, names in FIELD_CANDIDATES.items():
        for n in names:
            if n in known:
                picked[concept] = n
                break
    check("fields-present", picked.get("time") == "frame.time_epoch"
          and picked.get("subtype") == "wlan.fc.type_subtype")
    check("fields-absent-not-zero", "mcs" not in picked and "retry" not in picked)

    # the direction split and the honesty of a missing station address
    frames = [
        {"t": 1.0, "st": 0x28, "retry": 0, "len": 1500, "ta": "STA", "ra": "AP", "mcs": 7, "rate": None, "ampdu": None},
        {"t": 1.0002, "st": 0x1D, "retry": 0, "len": 14, "ta": "AP", "ra": "STA", "mcs": None, "rate": None, "ampdu": None},
        {"t": 1.01, "st": 0x28, "retry": 1, "len": 1500, "ta": "STA", "ra": "AP", "mcs": 7, "rate": None, "ampdu": None},
        {"t": 1.0102, "st": 0x1D, "retry": 0, "len": 14, "ta": "AP", "ra": "STA", "mcs": None, "rate": None, "ampdu": None},
        {"t": 1.1, "st": 0x08, "retry": 0, "len": 120, "ta": "AP", "ra": "ff", "mcs": None, "rate": None, "ampdu": None},
    ]
    s = analyze_frames(frames, "sta", {})
    check("analyze-direction", s["per_direction"]["from_sta"]["data"] == 2
          and s["per_direction"]["to_sta"]["ack"] == 2)
    # the subtype table and the counting must agree, or a renamed subtype would
    # silently stop being counted
    named_ack = sum(1 for f in frames if SUBTYPE_NAMES.get(f["st"]) == "ack")
    check("analyze-subtype-table-used",
          named_ack == 2 and named_ack == s["by_subtype"].get("ack") == 2)
    check("analyze-retry-share",
          s["per_direction"]["from_sta"]["retry_share_pct"] == 50.0)
    check("analyze-ack-turn", s["ack_turn_us"]["n"] == 2
          and 150 <= (s["ack_turn_us"]["p50"] or 0) <= 250)
    check("analyze-infer-sta", analyze_frames(frames, "", {})["sta"] == "sta")
    s2 = analyze_frames(frames, "not-a-station-present", {})
    check("analyze-no-sta-honest", "from_sta" not in s2["per_direction"]
          and any("station address" in u for u in s2["unavailable"])
          is False)

    # the histogram never drops the tail
    h = hist([1, 2, 100], [1.5, 50])
    check("hist-overflow-visible", h["<=1.5"] == 1 and h[">50"] == 1)

    # a missing value is never zero
    check("num-missing-is-none", num("") is None and num("-") is None)
    check("num-hex", num("0x1d") == 29)

    # the channel number conversion both ways
    check("channel-2g", channel_to_number(2412) == 1
          and channel_to_number(2437) == 6)
    check("channel-5g", channel_to_number(5220) == 44)

    # the access point config carries the caller's key, not a default
    conf = hostapd_conf("wlan9", "SSID", "KEY", "1")
    check("ap-conf-has-key", "wpa_passphrase=KEY" in conf
          and "ssid=SSID" in conf)

    out("%s: selftest %s" % (TOOL, "PASS" if not fails
                             else "FAIL (%s)" % ",".join(fails)))
    return 0 if not fails else 1


# --------------------------------------------------------------------------

def main(argv: list[str]) -> int:
    if not argv or has(argv, "--help") or has(argv, "-h"):
        out(__doc__.strip())
        return 0

    words = positional(argv)
    if not words:
        return die("no command given (see --help)")
    cmd = words[0]
    if cmd == "selftest":
        return selftest()

    r = make_runner(argv)
    if cmd == "radios":
        return cmd_radios(argv, r)
    if cmd in ("mon", "sta", "ap"):
        return {"mon": cmd_mon, "sta": cmd_sta, "ap": cmd_ap}[cmd](argv, r)
    if cmd == "traffic":
        return cmd_traffic(argv, r)
    if cmd == "run":
        return cmd_run(argv, r)
    if cmd == "analyze":
        return cmd_analyze(argv, r)
    return die("unknown command %r (see --help)", cmd)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
