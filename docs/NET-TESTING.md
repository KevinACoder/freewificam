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

## 3. Build and boot

The profile is selected in `tests/board/rtos/config.mk`:

| Profile | What it tests |
|---|---|
| `net-netio-raw-tx256` | UDP 1440 B throughput; raw UDP consumer, TX ring 256 (the default profile) |
| `net-pps` | 64 B pps; serial-gated receiver for a zero-loss window |

Build with `tools/rtos/build.sh` (prints size, sha256, build tag, elapsed time).
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

- Board runs the netio raw server; the host runs the netio client, 10 s per
  direction, 2/2.
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

A UDP generator that can hold the requested rate for 10 s: two threads,
non-blocking sockets, no sleeping pacer, 256-frame batches.  A sleeping pacer
changes loss at the same mean rate (measured: 5.9% vs 0.001% at 190k pps), so
the sender shape is part of the test definition.

### 6.3 Gates

Requested vs offered within 2%; offered vs the wire-side count within 1%;
window from the sender's own duration; closure within 0.1%.

### 6.4 Procedure

1. Set the frequency.
2. If RX coalescing is used, enable it at runtime **after** the frequency
   change (`riwt 160`), not at boot.
3. Open the receive window on the board.
4. Start the sender.
5. Stop the window when the sender ends.

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
- TCP is not accepted yet.
- The RX ring is 128 descriptors; a deeper ring is untested for pps.  A
  256-descriptor ring measured -3% in the 1440 B path earlier, so it is not
  assumed to be free.
- The sender tops out near 350-375k pps on this bench; numbers above that are
  lower bounds, not platform limits.
