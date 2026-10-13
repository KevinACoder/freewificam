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
- a wireless IP camera on an RTOS. This snapshot covers the wireless side; the
camera side is not started.

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

Three USB WiFi lines, one SDIO line and one PCIe line are brought up on the
reference board. "Full chain" means the card is enumerated, its firmware
loaded, it scans, it joins a WPA2 network, it gets an address over DHCP, it
answers ping, and TCP data flows. It does not mean the throughput is good.

| Line | Chip | Board state |
|---|---|---|
| `rtl8xxxu` | RTL8188EUS | Full chain. |
| `rtw88` | RTL8821CU | Full chain, including a boot-time CD-ROM to WLAN mode switch. |
| `rtw89` | RTL8851BU | **Experimental**: full chain reached, join intermittent, link poor. |
| SDIO | RTL8189FTV | Full chain, over the SDIO bus rather than USB: the vendor driver carries its own MLME and talks to the port's cfg80211 and netdev faces. |
| `iwlwifi` | Intel Wireless-AC 7260 | Full chain, over PCIe message interrupts (GICv3 ITS): the driver routes station management and data frames through its transmit queues, and the port owns the two delivery contexts a threaded interrupt needs. |

The board, the connectors and pin/PHY multiplexing behind these lines, and which
port each line is wired to, are documented per board under
[docs/boards/](docs/boards/rk3568-e4ap5g1-itx.md).

### Throughput

Host to board, single TCP stream, measured on the board. The Linux column is
the same card under Linux on the same board, from a clean upstream build, on the
same channel and the same access point. Note the channel width: Linux negotiates
HT40 on this access point while the RTOS port runs 20 MHz legacy, so the Linux
figures carry a wider-channel advantage. Older Linux figures taken on a different
band are not comparable and are not used here.

| Line | RTOS (host to board) | Linux, same card, same channel |
|---|---|---|
| RTL8188EUS | 4.00 / 4.66 Mbit/s | **42.6 / 43.9 / 45.3 Mbit/s** |
| RTL8821CU | 3.65 Mbit/s | **63.7 / 66.0 / 68.2 Mbit/s** |
| RTL8851BU | 136 / 173 Kbit/s | **23.9 / 25.3 / 26.5 Mbit/s** |
| RTL8189FTV (SDIO) | 10.6 / 1.18 / 10.9 Mbit/s | **45.0 / 46.4 Mbit/s** |
| Intel Wireless-AC 7260 (PCIe) | 0.24 Mbit/s | **167 Mbit/s** |

All five lines are measured on channel 1 (2412 MHz) against the same access
point, BSSID `14:a3:2f:18:07:2c`; each Linux cell is min/avg/max over three 30 s
runs, except RTL8189FTV whose Linux cell is the range the lab's ground-truth
ledger records for the same card, and the 7260 whose Linux cell is a single
20 s run of the same iperf session (the RTOS cell is a single 31 s run). The
RTOS figures are roughly one tenth, one eighteenth, one hundred-and-seventieth,
one quarter and (for the PCIe line) one seven-hundredth of the Linux figures.
The SDIO line is the closest to Linux, and the least stable:
two of its three runs sat at 10.6 and 10.9 Mbit/s while the third collapsed to
1.18 Mbit/s — the signature of the driver's default power management, the same
thing the Linux lane had to turn off to stop TCP stalling. Linux also moves data
in the reverse direction on all five lines; the RTOS reverse direction is not
measured yet.

The gap to Linux is large, and it is the current open problem. It is stated
here rather than hidden: the value of the port so far is in the structure, not
in the numbers. See [docs/known-limitations.md](docs/known-limitations.md).

### The camera line

The USB camera (UVC) line is the second headline milestone and is in progress.
It has no board evidence yet: the USB host stack in this tree does not include
an isochronous transfer path, and the camera class is not ported.

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
