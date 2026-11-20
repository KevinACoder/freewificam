# AP6181 (BCM43362) — Cypress WICED WWD, ported to the F429

This component is the **first-party port** of the module vendor's WICED wireless
driver (WWD) to this line's STM32F429 platform.  It is deliberately *not* part of
`components/liblinux80211`: the driver runs natively on FreeRTOS with lwIP as its
only network stack, and no Linux kernel surface (linuxkpi, mac80211, cfg80211) is
compiled in.  A profile that selects `CONFIG_WICED_AP6181` depends on
`!CONFIG_LIBLINUX80211`, and the image gate fails if a `lkpi_*` or `ieee80211_*`
symbol appears in the link.

## The Cypress source is not in this repository

The WWD driver core belongs to Cypress and is licensed for use with Cypress
integrated circuits only; it may not be redistributed.  The repository therefore
carries only the port.  The driver tree is materialized into `vendor/` from an
external checkout:

```bash
export WICED_ROOT=~/workspace/stm32_lab/os/wifi-ap6181-f429-code
tools/build/wiced-tree.py            # copy the pinned subset into vendor/
tools/build/wiced-tree.py --check    # verify the pin and every digest
```

`vendor/` is git-ignored; `vendor/MANIFEST.sha256` and `vendor/PIN.txt` are the
record of exactly which files the build compiled and against which WICED
version.  `PROVENANCE.md` states the pin, the licence and the firmware digest.

## What the port supplies

| File | WWD entry points it implements |
|---|---|
| `ports/f429/sdio_host.c` | the STM32F4 SDIO host: enumeration (CMD0/5/3/7), CMD52/53 byte and block transfers, DMA, the controller interrupt |
| `ports/f429/wwd_platform.c` | power/reset (PB13 = WL_REG_ON), MAC read, the OOB interrupt line, interrupt-context query |
| `ports/f429/wwd_rtos.c` | `host_rtos_*` over the FreeRTOS kernel: threads, queues, semaphores, mutexes, timers, millisecond time |
| `ports/f429/wwd_buffer.c` | the network buffer pool (`host_buffer_*`), internal SRAM, 4-byte aligned |
| `ports/f429/wwd_network.c` | the lwIP seam: transmit entry and the receive hand-off |
| `ports/f429/wiced_port_config.h` | the board's numbers and policy (clock, pool, thread bands, OOB) |

The board-side pin and rail glue lives with the board, in
`tests/board/rtos/platform/board/stm32f429-challenger/sdio_board.c`.

## Building

The component is selected by one Kconfig symbol and compiled in its own flag
world (`tests/board/rtos/build/wiced.mk`); the vendor units and the port units
share that world so a header change cannot be seen by only half of the driver.
A build without `WICED_ROOT` fails with one line that names the variable rather
than a missing-header error.

## Licence

The port files here are BSD-3-Clause (this repository's licence).  The
materialized Cypress tree keeps the WICED licence; an image that links it is for
use with Cypress ICs only and is not a redistributable artifact.  See
`docs/LICENSES.md`.