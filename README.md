# freewificam

Run the Linux wireless stack on an RTOS.

`freewificam` takes `net/mac80211` and `net/wireless` **verbatim** and runs them
on top of a small kernel-API layer, with the bus backend and the network egress
replaced. The MAC layer — MLME, aggregation, rate control, encryption, channel
management — is the Linux code, unmodified. What changes is what sits below it
(the bus) and above it (the network stack).

```
   lwIP                                     network egress, replaced
   ────────────────────────────────────
   net/mac80211 + net/wireless              upstream, verbatim
   ────────────────────────────────────
   components/liblinux80211                 kernel API + bus engines
   ────────────────────────────────────
   CherryUSB / PCIe / SDHCI host + driver   bus, replaced
```

## Where the line is

This repository is the successor of the previous net80211 line. It starts by
bringing the **RTOS platform itself** up in this tree, because the mac80211 work
needs a board that boots before it needs a stack that links:

| Step | State |
|---|---|
| RTOS baseline builds and boots (`rk3568-tick`) | done, 2/2 |
| Four acceptance images build and boot (network, PCIe/NVMe, eMMC, USB storage) | done, 2/2 each, judged |
| `components/liblinux80211` (linuxkpi + PCI/SDIO engines + seams) | landed; `rk3568-kpi-live`/`-mock` 2/2 |
| mac80211 + cfg80211 + iwlwifi | C0-C6 reached: register, attach, scan, join, data plane (2/2 cold boots each) |
| C6 throughput / stability | measured on the AC 7260 bench; numbers below |
| AP role (RTOS as AP, Pi 4B as STA) | **supported**: the AC 7260 beacons, the Pi associates, the data plane runs both ways and WPA2-PSK/CCMP is 2/2; the A4 transmit stall is fixed - throughput below. The uplink is still 57.3% of the GT AP |
| Monitor role (RX only) | **2/2**: user monitor vif, `SET_INTERFACE`/`dev_open`/`SET_WIPHY` all `rc=0`, 9.87 / 10.4 beacon/s against the same card's 9.25 on a mainline kernel; no injection |

`docs/STATUS.md` states each stage as what it is; `docs/BUILD.md` records the
build's design and how a new world is added.

## Performance

One board, one bench, one client.  The tables are split by module - platform -
bus, so a new module adds a table instead of a wider row.  Case names are fixed
(registry below); a table reports the cases it measured, and a new case is
added to the registry first.

In the AC 7260 tables every arm is the same card: the Linux column is that card
on a mainline kernel (`iwlwifi`, `iwlmvm`), the `rtos` column is the same card
on this port.  The RTL8189FTV tables are a second card and a second driver
world, and their Linux column is that card on a mainline kernel with the
vendor's own stack.  A rate
is payload bytes over the interval that actually elapsed, counted at the
receiver: `iperf2`'s own window where the peer receives, the case's byte counter
where the board receives, and the peer's counting sink for the AP role's UDP
downlink (which is also what the loss figure comes from).  A cell is one cold boot; where
a case runs twice inside one boot, the cell is the larger of the two repeats,
which is the rule the gates use.

### Cases

| case | traffic | condition | parameters |
|---|---|---|---|
| `W-TCP-D/HT20` | TCP bulk, access point to station (**the receiver** is the board in the station role and the peer in the AP role) | 2.4 GHz, HT20 | iperf2, 28 s window, two repeats per boot |
| `W-TCP-U/HT20` | TCP bulk, station to access point (**the receiver** is the peer in the station role and the board in the AP role) | same | iperf2, 28 s window |
| `W-UDP-D/HT20` | UDP 1470 B, 40 Mbit/s target, downlink | same | iperf2 `-u -b 40M -t 18`; loss from the two ends' payload counts |
| `W-UDP-U/HT20` | UDP 1470 B, 40 Mbit/s target, uplink | same | same cell, reversed |
| `W-MON-BEACON/HT20` | beacon + assoc rate seen by a monitor vif | 2.4 GHz ch5 | 15 s window, frame census by type |

Zero loss in every row unless stated; a Wi-Fi cell that drops more than 1% is
void.  The two benches are not interchangeable and are named per table: in the
station role the peer is the Pi's RTL8812AU AP on channel 5 (2432 MHz); in the
AP role the peer is a Pi station (the MT7921AU on the AC 7260 bench, where the
board beacons on channel 11) and the RTL8189FTV beacons on channel 5.  Both are
open unless the row says WPA2.  A rate is quoted over the interval the flow
actually ran, which is not always the phase's own window - where the two differ
the row says so.  A volume in MB is 10^6 bytes; where a figure is `iperf2`'s
own `MBytes` column it is MiB and the row says so.

### RK3568 / Intel AC 7260 / PCIe - station

`PROFILE=rk3568-iwl7260-c6-pool128` (and `-c6` for the clean baseline), driven
by `tools/rtos/c6-throughput.sh`; gates applied by `tools/rtos/c6-report.py`.
Two cold boots per arm.

Each column is two cold boots, and each cell is that boot's larger repeat.

| case | rtos, pool64 (baseline) | rtos, pool128 | Linux reference | vs reference |
|---|---|---|---|---|
| `W-TCP-D/HT20` | 67.2 / 60.8 | 62.7 / 58.2 | 42.3 / 42.5 | **142.6%** |
| `W-TCP-U/HT20` | 21.5 / 17.6 | **25.6 / 20.7** | 26.7 / 23.0 | 93.3% |
| `W-UDP-D/HT20` | 37.8 / 37.8 | 37.8 / 37.9 | 42.1 / 40.7 | 91.5% |
| `W-UDP-U/HT20` | void | void | void | - |

The uplink was 78.8% of the reference until the static skb data pool went from
64 to 128 slots, which is the single-variable arm between the two `rtos`
columns: the transmit path's census showed `data_high=64`, exactly the pool
size, and the pool was the wall rather than the air.  The downlink is above the
reference and shifts with boot noise; the reference column's own spread
(42.3 / 42.5 TCP, 40.7 / 42.1 UDP) is the same size.  UDP loss is computed from the
two ends' payload counts rather than read off `iperf2`, whose client prints no
loss column when the server does not answer its report: 94.4-94.8 MB counted at
the board against the peer's 90.0, i.e. 0.0-0.3%.  The two-source check -
application bytes against the AP's own capture of the same phase - closed at
2.4% on UDP and 0.002% on TCP.

The sibling profile `rk3568-iwl7260-sta-wpa2` joins a WPA2-PSK/CCMP AP on the
same channel and runs the same six phases over the encrypted link, two cold
boots:

| case | rtos, WPA2 (pi2, pi4) | Linux reference (open, above) |
|---|---|---|
| `W-TCP-D/HT20` | 42.2 / 41.7, 41.9 / 42.6 | 42.3 / 42.5 |
| `W-TCP-U/HT20` | 24.8, 26.5 | 26.7 / 23.0 |
| `W-UDP-D/HT20` | 37.8, 37.8 | 42.1 / 40.7 |
| `W-UDP-U/HT20` | void (board side only: 40.8, 40.7) | void |

The encrypted downlink lands on the open-AP reference rather than under it: the
software CCMP path is not what costs these cells.  Two limits on reading the
table.  There is no loss figure for these rows, because the peer's `iperf2`
window (18 s) and the board's phase window (20 s) are not the same interval, so
the two byte counts cannot be differenced the way the open arm's sink allows.
And the reference has no encrypted arm at all, so its column here is the open
one and the ratios are indicative rather than a gate.

The 40.8 / 40.7 in that last row is the board's own `report` line, not a
received figure, and the harness cannot weigh this direction in the station
role: the board blasts raw datagrams with no `iperf2` header, so the peer's
counting sink sees only the fraction the port accepted (1.1-1.7 Mbit/s), and the
board's counter has hundreds of thousands of refusals behind it.  The AP role
weighs the same direction properly because there the *board* receives; that row
is above, and it is void too.

### RK3568 / Intel AC 7260 / PCIe - access point

`PROFILE=rk3568-iwl7260-ap-a4`, driven by `tools/rtos/ap-round.sh`; gates
applied by `tools/rtos/ap-report.py`.  Two cold boots.  The AP MLME is the
ported NXP `hostapd`; the Linux column is the same card under the distribution's
`hostapd` on channel 11.

Each boot ran the downlink case twice; both repeats are printed for it and
nothing is averaged away.  The gate is on the mean of all four.

| case | rtos, a4-1b | rtos, a4-2 | Linux reference | vs reference |
|---|---|---|---|---|
| `W-TCP-D/HT20` | **72.7 / 72.8** | **73.3 / 74.1** | 73.3 | **99.9%** (mean 73.2) |
| `W-TCP-U/HT20` | 43.7 | 44.1 | 76.6 | 57.3% (mean 43.9) |
| `W-UDP-D/HT20` | 37.56 / 37.56, loss 0.112% / 0% | 37.56 / 37.55, loss 0.0098% / 0% | 41.9, loss <=0.02% | 89.6% |
| `W-UDP-U/HT20` | 36.8, 2.2% missing | 36.9, 2.5% missing | 41.9, 0% | void (over the 1% line) |

The uplink row is void rather than measured, and it is the one cell the port
misses by loss rather than by rate: the peer sent 64,201 datagrams in both boots
and the board's receive path counted 62,757 and 62,620 of them.  The delivered
rate is within 12% of the reference; the 2.2-2.5% loss is what disqualifies the
cell, so nothing here should be quoted as a rate.

The downlink reaches parity and the uplink does not.  The first sustained flow
on this profile used to stop the device's interrupts, which read as firmware
silence: the hard-interrupt count froze, no transmit completion came back and
the skb pool filled.  It was the host - the port's `__napi_schedule()` ran one
inline poll, and iwlwifi's receive poll re-enables `CSR_INT_MASK` only when a
poll returns less than its budget, so a poll that returned the whole budget
left the mask at zero with work still pending.  The shim now repeats the poll
until the driver's drain round re-arms the interrupt
(`docs/decisions/2026-11-12-ap-a4-napi-budget.md`).  What is left on the uplink
is the board's receive path, and it is the same wall the station role's uplink
sits against; it is an open item, not a pass.

### RK3568 / Intel AC 7260 / PCIe - monitor

`PROFILE=rk3568-iwl7260-mon`, driven by `tools/rtos/mon-round.sh`.  The Linux
column is the same card as a `iw` monitor under mainline `iwlwifi`; the count in
both arms is beacons and frames from the saved capture, not the live stream.

| case | rtos | Linux reference | vs reference |
|---|---|---|---|
| `W-MON-BEACON/HT20` | 9.87 / 10.4 beacon/s (297 / 256 frames in 15 s) | 9.25 beacon/s | +6.7% / +12.4% |

The frame census covers beacons, probe requests and responses, auth, assoc, data
and control frames; radiotap's `it_len` set is 56/64 in both arms, and every
frame but the handful that arrived before the channel was set is on 2432.  The
interface is receive-only: nothing is injected, `tx_packets=0`, and lwIP's
counters are untouched.  Injection, channel hopping, the 5 GHz band and a
five-minute soak are untested.

### RK3568 / Realtek RTL8189FTV / SDIO - station

`PROFILE=rk3568-rtl8189fs-c6t-adaptive-yield` (and `-prio1`, `-drain`), driven
by `tools/rtos/c6-throughput.sh`; gates applied by `tools/rtos/c6-report.py`.
The Linux column is the same card on a mainline kernel with the vendor's own
`rtl8189fs` stack (`tools/gt/gt-8189fs-board.sh --mode station`), same bench,
same day, same clean module.  Three cold boots on the port, one on the
reference; the three RTOS columns are the arms of the pump-timing sweep, which
is where the port's transmit pacing lives.

| case | rtos, adaptive+yield | rtos, +prio1 | rtos, +drain | Linux, same day | vs Linux |
|---|---|---|---|---|---|
| `W-TCP-D/HT20` | 30.0 | **30.5** | 29.0 | **41.8** (162 MB / 32.56 s) | 69-73% |
| `W-TCP-U/HT20` | **11.0** | 10.9 | 10.5 | 9.97 (27.1 MB / 22.78 s) | **105-110%** |
| `W-UDP-D/HT20` (35 Mbit/s offered) | **33.1**, no measurable loss | 33.1, same | 33.1, same | 36.7, 0% | 90% |
| `W-UDP-U/HT20` (35 Mbit/s offered) | 35.2 offered, **30,711,550 B** accepted | 35.2, 31,665,870 B | 35.2, 29,012,190 B | 23.3 offered, peer reports 10.5 MB accepted | void (see below) |

Three readings the table needs.  The TCP downlink is 69-73% of the same-day
reference, and that is the one number in this table to trust: both ends of both
arms are counted by the same instrument over the interval the flow actually
ran.  The round's own ledger called this pair 87-93% by dividing the reference's
*board* counter by its 45 s window; that window carries the peer's ~12 s
connect lead-in and the board's counter covers more than the peer says it sent,
so it is not a usable denominator.  Against the reference
document's own historical anchor (30.9 / 31.3, peer-timed, same method) these
rows are 95-97%; the same-day bench ran about a third faster than the day the
anchor was taken, which is exactly why the denominator is re-taken the same day.

The UDP uplink row is void, not a win.  Both arms name 35 Mbit/s and neither
delivers it: the port's sink accepts 30.7 MB of the 88.1 MB the board emitted, and the
reference's own two ends disagree with each other (23.3 Mbit/s at its board's
counter against 10.5 MB in its peer's report).  They are also weighed
by different instruments over different windows - the reference's `iperf2`
server answers with its own report, the port's raw blast has no `iperf2` header
and only a counting sink can weigh it - so the two cannot be differenced.  The
round's ledger read this pair as 151% of the reference; that comparison is
between two offered rates and is not a measurement of either.

UDP downlink is a paced blast: the port's `report` byte count is what it
*offered*, and the loss figure is the peer's count against it - 82,761,000 B
counted at the board against the peer's 56,177 x 1470 B = 82,580,190 B, i.e.
the two agree to 0.2% and the sign is the window difference, so no loss is
resolvable here.  The uplink is the port's raw blast
with no `iperf2` header, so the accepted figure comes from the peer's counting
sink (`tools/rtos/udp-sink.py`), not from `iperf2 -s -u`.  The port's own census
for the blast phase is `tx_bytes=32,247,150 tx_fail=75,534` in one boot: the
refusals are the pacing loop's, and they are why the offered number is not a
throughput number.

### RK3568 / Realtek RTL8189FTV / SDIO - access point

`PROFILE=rk3568-rtl8189fs-ap-tp`, driven by `tools/rtos/ap-round.sh`; gates
applied by `tools/rtos/ap-report.py`.  The AP MLME is the ported NXP
`hostapd`; the peer is a Pi station (`MT7921AU`).  The Linux column is the same
card as a mainline `hostapd` AP on the same channel, same day, clean module.
Two cold boots each.

| case | rtos, a4-2 | rtos, a4-3 | Linux, same day | vs Linux |
|---|---|---|---|---|
| `W-TCP-D/HT20` | 14.9 | **15.3** | **34.5** (84.6 MB / 20.57 s) | 43-44% |
| `W-TCP-U/HT20` | 13.6 | **16.2** | **38.7** (113 MB / 24.52 s) | 35-42% |
| `W-UDP-D/HT20` 15 Mbit/s | 14.58 received, **0.0% loss** | 14.58, 0.0% | 15.7, 0% | 93% |
| `W-UDP-D/HT20` 25 Mbit/s | 25.5 offered, 22.4 received (**12.3% loss**) | 25.5, 23.2 (9.1%) | 26.2, 0% | ~89%, void over the 1% line |
| `W-UDP-D/HT20` 35 Mbit/s | 35.2 offered, 22.5 received (**36.3% loss**) | 35.2, 22.9 (35.0%) | 36.6, 0% | 63%, void |
| `W-UDP-U/HT20` 35 Mbit/s | board's counter 11.1, peer sent 21,155 datagrams in 18.0 s (13.8) | board's counter 14.2, peer 26,679 datagrams (17.4) | n/a (no such phase) | - |

The AP role is the weaker role and the table says so plainly.  The 15 Mbit/s
rung lands within 7% of the reference with no loss; from 25 Mbit/s up the port
keeps offering the rate and the peer stops receiving it, so the cell is void on
loss rather than on rate.  TCP is 43-44% downlink and 35-42% uplink.

Three limits on reading this table.  The reference has no UDP uplink phase at
all, so that row has no denominator and is not a parity claim either way.  The
board's counter and the peer's window are not the same interval (20 s against
18 s), so the two columns cannot be differenced into a loss figure here - unlike
the downlink rows, where the peer's sink and the phase's counter do share a
window.  The reference's UDP ladder stops at 35 Mbit/s, exactly where the
port's loss becomes fatal, so nothing here says whether the port could carry 45.
And the downlink loss percentages come from the peer's counting sink, the one
end that can count datagrams, because the port's blast carries no `iperf2`
header.

What the AP role's counters say about where the loss is: in the 35 Mbit/s
downlink phase the board transmits 52.85 MB and the peer's sink accepts 22.9 MB,
while every TCP counter in the same boot (`tcp_drop`, `memerr`, `tcp_err`,
`pbuf_err`, `tcpseg_err`) is zero - the loss is on the air or at the peer's
driver, not in lwIP or the port's queues.  The AP's own air census for the same
boot counts 67,938 frames down and 125,725 up with `retry=0` and `ampdu=0`: the
reference peer's driver reports no retries for these frames, and the port's
counter records no aggregation, so rate/aggregation state is the open
observation face here, not a conclusion.

### Stability

One 30-minute `W-TCP-D/HT20` window on the station role: 1,777,484 ms,
13,947,861,328 bytes at the adapter, peer-side 60.4 Mbit/s, 60 health samples,
no reset, and ten reconnects each completing in 713-903 ms with 4/4 ping.  The
image reports `cases=4/4`.

### What these tables do not cover

- **5 GHz.**  The AP role cannot beacon there: the card's per-channel flags
  carry `NO_IR` on every 5 GHz channel and the mainline kernel on the same card
  and bench refuses identically, so it is the card, not the port.  The station
  role receives and transmits on 5 GHz but does not associate yet.
- **Channel widths above 20 MHz.**  Every cell above is HT20.  The AP seam sets
  `secondary_channel = 0` and `ieee80211ac = 0`.  This one is a reading of the
  code, not a measurement: with a secondary channel set, hostapd asks the driver
  to scan for neighbouring BSSes and reads the results back through
  `scan2` / `get_scan_results2`, and the AP profile's `wpa_driver_ops` table
  carries neither (they are compiled in only for the station profile), so
  hostapd would refuse the configuration rather than run 40 MHz unverified.
  HT40 and VHT are unmeasured, so "2x2 802.11ac" is the card's specification
  here, not a result.
- **Power save.**  `ps_buffered` is the driver's own ledger; there is no
  `SET_POWER_SAVE` A/B behind it.

The console transcripts for every role, including the two 5 GHz refusals, are
in [docs/CONSOLE-EVIDENCE.md](docs/CONSOLE-EVIDENCE.md).

## Why

Porting a Linux wireless driver to an RTOS is well-trodden: the driver talks to
the kernel through a kernel-API compatibility layer, and the layer is small
because a driver uses a bounded slice of the kernel. Porting the *stack* is the
part that is usually avoided — a stack leans on scheduling, memory lifetime and
locking models that an RTOS does not have, and reimplementing it drifts from the
code being tested.

This repository tests the other split: keep the stack, replace the two edges.

## Requirements

- **Single core.** The RTOS side is a uniprocessor FreeRTOS port. Every
  read-copy-update primitive the stack uses is a plain load/store there, which
  is equivalent rather than approximate *because* there is no other core to be
  concurrent with. This is a precondition of the design, not an implementation
  detail; a multiprocessor build needs the whole RCU discipline re-done.
- **A bus backend** for the device you target (USB host stack, a PCIe host, an
  SDHCI/eMMC host).
- **An egress** to your network stack (lwIP is the intended one).

## Layout

| Path | Contents |
|---|---|
| `tests/board/rtos/platform/` | RK3568 board port, AArch64 boot layer, FreeRTOS/CMSIS-RTOS2 runtime |
| `tests/board/rtos/app/` | the case runner and the acceptance cases |
| `tests/board/rtos/build/` | the platform Makefile and the per-world fragments |
| `tests/board/rtos/configs/` | the profiles (one defconfig each) |
| `third-party/` | FreeRTOS, CMSIS, lwIP, CherryUSB, fsl_sdmmc, printf, tlsf, ulog (compiled closures, pins in `docs/PROVENANCE.md`) |
| `tools/` | the Kconfig generator, the build gates, the judge, the commit checker |
| `components/` | first-party components; `liblinux80211` is the next one |
| `docs/` | provenance, divergences, licenses, build design, status |

## Build and boot

```
AARCH64_CROSS_PATH=<xpack prefix> ./make.rtos list
AARCH64_CROSS_PATH=<xpack prefix> ./make.rtos image PROFILE=rk3568-emmc
```

The image is `tests/board/rtos/build/build/rtos-freertos.bin`: a flat raw image
linked at `0xa000000`, loaded over TFTP and entered with `go 0xa000000`. The
board-side control (serial, power, TFTP, evidence) lives in the project's
`skills/rk3568-board`.

## License

The platform, the build and the tools written here are `BSD-3-Clause`. The
imported trees keep their own terms (MIT, Apache-2.0, BSD-3-Clause, ISC); see
`docs/LICENSES.md`.

When `net/mac80211` and `net/wireless` land in `components/liblinux80211`, every
binary that links them becomes a `GPL-2.0` combined work. That is why the
component is a separate build unit and a separate profile family.
