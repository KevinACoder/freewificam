# Changelog

All notable changes to this project are recorded here. The format follows the
spirit of Keep a Changelog; versions are the project's own.

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

In progress toward the next tag (not in this one): a single console log level
mechanism over a vendored engine.
