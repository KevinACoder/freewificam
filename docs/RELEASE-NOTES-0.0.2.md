# Release notes - 0.0.2

## What this is

An open-source WiFi stack for RTOS kernels, plus the Linux-side module line
that shares its code. The mainline Linux SoftMAC drivers are taken unchanged
and the 802.11 stack, the bus engines and the OS surface are supplied
underneath them, so an ordinary USB or SDIO WiFi module can be used on an RTOS
the way it is used on Linux. The same code is built as a loadable module for
Linux, which is what makes the port checkable: the two builds run the same
driver bytes against the same 802.11 stack.

This release is the first one cut from this repository. It is a structural
release as much as a feature release: the code moved to a per-component shape
(source + `ports/` + `patches/`), the host mock tier and the build outputs were
left out, and the camera line - the one new functional line - lands with it.

## What is in the box

- The 802.11 core (a NetBSD-shaped net80211 import) with one port layer for the
  OS, the bus and the network presentation.
- The Linux SoftMAC drivers, unchanged, with the RTOS-facing interface supplied
  underneath them.
- A USB host stack (CherryUSB with the board port: xHCI, EHCI, hub, mass
  storage, video class, isochronous engine) and an SDIO/SDMMC host stack.
- Two RTOS kernels behind one CMSIS-RTOS2 abstraction: ThreadX 6.5.1 and
  FreeRTOS 11.3.1.
- A Linux kernel-module line: one module per driver arm, with net80211 in
  place of mac80211.
- A host conformance tier, a board round entry and a judge that reads only the
  captured serial log.
- Release documents: `VERSION`, `CHANGELOG.md`, `LICENSE`, `LICENSES.md`,
  `docs/RELEASE.md`, `docs/MIGRATION-FROM-0.1.0.md` and `known_issues/`.

## Supported hardware and line status

The reference board is an RK3568 ITX board (64-bit ARM, two EHCI USB2 roots,
two xHCI USB3 instances whose SuperSpeed lanes are routed to SATA, one SDIO
slot, one PCIe x1 and one PCIe x2 slot).

| Line | Bus | State in this release |
|---|---|---|
| `rtw89` / RTL8851BU | USB2, USB3 | Linux module: attach, firmware, scan, join and data measured; WPA2 measured on a controlled access point, with an intermittent first join. RTOS: bring-up, scan, join and data on an EHCI root (20 KiB receive URBs), both kernels. |
| `rtl8xxxu` / RTL8188EUS | USB2 | Linux module: attach, scan, join, ping and TCP/UDP measured. RTOS: the full ladder (bring-up, scan, join, raw data, lwIP netif with iperf), both kernels. |
| `rtl8xxxu` / RTL8723BU | USB2 | Same driver as the 8188EUS; the part is a three-interface composite device with WiFi on interface 2 and a BT half that this repository does not drive. Linux module and RTOS ladders measured on an EHCI root. |
| `rtw88` / RTL8821CU | USB2 | Linux module: attach, firmware, scan, join, ping, TCP/UDP measured. No RTOS image in this release. |
| `iwlwifi` / Intel AC 7260 | PCIe | Linux module: attach, firmware, scan, join and ping measured; TCP does not flow (see known issues). RTOS: scan, join, dataplane and lwIP netif measured on both kernels. |
| vendor `rtl8189fs` / RTL8189FTV | SDIO | The vendor driver with its own MLME over the SDIO host stack: scan, join, ping and a 30 s TCP downlink measured, both kernels. |
| USB camera (UVC) | USB2 | New in this release: an isochronous engine, the video class's streaming half, an application half and four cases. One camera model measured; see below. |

"Measured" means a board round was run and judged on a clean boot; the numbers
below say what was measured. It does not mean that every mode of the part was
exercised - see "What is not verified".

## Measured results

Each row names its own scenario. The rows come from different rounds with
different peer builds and access points; they are not one benchmark run and are
not directly comparable with each other. The Linux column is the same card, on
the same board and the same access point, measured with the same instrument
where a comparison was made.

| Line | Scenario | This project | Same card under Linux |
|---|---|---|---|
| RTL8189FTV (SDIO) | scan, open join, 300 ping; 30 s TCP downlink | both kernels 300/300 ping; ThreadX downlink 7.70 / 7.55 Mbit/s with no stall; FreeRTOS downlink stops after about 1 MB (known issue) | 300 ping 0/300; the same 30 s TCP also stalls in the Linux build (about 15-16 s, 73.8 / 74.6 MB), so the stall is not RTOS-specific |
| RTL8188EUS (USB) | ladder, then lwIP netif and 30 s iperf2 | both kernels 5/5 arms on 2/2 clean boots; ping 19-20/20; downlink 10.1 / 12.0 Mbit/s (FreeRTOS), 9.73 / 9.90 (ThreadX); uplink 0.11 Mbit/s (both) | downlink 19.0-20.8, uplink 13.0-13.5; 300 ping 298-300/300 |
| RTL8723BU (USB) | ladder, then lwIP netif and 30 s iperf2 | both kernels 5/5 arms on 2/2 clean boots; ping 20/20; downlink 9.30 / 2.06 (FreeRTOS) and 10.16 / 13.58 (ThreadX); uplink 4.94 / 4.82 (FreeRTOS) and 2.81 / 4.03 (ThreadX) | downlink 29.6 / 36.9, uplink 10.3 / 12.9; 300 ping 300/300 and 298/300 |
| RTL8851BU (USB) | EHCI bring-up, scan, open join, data (20 KiB receive URBs) | both kernels: bring-up 2/2, scan 2/2, join 2/2, data 2/2 (ping 10/10), ThreadX 2/2 | not measured for this arm |
| AC 7260 (PCIe, RTOS) | scan, join, raw dataplane, lwIP netif and iperf2 | both kernels 2/2; ping 9-10/11; downlink 9.7-10.4 Mbit/s, uplink 0.26-0.38 | downlink 51.3-52.1, uplink 33.1 |
| Platform (no wireless) | four-case image: tick, CMSIS-RTOS2 conformance, GMAC/lwIP, eMMC | both kernels 2/2 clean boots, 4/4 cases; 30 s TCP 507 / 521 Mbit/s downlink and 682 / 840 uplink; UDP 962 Mbit/s with no loss; eMMC 38.4 MB/s both directions | this board's Linux GMAC measures 941 / 943 Mbit/s |
| `rtw88` / RTL8821CU (Linux module) | attach, firmware, scan, open join, data | attach/firmware/scan/join 3/3; ping 5/5; TCP 12.2-12.4 Mbit/s uplink and 14.6-14.7 downlink; UDP at 20 Mbit/s offered with no loss | not measured for this arm |
| `rtl8xxxu` / RTL8188EUS (Linux module) | 2 cold boots plus 3 module reloads | scan, join, ping 5/5; TCP 8.18-8.47 Mbit/s downlink and 10.2-12.1 uplink; UDP at 20 Mbit/s offered, 1 lost of 12202 | not measured for this arm |
| `rtl8xxxu` / RTL8723BU (Linux module) | 3 clean boots, native-vs-module comparison | 3/3 clean boots; the module's and the built-in driver's command streams differ in 0 places on two independent captures | the built-in driver is the reference arm |
| `iwlwifi` / AC 7260 (Linux module) | join, ping, iperf3 | join and ping pass 2/2; iperf3 carries 0 bytes in both directions (known issue) | not measured for this arm |
| `rtw89` / RTL8851BU (Linux module, WPA2) | controlled access point, four-way handshake, ping, iperf3 | 2/2 clean boots with the handshake complete; ping 20/19; iperf3 uplink 90.4 / 88.8 Mbit/s, downlink 1.68 / 1.55 | not measured for this arm |

## The camera line

A Logitech Pro 9000 (`046d:0990`) on the board's first USB2 root:
640x480 MJPEG, alternate setting 4 (640-byte isochronous transactions). Four
clean boots, two per kernel, certified at the release commit:

| Kernel | Frames in the 20 s window | Rate | Payload rate |
|---|---|---|---|
| FreeRTOS | 236 / 262 | 11.8 / 13.1 fps | 1853 / 2052 kbit/s |
| ThreadX | 257 / 266 | 12.8 / 13.3 fps | 2011 / 2095 kbit/s |

Every boot passed all four cases: the camera was identified on the controller,
the open negotiated the configuration, all 16 raw isochronous URBs completed
without packet errors, and the stream window delivered frames with at most one
frame dropped and no transport errors.

What is not shown: one camera model, one resolution, one alternate setting; no
long unattended run; no hot-plug; and the failure model of a stream *stop* is
not understood well enough to trust it (see the camera entries in
`known_issues/v0.0.2.md`).

## What is not verified

- **Encrypted networks on the RTOS images.** No supplicant is carried in this
  repository, so the RTOS lines were measured against open access points only.
  WPA2 was measured on the Linux module line, where the host's own supplicant
  drives the interface.
- **Access-point mode, power save, 40 MHz/HT and 5 GHz** on the RTOS images.
- **Long unattended runs** on any line.
- **Other camera models** and the camera's other resolutions and formats.
- **Other boards.** All hardware results are from one RK3568 ITX board.

## Certified images

The numbers above were measured on images built from commit `ef4f77d` on a
clean tree, tag `v0.0.2` on top of it:

| Kernel | Size | sha256 |
|---|---|---|
| FreeRTOS | 116,488 | `1fa2d1a6d235cb2948f9cfa56ac681904cb26de4645100f66aae8f57666a0848` |
| ThreadX | 125,528 | `dab1d3fd54f2c116547a4a1dd7d2e08f62c0b1f5ff6abaa3862952a9a9b30e12` |

The build banner embeds `git describe`; rebuilding the tag changes those bytes
and nothing else (see `docs/RELEASE.md`).

## Known issues

`known_issues/v0.0.2.md` lists what this release is known to get wrong, with
the impact, the workaround where one exists, and how each was observed.

## License

First-party code is BSD-3-Clause (`LICENSE`). Imported units keep their own
terms; the registry is `LICENSES.md`, and the firmware files' notices are in
`docs/FIRMWARE-NOTICES.md`.
