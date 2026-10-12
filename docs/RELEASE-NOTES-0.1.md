# Release notes - 0.1.0

## What this is

A portable RTOS WiFi stack: the 802.11 core, the Linux SoftMAC drivers and a
supplicant, running on an RTOS kernel through a single abstraction. The goal is
to make an ordinary, off-the-shelf WiFi card work on an RTOS the same way it
works on Linux.

Two things are worth stating plainly, because they are the point:

- **An open-source port of the rtw88 and rtw89 SoftMAC drivers to an RTOS.**
  The rtw88 and rtw89 drivers are the mainline Linux drivers for a large family
  of Realtek WiFi chips. To our knowledge this is the first open-source port of
  these two drivers to an RTOS, with the drivers taken unchanged and the
  RTOS-facing interface provided underneath them.
- **Plug in a commodity WiFi card and use it.** The card is not a lab part and
  no vendor SDK is involved: it is the same USB WiFi dongle you would plug into
  a PC.

The end goal is a camera whose data streams over WiFi for long, unattended
runs - a wireless IP camera on an RTOS. This snapshot covers the wireless side;
the camera side is not started.

## What is in the box

- A portable 802.11 core (a verbatim import of a NetBSD-shaped net80211) with a
  single port layer for the OS, the bus and the network presentation.
- Linux SoftMAC driver lines, taken unchanged, with the RTOS-facing interface
  supplied underneath them.
- A USB host stack (CherryUSB) with a first-party interface layer, and an
  SDIO/SDMMC host stack with a first-party interface layer.
- A supplicant port and an lwIP network stack.
- Two test tiers: a host mock tier that runs in seconds to minutes, and a board
  regression tier.
- Two RTOS kernels (ThreadX and FreeRTOS) behind one CMSIS-RTOS2 abstraction.

## Supported hardware and line status

Reference board: an RK3568 ITX board (64-bit ARM). Three USB WiFi lines and one
SDIO line are brought up on it. The status of each line is what has actually
been measured, not what is planned.

| Line | Chip | State |
|---|---|---|
| `rtl8xxxu` | RTL8188EUS | Full chain on hardware: probe, scan, WPA2, DHCP, ping, TCP data. |
| `rtw88` | RTL8821CU | Full chain on hardware, including a boot-time CD-ROM to WLAN mode switch. |
| `rtw89` | RTL8851BU | **Experimental.** Full chain reached, but join is intermittent and the link is poor. |
| SDIO | RTL8189FTV | Full chain on hardware, over the SDIO bus rather than USB. The vendor driver carries its own MLME and talks to the port's cfg80211 and netdev faces. |

The SDIO line and its figures below landed after the `0.1.0` tag was cut; the
tag itself predates it.

"Full chain" means the card is enumerated, its firmware loaded, it scans, it
joins a WPA2 network, it gets an address over DHCP, it answers ping, and TCP
data flows. It does not mean the throughput is good - see below.

## Throughput

These are board measurements, host to board, single TCP stream. The Linux column
is the same card under Linux on the same board, from a clean upstream build, on
the same channel and the same access point, so the two columns are comparable.
One caveat on width: Linux negotiates HT40 on this access point while the RTOS
port runs 20 MHz legacy, so the Linux figures carry a wider-channel advantage.
Older Linux numbers taken on a different band are **not** a valid comparison and
are not shown here.

| Line | RTOS (host to board) | Linux, same card, same channel |
|---|---|---|
| RTL8188EUS | 4.00 / 4.66 Mbit/s | 42.6 / 43.9 / 45.3 Mbit/s |
| RTL8821CU | 3.65 Mbit/s | 63.7 / 66.0 / 68.2 Mbit/s |
| RTL8851BU | 136 / 173 Kbit/s | 23.9 / 25.3 / 26.5 Mbit/s |
| RTL8189FTV (SDIO) | 10.6 / 1.18 / 10.9 Mbit/s | 45.0 / 46.4 Mbit/s |

All four rows are on channel 1 (2412 MHz) against the same access point, BSSID
`14:a3:2f:18:07:2c`; each Linux cell is min/avg/max over three 30 s runs, except
the SDIO row, whose Linux cell is the range the lab's ground-truth ledger records
for the same card. The RTOS figures are roughly one tenth, one eighteenth, one
hundred-and-seventieth and (for the SDIO line) one quarter of the Linux figures.
The SDIO line is the closest to Linux and the least stable: two of its three runs
sat at 10.6 and 10.9 Mbit/s while the third collapsed to 1.18 Mbit/s. Linux also
moves data board to host on all four lines; the RTOS reverse direction is not
measured yet.

The gap to Linux is large, and it is the current open problem. It is stated
here rather than hidden because the value of the port is in the structure, not
yet in the numbers.

## Known limitations

See [known-limitations.md](known-limitations.md). In short: the throughput is
far below Linux, the 8851BU line is experimental, long-run streaming is not
demonstrated, and the camera side has not started.

## License

First-party code is BSD-3-Clause (see [../LICENSE](../LICENSE)). Vendored trees,
imported units and binary blobs keep their upstream licenses, recorded
component by component in [../LICENSES.md](../LICENSES.md), including the
distribution obligations for the one copyleft unit in the tree.

## How to get it

This snapshot is tagged `0.1.0` in the repository. It is a local tag; nothing
has been pushed or published.
