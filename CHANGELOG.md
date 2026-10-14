# Changelog

All notable changes to this project are recorded here. The format follows the
spirit of Keep a Changelog; versions are the project's own.

## Unreleased

Landed after the `0.1.0` tag, on the reference board:

- **USB camera (UVC) line.** A Logitech Pro 9000 is enumerated, opened and
  streams MJPEG 640x480 over its isochronous endpoint (about 13.5 fps,
  1.9 Mbit/s over cold boots). Two pieces of the vendored USB stack that its
  open-source tier does not carry travel as patches to it: the high-speed
  isochronous transfer engine, and the video class's running-stream half.
- **PCIe wireless line.** `iwlwifi` (Intel Wireless-AC 7260), full chain over
  GICv3 message interrupts.
- **PCIe storage line.** NVMe over the PCIe bus with an exFAT filesystem on it.
- **SDIO wireless line.** RTL8189FTV over the SDIO bus (vendor driver with its
  own MLME).
- **A single console log level mechanism** over a vendored engine.

## 0.1.0 - 2026-10-11

First tagged snapshot. A portable RTOS WiFi stack (net80211 + Linux drivers +
supplicant) running on ThreadX/FreeRTOS over CMSIS-RTOS2, with three USB WiFi
lines brought up on the reference board.

See [docs/RELEASE-NOTES-0.1.md](docs/RELEASE-NOTES-0.1.md) for the release
notes: positioning, supported hardware and the state of each line, known
limitations, and the license.

Highlights:

- Portable net80211 with a single CMSIS-RTOS2 osal/bus/net port layer.
- Three USB WiFi driver lines ported to the RTOS and brought up on hardware:
  RTL8188EUS (rtl8xxxu), RTL8821CU (rtw88), RTL8851BU (rtw89, experimental).
- A dual-kernel host mock tier (Gate1) and a board regression tier (Gate2).
