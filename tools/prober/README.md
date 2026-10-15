# prober - the wireless measurement peer

`prober.py` turns a Linux host with a monitor-capable USB wireless card into the
far end of a board test. It plays four radio roles and one traffic role:

- **monitor** - puts a card into monitor mode on a pinned channel and captures
  the air, then reports what the air says per direction;
- **station** - joins an access point (the case where the board hosts one);
- **access point** - hosts an access point the board joins, on a pinned channel
  and width, with a known address for the board;
- **traffic** - drives or serves the traffic a throughput window is measured on;
- **run** - one measurement round: it follows the board's own phase lines,
  drives the client inside each window, and slices the capture per phase.

It exists because board logs only describe the board's own view. The hardest
wireless questions are of the form "did that frame actually reach the air", and
answering them needs a receiver that is not the device under test.

## Why it is not tied to one host or one card

The cards are resolved by their USB identity (`vendor:product`), not by
interface name, because interface numbering is not stable when more than one
card is attached. Roles come from what the card reports it can do, so a card
that cannot monitor is reported as such instead of failing later.

Known identities:

| identity | chip | bands | generation | typical role |
|---|---|---|---|---|
| `0cf3:9271` | AR9271 | 2.4 GHz | 11n | monitor, cross-check |
| `0e8d:7961`, `0e8d:7902` | MT7921AU | 2.4 + 5 GHz | ax | monitor on 5 GHz/ax links |
| `0e8d:7612` | MT7612U | 2.4 + 5 GHz | ac | monitor (no ax metadata) |

A card still on the USB bus but not bound to a network interface is reported as
present but unbound - that is the state a dongle sits in before its mode switch,
and otherwise it reads as a missing card.

## Requirements

On the measurement host:

- `iw`, `iproute2`, `rfkill`;
- `dumpcap` and `tshark` (the capture and the field extraction);
- `iperf` version 2 for the board's lwIP server, which speaks the iperf2
  protocol; `iperf3` for Linux lanes;
- `hostapd` and `dnsmasq` for access point mode; `wpa_supplicant` for station
  mode;
- root, or a sudo rule. Only the commands that change a radio or open a capture
  need privilege; every read-only probe runs unprivileged. A dedicated
  measurement host is the point of the box, so scoping the rule to the tools is
  enough:

```
<user> ALL=(root) NOPASSWD: /usr/sbin/iw, /usr/bin/ip, /usr/bin/dumpcap, \
    /usr/sbin/hostapd, /usr/sbin/dnsmasq, /usr/sbin/wpa_supplicant, \
    /usr/bin/pkill, /usr/sbin/rfkill, /usr/bin/nmcli, /usr/bin/mkdir, \
    /usr/bin/chmod
```

- passwordless ssh from the driving machine, when the tool is driven with
  `--host`;
- clock agreement between the driving machine and the host, so the phase
  boundaries recorded below line up with the capture's own timestamps.

## Usage

Read the cards before anything else; it is read-only and it answers whether the
roles are available at all:

```
prober.py radios --host <user>@<addr>
```

Capture:

```
prober.py mon up --iface <if> --channel 2412 --width HT20 \
    --out <file>.pcapng --duration 15 --host <user>@<addr>
prober.py mon status --iface <if> --host <user>@<addr>
prober.py mon down   --iface <if> --host <user>@<addr>
```

The channel may be given as a frequency (2412) or as a channel number (1); both
are accepted, because the capture side speaks frequency and the control tool
speaks channel number.

A full measurement round against a board running its phase sheet:

```
prober.py run --serial-hub http://<hub>:<port> --out <dir> \
    --iface <if> --channel 2412 --width HT20 \
    --ssid <ssid> --psk <key> --bssid <board's ap> \
    --host <user>@<addr>
```

`run` does not power, boot or deploy the board - that stays with the board
runner. It reads the board's console over the hub's HTTP interface, sends the
join command when the board says it is waiting for one, and for each phase it
records the wall clock, drives the client, and collects the board's own
`perf census:` lines that fall inside the window. It writes `result.json`, one
file per phase, a copy of the console, and the capture.

The client inside a phase is the TCP stream by default, and four options change
what it does and what else is recorded. `--udp` blasts datagrams at the board's
own sink instead (no acknowledgement clock, so the receive path's own ceiling);
`--window <size>` pins the sender's socket buffer (`-w`), because the sender's
own window is one of the limits a bulk transfer runs into; `--ping-during` runs
a ping alongside the transfer and keeps its summary in `phaseN-ping.txt` (the
round trip while the link is loaded); `--ss-during` reads the sender's socket
mid-window into `phaseN-ss.txt` (congestion window, smoothed round trip,
unacknowledged bytes, send buffer). `--phase-actions` overrides the arm and the
window per phase, left to right, so one boot can carry an A/B/A/B:

```
prober.py run ... --phase-actions "tcp,tcp-w256K,udp,tcp-w256K" \
    --ping-during --ss-during
```

A phase beyond the spec keeps the run-level defaults, and a token is `tcp`,
`udp` or `tcp-w<size>` - a window on a UDP phase is a usage error rather than a
silently ignored flag.

The reverse direction needs no extra flag: when the board's console announces
its uplink stage, `run` brings up an iperf2 server on this host, tells the board
on the console the address to send to - this host's address on the path to the
board, found with `ip route get` - and then records the stage's own ready and
result lines per window. The server's log is kept with the products
(`uplink-server.log`) and its per-test rows are the receiver-side figures the
uplink numbers are quoted from. A board boot that was not given a peer skips the
stage and says so on its console; `run` reports that as a skip, not a failure.

Analysis, either on a capture just taken or on an older file:

```
prober.py analyze <file>.pcapng [--sta <mac>] [--out <summary>.json]
```

## What the analysis reports

Per direction, with the station's address found from the pair evidence (the
address that both sends data and is the addressee of the acknowledgements for
it) unless one is given:

- the air census: frames by subtype, and who is transmitting;
- data frames, acknowledgements and block acknowledgements per direction, so
  whether aggregation is in use is answered by counting rather than assumed;
- the observed retry share, from the retry bit each frame carries;
- the data frame length distribution, which is what a receive-path question
  turns on;
- the turn-around from a data frame to the acknowledgement addressed to its
  sender, and the spacing between consecutive acknowledgements (whether the
  returning path is immediate or batched);
- the inter-arrival distribution of the downlink data frames, which is where a
  burst period shows up.

## Honesty rules this tool follows

These are not style preferences; a number read the wrong way sends the next
round in the wrong direction:

- the retry share is the share of frames the capture saw with the retry bit
  set, which is lower than the loss rate, and is not a loss rate;
- a missing field is reported as unavailable, never counted as zero;
- a count of zero acknowledgements means this capture carried none - not that
  none was sent;
- the absence of an aggregation marker does not prove aggregation was absent,
  so aggregation is reported from the block acknowledgement counts instead;
- the signal level is the signal at the capture point, not at either end;
- frames captured but no beacon of the target is the signature of a channel that
  was never pinned, and is reported as such rather than as empty air.

## Exit status

`0` ok; `1` a measurement or verdict failed (a phase was missed, the client
failed, the capture was empty); `2` usage, configuration or environment problem
(no such card, a missing tool, an unreachable hub).

## Offline control

`prober.py selftest` runs without any hardware. It exercises the phase-line
parser, the channel-pinned verdict, the traffic-output parser, the field
discovery, the direction split, the histogram tails and the channel conversion,
and it includes negatives for each so that a regression turns it red.

`--dry-run` prints every command the tool would run and executes none of them,
which is how the sequence is reviewed before it touches a radio.

## Credentials

Nothing secret belongs in this repository. The access point's name and key, and
any supplicant configuration, are passed as arguments or read from a file kept
outside the tree.
