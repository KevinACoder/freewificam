# Wired network testing: method and current numbers

This document is the basic test method for the wired 1 GbE port (DWC EQOS GMAC)
and the numbers the current tree reaches.  It is about how to test, not how to
tune: the Linux reference on the same board is measured with the same method, so
the two columns are comparable.

## 1. Metrics

Three tests, in this order:

1. **Link smoke**: link up at 1000/full, ping 20/20.
2. **UDP 1440 B zero-loss throughput**, both directions.  Report MiB/s and
   Mbit/s.  Zero loss means MAC FIFO overflow 0, ring-full 0, stack counters 0,
   and the layer closure holds.
3. **64 B zero-loss pps `T0`**: the highest offered rate for which a 10 s run
   delivers every frame.  Report `T0` (pps), `c0 = 1e6/T0` (us/frame) and
   `C0 = c0 * actual MHz` (cycles/frame).

TCP is not part of the accepted set yet (section 8).

## 2. Bench

One RK3568 board, one 1 GbE link, one sender host on the same wire.  The Linux
reference runs on the same board, the same link and the same sender.  Never
compare against a userspace receive loop: a userspace receiver spends work per
packet and turns an application limit into a fake platform wall.

The peer client is the pinned NETIO tree (`third-party/netio`), built by
`tools/netio/build.sh`.  Keep it unmodified: it is the one component shared by
every arm.

## 3. Build and boot

The profile is a defconfig in `tests/board/rtos/configs/`;
`tests/board/rtos/config.mk` only names the default.  The two profiles the
netio rounds use:

| Profile | What it tests |
|---|---|
| `net-netio-raw-tx256` | UDP 1440 B throughput; raw UDP consumer, TX ring 256 (the default profile) |
| `net-pps` | 64 B pps; serial-gated receiver for a zero-loss window |

Build with `./make.rtos image PROFILE=<name>` (the compatibility
`tools/rtos/build.sh` forwards); it prints size, sha256, build tag and elapsed
time.
Boot the image with `tools/rtos/board-round.py` (stage, boot, capture, judge).
The console log is the evidence; keep it with the run.

## 4. Counters and the layer closure

Snapshot before and after each run; every counter is a delta over the window.

| Layer | Counters |
|---|---|
| MAC/MMC | `rx_pkts_gb`, `rx_fifo_overflow`, `rx_buf_unavail` (ring full), `rx_crc_error` |
| Driver | `rx_dropped`, `rx_irqs`, RX ring state |
| Bridge | `pool_fail`, `input_fail`, `fed` |
| Stack | `udp.recv`/`drop`, `ip.drop`, `link.drop`, `mem.err`, `pbuf.err` |

Accounting:

```
offered   = frames + fifo_overflow + ring_full
accounted = delivered + fifo_overflow + ring_full + rx_dropped + bridge + stack
closure   = |accounted - offered| / offered
```

Rules:

- `ring_full` (`rx_buf_unavail`) is **not** in the MMC FIFO count.  If it is
  left out of `offered`, closure stays 0 while frames disappear.
- `ring_full` is an event/status counter, not necessarily a frame count.  At
  1440 B it can exceed the actual frame loss; at 64 B high pps it matched 1:1 in
  the measured runs.  Trust the closure, not the raw counter.
- A run with closure above 0.1% is invalid.

## 5. UDP 1440 B test

- Board runs the netio raw server; the host runs the pinned NETIO client
  (`netio -u -b 1440 <board-ip>`, or `-t` for TCP), 10 s per direction, 2/2.
- Accept only zero loss: FIFO overflow 0, ring-full 0, stack counters 0,
  closure within 0.1%, ping clean.
- Record MiB/s and Mbit/s.  `1 MiB/s = 1.048576 MB/s = 8.388608 Mbit/s`.

## 6. 64 B pps test

### 6.1 Frequency first

Set the CPU frequency explicitly and **measure the actual frequency**; do not
use the requested value as the clock.  Nominal OPP labels are not actual clocks
on this board: 1104/1416/1800/1992 MHz requests measured about
1443/1543/1689/1689 MHz with a cycle counter, and the top is about 1.69 GHz.
Record the measured MHz in every result; `C0` uses it.

### 6.2 Sender

`tools/netio/ppsgen.c` is the sender: target-scheduled pacing on two busy
threads, non-blocking sockets, 256-frame batches, an optional TCP control
channel.  A sleeping pacer changes loss at the same mean rate (measured: 5.9%
vs 0.001% at 190k pps), so the sender shape is part of the test definition:

```sh
build/netio/ppsgen -d <board-ip> -p 18768 -r 260000 -t 10 -T 2 -k 0 -B 256
```

### 6.3 Gates

Requested vs offered within 2%; offered vs the wire-side count within 1%;
window from the sender's own duration; closure within 0.1%.

### 6.4 Procedure

1. Set the frequency (`freq 1992000` on the console) and record the measured
   actual MHz.
2. If RX coalescing is used, enable it at runtime **after** the frequency
   change (`riwt 160`), not at boot.
3. Open the receive window on the board (`go cell`).
4. Start the sender (command in 6.2).
5. Stop the window when the sender ends (`stop cell`).
6. Summarize the cell: `tools/netio/pps-report.py --serial console.log
   --sender sender.log --tag cell`.  It computes offered/delivered, `c0`, `C0`
   and the validity gates.

### 6.5 `T0`

Start below the expected knee, step up, then bisect near the knee.  `T0` is the
highest offered rate with zero loss, repeated 2/2.  Near the knee the curve is
steep: a 0.2% offered difference can move loss from 0.001% to several percent,
so report `T0` as an interval.

### 6.6 Invalid runs

The first run after boot (ARP/link settle), a sender that misses its target, a
window mismatch, and a missing frequency are invalid.  Mark them and rerun; do
not average them in.

## 7. Current numbers

2026-10-31, reference board, 10 s windows, 2/2, zero loss, closure within 0.1%.

| Test | freewificam | Linux on the same board |
|---|---|---|
| UDP 1440 B, board receive | 114.00 MiB/s = 956 Mbit/s | 113.13 MiB/s |
| UDP 1440 B, board send | 108.09 MiB/s = 907 Mbit/s | 113.61 MiB/s |
| 64 B `T0`, 816 MHz actual | 197.5k pps | 100k pps (the 816 curve is not monotonic; reference only) |
| 64 B `T0`, ~1.69 GHz actual, per-frame interrupts | 260k pps | 195k pps |
| 64 B `T0`, ~1.69 GHz actual, RX coalescing 160 | 350k pps | 195k pps default; 230k+ with RPS |

At the 64 B knee the per-frame cost is 2.857 us (4,819 cycles at the measured
1,686 MHz) with coalescing, 3.774 us (6,366 cycles) without; the Linux reference
is 5.128 us (8,687 cycles at 1,694 MHz).

## 8. Open items

- RX coalescing is wired only into the `net-pps` case; `netio` and TCP still run
  per-frame interrupts.  The 1440 B and TCP paths need a re-test before
  coalescing becomes a default.
- Setting coalescing at boot does not reproduce the runtime result (0.35-7.5%
  loss at the 350k arm with the boot-time setting vs 0.001% at runtime); the
  mechanism is unresolved.  Use the runtime sequence.
- TCP is accepted since 2026-11-02, after two defects that turned out to be
  independent.  (1) Input path: the peer-to-board direction ran at 33 kB/s
  while the board's window stayed wide open (`wnd=131072`); with
  `LWIP_TCPIP_CORE_LOCKING_INPUT=1` the whole receive burst runs inside the
  driver's RX thread and the socket reader cannot drain until it ends - the
  socket's receive mailbox is one slot (lwIP's default is 0 and this port
  rounds up) - so the stack refused segments (`tcp.drop` / `mib2.tcpinerrs`)
  and only re-offered them from the 250 ms fast timer.  Input now runs on the
  tcpip thread (the upstream shape).  (2) Zero-copy slot table: at a real TCP
  rate lwIP died on `pbuf_free: p->ref > 0`; the free list and the ready ring
  were updated from three threads under a lock that only masks the port line,
  so a lost half of head/count eventually handed a slot out again while its
  pbuf was still in the stack - two pbufs over one custom wrapper.  Those
  updates now run in a masked-IRQ critical section (`ARCH_DAIF_GET` +
  `ARCH_IRQ_MASK`), and three lifetime counters (`netio: zc wrap_live/
  free_dead/dead_handle`) are printed at every layer tag and from the assert
  itself; they stay at zero across the runs below.
  Measured on the default image (TCP and UDP, 1440 B, 2/2): peer-to-board TCP
  **43.4 / 45.0 MB/s** (`tcp_retrans=0`), board-to-peer 49.8 / 49.9 MB/s, and
  the UDP cells **113.99/107.35 and 113.99/107.41 MB/s at 0% loss both
  directions** - the fix does not touch UDP.  At the 1.689 GHz OPP the same
  arm runs TCP 77.8 MB/s receive / 118.9 MB/s transmit with `tx_drop=0`.
- With the input fix in, `CONFIG_GMAC_RX_BATCH` no longer exists: it folded
  the per-frame core-lock acquisitions of the in-RX-thread shape.
- The `net-pps` arm is pinned to the old input shape by its defconfig
  (`CONFIG_LWIP_TCPIP_CORE_LOCKING_INPUT=y`) so the K2-12/13 `T0` numbers stay
  reproducible; the 64 B pps effect of the new shape is not measured.
- The board-to-peer TCP direction is still below the UDP numbers (50 vs
  107 MB/s) but no longer stalled, and the 1440 B UDP cells are unchanged.
- The RX ring is 128 descriptors; a deeper ring is untested for pps.  A
  256-descriptor ring measured -3% in the 1440 B path earlier, so it is not
  assumed to be free.
- The sender tops out near 350-375k pps on this bench; numbers above that are
  lower bounds, not platform limits.
- `tools/netio/ppsgen.c` and `tools/netio/pps-report.py` are the pps tools; the
  batch/window/turnover interaction is described in 6.2-6.5.

## 9. The two RTOS-side instruments beyond the wired netio arm

Both are built from `tests/board/rtos/config.mk`, one profile per image; neither
shares an image with the wired netio case.

### 9.1 Mass storage (U-disk) ladder - `usb-xhci` / `usb-ehci`

```
PROFILE=usb-xhci ./make.rtos image   # one controller per boot; the first ready disk wins
BOARD_SESSION=<client> BOARD_PYTHON=<python> \
  tools/rtos/board-round.py --backend freertos --out <run> --boot boot1 \
    --cases tick,usb_xhci
```

The case reads and writes the disk's last 32 MiB, 1 MiB aligned, at 4/64/512 KiB
(1 MiB on EHCI) plus an 8..128 KiB sweep, and prints
`usbh: msc io read|write size_kib=N runs=3 ... rate=R mib_s per_cmd_us=N` and
`usbh: msc sweep ...`.  The Linux ground truth is the same window and the same
command sizes: `DEV=/dev/sda sh tools/usb/fio-udisk.sh <out>` (fio is in the mfs
package set; psync + direct, three runs per arm, rotating offsets).

The arm needs a real USB mass-storage disk.  A Realtek "DISK" (`0bda:1a2b`) is
the wifi dongle's CD-ROM persona: it enumerates with `bInterfaceClass=8`, but
`usb-storage` ignores it by quirk and no `sdX` appears - check `lsblk`, not
`lsusb`.

### 9.2 WLAN netio server - `rtl8xxxu-8188eu-netio-ar-g` / `-ar-ht20` / `-ar-ht20-ba`

```
CONFIG := rtl8xxxu-8188eu-netio-ar-g     # legacy 11g AP (labwifi-ar-g)
tools/rtos/build.sh
# stage build/build/rtos-freertos.bin as rtos.bin, boot it, wait for:
#   net: netio ready port=18767 aux=18768 buf=16384
netio -u -b 1440 <board-ip>     # the bench host's pinned NETIO 1.34 client
```

`-ar-ht20` is the same image on the HT20 AP (`labwifi-ar-ht20s`);
`-ar-ht20-ba` adds `-DCONFIG_WLAN_RX_BA=1`, which links `wlan_rx_ba.c`: the
downlink receive side answers the AP's ADDBA Request through the bridge's
management window and delivers the aggregated MPDUs in sequence.  The imported
net80211 has no action handler, so without that module the AP asks five times,
tears the block-ack session down, and the downlink stays at one MPDU per TXOP:
19.9 Mbit/s, 618 us/frame of which 327 us is inter-frame gap.  With it:
50-51 Mbit/s (0.97x the Linux ko arm on the same AP), 229 us/frame,
BlockAck/A-MPDU visible on the air, ctrl 2.1 us/frame, gap 0.

The server runs inside the image; there is no shell on the board.  Its ports are
fixed (18767 control, 18768 data), so the client must not pass `-p`.  The UDP
transmit cell is a non-blocking blast over the WLAN netif: the bridge gates the
driver queue and wakes it on the next completion, so `sendto` returns `-1`
between wakes.  The server waits `S2C_REFUSAL_MAX` (200 ms) of consecutive
refusals before it treats the cell as an aborted one, which is what makes the
uplink measurable at all (before that, the first WLAN cell aborted after 104
frames on a healthy link).  The wired arm is compiled without the retry.

Per-cell evidence: the client's `Packet size ... Tx, ... Rx` line is the number;
the board prints `netio: udp c2s/rx done/s2c/tx done`, the bridge ledger
(`netio: layers ... wlan_tx/wlan_done/wlan_rx/wlan_gated/wlan_wakes`), a
per-frame cost (`netio: cpu tag=<phase> frames=N us_x10_per_frame=M`, i.e.
M/10 microseconds per frame), and the rate fingerprint (`wlan: link ...`).
Take the console slice per cell by byte window: the hub keeps one serial buffer
across boots, so a tail slice carries the previous cell's anchors.

Measured 2026-11-02 on the reference board (1440 B UDP, legacy 11g / HT20-SGI
AP, 2 boots x 3 cells, cpu at the pinned ~1.69 GHz):

| arm | legacy down | legacy up | HT20 down | HT20 up |
|---|---|---|---|---|
| freewificam RTOS, pre-BA | 19.0-19.4 Mbit/s | 31.7-32.5 | 20.0-20.5 | 31.3-31.6 |
| freewificam RTOS, RX-BA arm | - | - | 50.2-51.1 | 30.9-31.6 |
| Linux ko, same AP | 18.9-19.4 | 34.7-35.3 | 49.3-52.3 | 34.4-34.8 |

The pre-BA per-frame receive cost is inter-frame gap (610-621 us/frame on
legacy, 578-590 on HT20) against 12.2 us/frame for the same lwIP stack on the
wired port - it tracks the unaggregated medium, not the host.  The RX-BA arm
turns that into 229 us/frame (gap 0, ctrl 2.1) and 50-51 Mbit/s.

### 9.4 RTL8821CU USB arm (rtw88) - 2026-11-03

`rtw88-usb-8821cu{,-ar-g,-ar-ht20,-ar-ht20-ba}` are the same server on the
rtw88 USB arm (linuxkpi USB engine over CherryUSB, `RTWUSB88_BUS=USB`, stage
32768 because the driver hands the chip one 4144-byte firmware packet and
32768-byte receive urbs).  The bench AP is the AR9271 one, ch5 2432.

Two build-shape facts the arm needs, both adapter-side, no driver byte:

- `CONFIG_LKPI_USB_CHERRYUSB_STAGE_SIZE=32768` (default 4096 refuses the
  firmware packet with `transfer of 4144 bytes refused - a stage slot holds
  4096 bytes` and the probe then fails `-EBUSY` on `failed to download firmware`).
- `CONFIG_WLAN_RX_BA` for the HT20 receive side, same as the 8188EU arm.

Measured 2026-11-03 (1440 B UDP, 2 cells unless noted):

| arm | legacy down | legacy up | HT20 down | HT20 up |
|---|---|---|---|---|
| Linux GT (rtw88 in-kernel) | 21.4 | 18.3-19.6 | 54.9-55.4 | 10.0-10.8 |
| linuxko (`wlan_ko.ko`, WEXT join) | 21.4 | invalid | 54.5 (1 cell) | invalid |
| RTOS, no RX BA | 21.1-21.6 | 8.3-8.9 | 11.7-23.1 | invalid |
| RTOS, RX-BA arm | - | - | 54.5-54.8 (2 boots) | invalid |

The HT20 downlink gap against GT is the missing ADDBA answer: 11.7-23.1 without
it, 54.5 with it, legacy unchanged.  The legacy uplink ~8.5 against GT's ~19 is
a real stack-side gap (0% loss, zero `wlan_txerr`), still to attribute.

**The uplink cells of the non-GT arms did not close** (2026-11-03 morning):
the board-side server queued 440k frames in the six-second window while the
air carried ~6k (`srv_sent` against `got`), because the netdev TX path
returned success while the driver's queue was already full - `sendto` never
saw `-ENOMEM`, so the server's `S2C_REFUSAL_MAX` path was never entered.  On
the GT kernel the qdisc provides that backpressure and the same server paces
itself to the link.

**The RTOS arm closes now** (2026-11-03 evening).  The seam refuses a data
frame for want of a socket buffer (`WLAN_SEAM_TX_ERR_NOSKB`), and the bridge
holds that frame, keeps it in order, and re-offers it on the next transmit
completion (`wlan_bridge.c` pending frame + `lkpi80211_tx_wake_defer()`), so
the refusal reaches `sendto` once the 64-frame interface queue is full.
Three first-cells of three boots:

| cell | client Rx got/srv_sent | Mbit/s | `wlan_txerr` | `wlan_xdrop` | refusals waited |
|---|---|---|---|---|---|
| boot 1 | 14653/14806 = 98.97% | 19.3 | 0 | 0 | 5914 |
| boot 2 | 15476/15585 = 99.30% | 19.7 | 0 | 0 | - |
| boot 3 | 14498/14574 = 99.48% | 19.3 | 0 | 0 | 5923 |

The residual is air, not the bridge: the AP's own `rx_packets` delta is
99.35-99.5% of what the board handed to the seam.  The GT arm on the same
evening measured 10.2 Mbit/s up (5593/5593) - it is retry-bound (62.6% of its
uplink data frames are retries at MCS4 39 Mbit/s), while the RTOS uplink
transmits legacy 36 Mbit/s frames (no MCS entries, no TX block-ack) at 347
us/frame with 11.6% retries.  So the RTOS uplink's ceiling tonight is its
**rate**, not the air or the bridge; the uplink's HT path is the next piece of
work.  The ko arm is still unmeasured (its server needs the same pacing).

The `layers` line now carries `wlan_stash/wlan_stashok/wlan_xdrop/wlan_qhi/
wlan_pending` and the s2c done line carries `refusals=`, so a cell says on its
own whether it ran against backpressure or against the wire.

### 9.3 The 8723BU EHCI arm and the receive-side ledger

`rtl8xxxu-8723bu-netio-ar-ht20{,-ba}` are the same server on the EHCI root
(same AP and client; the module sits behind the board's hub, bus 1).  The
receive side there is slower than the wire: about 2,700 bulk completions per
second, because the CherryUSB EHCI port hands a transfer back only from the
interrupt-on-async-advance path (~370 us per completion), against 4,430/s for
the same one-transfer-in-flight engine on xHCI.  A downlink that offers more
than that overflows the chip's receive FIFO, the AP retransmits, and the
reorder window has to bridge the holes - so the cell tells you about the
reorder window, not about the host's per-frame cost.

The WLAN arms therefore carry a receive-side ledger of their own, enabled with
`-DCONFIG_NETIO_RXPATH=1` (the `*-probe` arms do it; the plain arms stay
byte-clean):

```
netio: rxpath  tag=... usb_rx=... usb_err=... usb_last=... usb_lasterr=...
               usb_full=... usb_max=... usb_hist=<256/<512/<1024/<1600/<2328/>=2328
netio: rxconn  tag=... rx_in=... rx_ref=... rx_drop=... rx_frames=... rx_q=...
               ep=... ep_q=... ep_inf=... ep_enq=... ep_comp=... ep_err=... ep_last=...
netio: rxdesc  tag=... w0=... w1=... w2=... w3=...
netio: usbeng  tag=... polls=... pdrain=... kicks=... eng_comp=... eng_err=...
               parked=... parked_now=...
```

`usb_rx`/`usb_err`/`usb_full` are the backend's bulk-IN completions, errors and
"buffer exactly full" count; `rx_in` is what the driver handed to
`ieee80211_rx_irqsafe`, `rx_frames` what the receive worker delivered to the
bridge, `rx_ref`/`rx_drop` the linearize refusals and the queue-full drops;
`ep_*` is the lkpi endpoint (queued, in flight, enqueued, completed, errors) and
`w0..w3` are the first four words of the last completed receive descriptor (the
receive header).  The bridge ledger (`netio: layers`, `wlan: link`,
`wlan: ba`) closes the last hop.

Three practical points:

- The cell may never close.  If the receive path delivers almost nothing, the
  client blocks in its transmit loop before it can send `CMD_RES`, so the board
  never prints `udp rx done`.  The arms now print the counters at the start of
  the cell (`tag=begin`), at its end (`tag=udp-c2s-end`, after the receive
  thread is quiesced) and every two seconds in between (`tag=cell`); use the
  deltas, not the begin line alone.
- The console truncates a message at 256 bytes, which is why the ledger is four
  lines; do not re-join them.
- `NETIO_PPS=<rate>` paces the client's UDP transmit loop without changing its
  burst shape.  It is the clean lever between congestion and an algorithm
  fault: the same cell that loses 77% at full rate delivered 6,016/6,016 at
  1,000 pps.

Measured 2026-11-02 (1440 B UDP, same AP/client, one board): the downlink on
the fixed image is 21.7-30.6 Mbit/s with zero loss in three of four cells, and
one cell that dropped 11%.  The unlock was the reorder window: 32 slots against
an AP block-ack buffer of 64, with the head parked on a missing sequence number
during AP retransmissions.  A window of 128 delivers 0 loss on the same cells,
and the xHCI 8188EU arm re-measured at 51.7 Mbit/s afterwards, so the change
does not tax the healthy path.  The remaining ceiling is the ~2,700
completions/s above.

### 9.5 A sustained uplink window (64 B, 2026-11-06)

The profile `rtw88-usb-8821cu-ar-ht20-agg2-ehci-long` is the same arm as
`-agg2-ehci` with one change: `CONFIG_NETIO_S2C_MS=30000`, so a transmit cell
is decided by its own timer instead of ending on the first long run of
refusals.  Nothing else moves - no rate lever, no queue depth, no observation
switch.

Client `netio -u -b 64`, the server's window armed for 30 s, eight boots
across four images of the same fix (the last on the tree rebuilt from the
commit).  Per boot: `frames` 312k-334k, client `got/srv_sent` short by 0.02%,
the access point's `rx_packets` delta short of `srv_sent` by 0.018-0.019%,
`wlan_txerr` 0, `wlan_xdrop` 0, `tx q` and `txqwalk` equal at every two second
sample (zero in seven of the eight boots; one boot reads 47, 33, 23 on both
sides of the same samples), 4.3-4.7 Mbit/s of 64 B payload (8.4-9.1k
frames/s).

Two properties of this arm are worth knowing before it is compared to anything:

- The burst lasts 36.6-36.7 s, by the board's counter and by the client's own
  byte count over its own rate, while the timer is armed for 30 s.  The two
  second heartbeat in the same log shows gaps up to 21 s during the burst, so
  the board's tick is not keeping wall time under this load.  Rates are
  computed over the interval that actually elapsed (the client's line does
  that on its own), so they are not inflated by the armed length; what is not
  explained is why the timer fires late.
- `refusals=` sits at about 29k per boot.  That is the seam refusing a data
  frame (pool spare, in-flight cap or USB backlog) with the bridge holding it
  and re-offering it on the next completion (`wlan_stashok` is the count of
  held frames the seam later took).  It is backpressure; the client's own
  `got/srv_sent` line is what says whether anything was lost.

Known residue after such a window: the driver's transmit-report queue keeps
about 64 frames, visible as one `skbq` line whose address is one of the
transmit engine's own queue heads rather than a heap address.  The next cell
starts with empty transmit queues and is unaffected, so the residue is recorded
rather than gated.  Separately, one or two register accesses per boot return `-110`
(`read register 0x770`, `write register 0xaa`); no window was stopped by one.
