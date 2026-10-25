# patches/lwip - index

The stack's per-frame cost is a call into `tcp_in.c` per segment, and no option in
`ports/lwip/include/lwipopts.h` changes what that call does. The patches in
`unadopted/` are real, measured, upstreamable changes - and **not carried**: on
this board each of them bought a per-frame saving that a receive-path limit took
back, so the shipped image keeps the pin's semantics byte-for-byte.

| Item | Value |
|---|---|
| Upstream | https://github.com/lwip-tcpip/lwip.git |
| Base (= pin) | `77dcd25a72509eb83f72b033d219b1d40cd8eb95` (tag `STABLE-2_2_1_RELEASE`) |
| Patch list | none carried; four parked under `unadopted/` (see below) |
| Replay | `tools/patches/replay.sh lwip` prints the vendored tree while this directory holds no `*.patch`; dropping one back in is the whole switch |

## Why nothing is carried (2026-10-25 round, `runs/L3-rtos/2026-10-25-gmac-parity`)

Measured on the board with the series applied (the three below plus 0006):

- downlink 484.4 -> 473.3 Mbit/s (neutral within boot-to-boot noise),
- uplink 651.8 -> 605.8 Mbit/s (first boot) and 548.9 (a later one), with
  `ovf` (MAC FIFO overflow) 0 -> 18,792 .. 316,170 in the uplink phase,
- per-ACK input cost 28.67 -> 25.57 us, i.e. the patch did make the path cheaper,
  and the throughput still fell: the limit is the ACK burst against a 128-slot
  receive ring, not the cost of handling one ACK.

That is the shape of a trade this board does not want: the patches lower the
per-frame price and the receive path turns it into loss. The next round's target
is the receive path (ring depth / moderation), after which these become worth
re-measuring together - which is why they are parked with their rationales intact
rather than deleted.

## 0001 - lwiperf: take plain payload at the 128 KiB boundary as data

`lwiperf_tcp_recv()` re-examines the stream every 128 KiB and, when the
`ANSWER_TEST` flag is set, requires the packet crossing the boundary to start with
a repeat of the 24-byte settings block. That repeat is lwiperf's own client's
behaviour; the iperf2 client never repeats it, so the first data packet after the
settings fails the comparison and the session closes with
`LWIPERF_TCP_ABORTED_LOCAL_DATAERROR`. The same branch also strips 24 bytes of
payload when the flag is *not* set, so a long single-direction transfer loses 24
bytes per crossing and reports 24 bytes more than it received.

The patch consumes the repeated block only when the packet really starts with one
and lets everything else fall through to the data path.

## 0003 - tcp: make the delayed-ACK watermark a build-time option

Add `LWIP_TCP_ACK_DELAY_SEGMENTS`: 2 keeps the historical behaviour
byte-for-byte, 1 acknowledges every segment immediately, and values above 2 hold
the ACK across that many segments. Non-default builds count in-order segments in a
new pcb field; the count restarts whenever `TF_ACK_DELAY` is set anew.

At `4` the downlink measured 484.4 -> 525-535 Mbit/s in three boots (with the code
generation changes that rode along), so the option itself does what it says.

## 0004 - tcp: cache the unsent queue's tail in the pcb

`tcp_write()` appends to the unsent queue by walking it from head to tail on every
call, and the walk length grows with the configured send buffer; the source itself
carries a `@todo` noting the fix. Keep the tail in the pcb (`last_unsent`),
maintained at every mutator of the queue. With the cache, `tcp_write` no longer
depends on the queue length.

## 0006 - lwiperf: disable Nagle on the TCP client

The client writes one MSS per `tcp_sent` callback; with Nagle on and a peer that
delayed-ACKs, that paces the measurement at one segment per RTT. The patch exists
because one line measured 0.4 Mbit/s that way with an empty transmit queue. On
this board the burst it creates is what the receive ring cannot absorb (uplink
548.9 -> 292.5 Mbit/s across the round's boots, `ovf` up to 316,170), so it stays
parked with the rest.
