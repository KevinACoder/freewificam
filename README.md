# freewificam

**English** | [中文](README_zh.md)

A portable RTOS WiFi stack: the 802.11 core, the Linux SoftMAC drivers and a
supplicant, running on an RTOS kernel through a single abstraction.

**Plug a commodity WiFi card into an RTOS and use it.** That is the point. An
off-the-shelf USB WiFi dongle - the same one you would plug into a PC - with no
vendor SDK and no vendor blob: the mainline Linux driver, taken unchanged,
running on ThreadX or FreeRTOS.

**An open-source port of the mainline Linux SoftMAC drivers to an RTOS.** The
rtw88 and rtw89 drivers are the mainline Linux drivers for a large family of
Realtek WiFi chips; iwlwifi (the mvm generation) is Intel's mainline driver for
the Wireless-AC family. To our knowledge these are the first open-source ports
of this driver set to an RTOS: the driver bytes are unchanged, and the
RTOS-facing interface is provided underneath them.

The end goal is a camera whose data streams over WiFi for long, unattended runs
- a wireless IP camera on an RTOS. Both halves are in the tree: the wireless
side is brought up on hardware, and the USB camera line captures and streams
frames on the same board.

## Why this project

Open-source 802.11 support on RTOS kernels has, in practice, been tied to
vendor SDKs and binary blobs, without the reusable, vendored-and-ported stack
that BSD and Linux enjoy. Contributing that missing piece - a portable 802.11
stack with per-OS ports that other RTOS projects can adopt - is the point of
this project.

The stack is deliberately not tied to one board. The components, the ports and
the contract tests must not assume an MMU, 64-bit or a large memory, so the
same code can run on a small MCU and on a 64-bit application processor. Two
kernels are supported (ThreadX and FreeRTOS) behind one CMSIS-RTOS2
abstraction, and the abstraction is enforced: the components must build and
link on both, and a gate rejects a foreign kernel call in a component.

## What runs today

Three USB WiFi lines, one SDIO line, one PCIe line and the board's own
gigabit Ethernet are brought up on the reference board, and the USB camera
line streams frames on the same board.
"Full chain" means the card is enumerated, its firmware loaded, it scans, it
joins a WPA2 network, it gets an address over DHCP, it answers ping, and TCP
data flows. It does not mean the throughput is good.

| Line | Chip | Board state |
|---|---|---|
| `rtl8xxxu` | RTL8188EUS | Full chain. |
| `rtw88` | RTL8821CU | Full chain, including a boot-time CD-ROM to WLAN mode switch. |
| `rtw89` | RTL8851BU | **Experimental**: full chain reached, join intermittent, link poor. |
| SDIO | RTL8189FTV | Full chain, over the SDIO bus rather than USB: the vendor driver carries its own MLME and talks to the port's cfg80211 and netdev faces. **It also runs as an open access point**: a station associates to the board, and both directions measure (see [Throughput](#throughput)). |
| `iwlwifi` | Intel Wireless-AC 7260 | Full chain, over PCIe message interrupts (GICv3 ITS): the driver routes station management and data frames through its transmit queues, and the port owns the two delivery contexts a threaded interrupt needs. |
| GMAC (wired) | RK3568 MAC + RTL8211F PHY | Full chain: DHCP, ping and a single TCP stream in both directions against a Raspberry Pi peer over the on-board RJ45 - the closest line to Linux on this board (see [Throughput](#throughput)). |
| UVC | Logitech Pro 9000 | Camera enumerated, opened and streaming MJPEG 640x480 over its isochronous endpoint (see [The camera line](#the-camera-line)). |

The board, the connectors and pin/PHY multiplexing behind these lines, and which
port each line is wired to, are documented per board under
[docs/boards/](docs/boards/rk3568-e4ap5g1-itx.md).

### Throughput

Single TCP stream, measured at the receiver, against the same access point.
Throughput baselines use an **open (unencrypted) access point**: the software
cipher's per-frame cost is then out of the measurement. The keyed L0 arm is run
separately when that cost is the subject, and the two are not mixed. Channel
width drifts across boots on this access point, so the figures below name the
width they were taken at, and the RTOS and Linux cells of a row are the same
width.

| Line | RTOS, down (host to board) | RTOS, up (board to host) | Linux, same card, same channel |
|---|---|---|---|
| RTL8188EUS | 9.17 (open) / 9.19 (CCMP) Mbit/s | **9.08-10.17 (open) / 5.6-7.7 (CCMP)** Mbit/s | **9.12 down / 10.48 up** Mbit/s |
| RTL8821CU | 3.65 Mbit/s | not measured | **63.7 / 66.0 / 68.2 Mbit/s** |
| RTL8851BU | 136 / 173 Kbit/s | not measured | **23.9 / 25.3 / 26.5 Mbit/s** |
| RTL8189FTV (SDIO) | **18.1 (HT20) / 20.5 (HT40)** Mbit/s | **15.1-16.8 (HT20)** Mbit/s | **31.4 down / 30.7 up (HT20)**, 44.2 / 50.5 (HT40) |
| Intel Wireless-AC 7260 (PCIe) | 0.24 Mbit/s | not measured | **167 Mbit/s** |

The RTL8188EUS uplink is the clearest result of the controlled-variable method
so far. On the keyed L0 link the board's software CCMP costs **464-480 us per
transmitted 1460-byte segment**, measured with an identity-matched per-segment
account (sequence number and payload length; 100% match, zero misses); every
other transmit stage - 802.11 encapsulation, the driver seam, the USB submit -
stays below the 16 us bucket. Running the same image and sheet against an open
access point removes the cipher: the per-segment handoff falls to below 16 us
and the uplink rises from 5.6-7.7 to **9.08-10.17 Mbit/s** across two boots (per-window 8.18-11.13).
The software cipher is therefore a measured, attributable cost rather than an
unexplained gap. Closing it - a faster AES backend, the card's hardware crypto,
or an optimised build - is open work; the `-O2` build tried so far does not pass
the board suite's join guard and is not adopted.

All lines are measured against the same access point, BSSID
`14:a3:2f:18:07:2c`. Each Linux cell is min/avg/max over three 30 s runs (the
7260's is a single 20 s run). The RTL8189FTV line is the one that has been
worked on: its receive path was measured first and two levers were found and
adopted - a pump cadence that keeps serving a busy card (+45%) and the card's
own receive-aggregation timeout byte (+42% at HT20) - which moved it from one
fourth of Linux to **about 1/1.7 (down) and 1/1.9 (up)**, the closest of the
wireless lines. Its reverse direction was measured for the first time on the same rig
(board as the iperf client), so the "not measured" cells above are per-line
facts, not a blanket one. Its access point mode, brought up later, measures
19.8-20.2 Mbit/s down and 18.3 Mbit/s up with a station associated to the board
itself. A later round moved to a controlled bench of the project's own - a
Raspberry Pi access point, so the peer's behaviour is ours to pin - where the
downlink measures 29-30 Mbit/s against 43.8 for the same card under Linux
(0.67x). That bench also located the remaining wall: the card's delivery
cadence, about eight frames every 2.9 ms, interlocked with the access point's
16-frame block-ack window - the air was never the constraint (MCS7 150 Mbit/s,
zero retries). Two stack-shape levers were measured on it and rejected with
board evidence (running the stack's input on the caller's thread, and a
smaller card-side aggregation length threshold). The other lines' gaps are as
recorded: one eighteenth (8821CU), one hundred-and-seventieth (8851BU), one
seven-hundredth (7260); the 8188EUS line is about 1/1.03-1/1.15 on the open baseline
and 1/1.4-1/1.9 on the keyed arm, the difference being the cipher cost above.

The gap to Linux is still large on most lines, and it is the current open
problem. It is stated here rather than hidden: the value of the port so far is
in the structure, not in the numbers. See
[docs/known-limitations.md](docs/known-limitations.md).

### Wired Ethernet (GMAC)

The board's own gigabit MAC (the RK3568 GMAC behind an RTL8211F PHY), measured
as a single TCP stream between the board and a Raspberry Pi peer over the
on-board RJ45. Same board under Linux, same peer: **940 Mbit/s up / 941 Mbit/s
down**. UDP through the same driver measures 955 down / 957 up, which is what
the wire path itself carries at this frame size.

| Direction | RTOS, four sheets in one boot (2026-10-20) | Linux, same board and peer |
|---|---|---|
| Up (board to peer) | **894.5 - 919.2, median 900.0 Mbit/s** | 940 Mbit/s |
| Down (peer to board) | 684.0 - 760.6, median 742.4 Mbit/s | 941 Mbit/s |

The uplink sits at 95.7% of the Linux build and is the first figure on this
board at or above the line's own 900 Mbit/s acceptance bar; the downlink is at
79% and is the open half. Two changes carried it here. An interrupt census
classified every interrupt entry by its raw status word and retired a wrong
hypothesis with it: the "odd" interrupt family that timing had charged 5% of
the core measured 0.1-0.2%, while the entry cost that matters is 1.9 us
overall. And the C library now comes from the toolchain's newlib instead of
the image's own copy of it - the hand-written 1514-byte memcpy moved one
8-byte word per loop iteration and cost 1.5 us per frame, about 13% of the
single core at line rate, where newlib's size-branched LDP/STP implementation
moves 16 bytes per instruction. The swap now covers every board image: the
image-local C library unit was retired tree-wide.

### The camera line

The USB camera (UVC) line is the second headline milestone, and it streams on
the reference board. A Logitech Pro 9000 on the board's USB 2.0 host: the
camera is enumerated, the probe/commit exchange runs, an alternate setting is
selected, and MJPEG 640x480 frames come out of an isochronous URB ring and are
assembled by payload-header boundaries - **267 to 275 frames per 20-second
window over cold boots, about 13.5 fps and 1.9 Mbit/s**. The same camera and
board under Linux gave the ground truth in the dim scene it was captured in
(4.69 fps, 372 kbit/s); the acceptance floor is two thirds of that, and the
stream runs several times above it.

Two pieces of the vendored USB stack needed filling in, and each travels as a
patch to it, written to be sendable upstream: the high-speed isochronous
transfer engine the stack declares but does not define, and the running-stream
half of its video class - the streaming engine (the alternate-setting choice
from the device's own answer, payload-header frame assembly, the frame pool) is
not in the open-source tier. See
[patches/cherryusb/](patches/cherryusb/README.md), and
[docs/known-limitations.md](docs/known-limitations.md) for what is measured and
what is not.

## How it is built

```
  app/                    entry per OS and flavour
  os/                     RTOS ports and the CMSIS-RTOS2 abstraction
  arch/  board/           CPU layer and per-board BSP
  components/             drivers and protocol stacks
    net80211/             the 802.11 core (vendored)
    linuxkpi/             the Linux driver-facing interface
    skbuff/  tlsf/  ...   supporting units
  ports/                  first-party glue for the imported units
    net80211/             osal / bus / net port + the driver bridge
    cherryusb/  fsl_sdmmc/   bus backends (USB host, SDIO host)
    lwip/                 the network stack backend
  third-party/            pinned upstream trees (Linux drivers, RTOS kernels)
  patches/                diffs replayed onto the vendored trees at build time
  tests/host/  tests/board/   the two test tiers
```

The wireless core is the authority and the driver aligns to it. The bridge
presents the contract the Linux driver was written against; it is not relaxed
to accommodate a particular chip. Where a chip needs something the interface
normally supplies, the driver is given that state and the chip's own logic
does the rest. See [docs/net80211.md](docs/net80211.md).

## Build and test

The build is driven from the repository root:

```sh
make -C freewificam mock           # host mock suites (Gate1; seconds to minutes)
make -C freewificam gates          # static gates (local knowledge, build shape)
make -C freewificam board-images   # build every board image
```

A single suite: `freewificam/tools/mock/run.sh <suite>` (`--list` shows the
registry). The board tier is driven by `freewificam/tools/gates/run-gate2.sh`.
The air, from outside the board, is measured by `freewificam/tools/prober/`
(monitor, station, access point and traffic roles on a Linux host with a
monitor-capable USB card; see its README).
The two tiers and what they guarantee are described in
[docs/bus-and-threading.md](docs/bus-and-threading.md) and
[CONTRIBUTING.md](CONTRIBUTING.md).

## Related projects

This stack stands on other people's work and says so: the net80211 core
(NetBSD lineage), CherryUSB, CherrySH, lwIP, the ThreadX and FreeRTOS kernels,
CMSIS, and the mainline Linux drivers. Each is vendored or imported with its
license recorded in [LICENSES.md](LICENSES.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

First-party code is BSD-3-Clause (see [LICENSE](LICENSE)). Every vendored tree,
imported unit and binary blob keeps its upstream license, recorded component by
component in [LICENSES.md](LICENSES.md), including the distribution obligations
for the one copyleft unit in the tree.
