# tests/board/rtos - where these files come from

The RTOS bin platform is imported, not re-invented.  The line is split into
**platform port + build/app/tests + imported components**; every imported tree
follows the repository's source + ports + patches shape (see
`docs/PATCH-POLICY.md`).

## This line

| Directory | Origin (reference path) | State |
|---|---|---|
| `platform/arch/aarch64/` | `arch/aarch64/` | byte-identical subset (17 files) |
| `platform/board/rk3568-itx/` | `board/rk3568-itx/` | byte-identical subset: console, MMIO, reset, clock parameters, the eMMC (dwcmshc), the SDIO (dwmmc) and GMAC (dwceqos + RK3568 glue + RTL8211F) drivers. The PCIe, NVMe, USB and CherryUSB board files are not carried. |
| `platform/os/baremetal/` | `os/baremetal/` | byte-identical |
| `platform/os/cmsis2/` | `os/cmsis2/` | byte-identical (facade plus the host and board stand-ins for the CMSIS Core headers) |
| `platform/os/freertos/` | `os/freertos/` | byte-identical except the board's USB configuration variant, plus the heap-size knob noted below |
| `platform/os/threadx/` | `os/threadx/` | byte-identical plus `cmsis2_event.c` (new, see below) |
| `app/` | new | the case runner and the `tick`, `cmsis2`, `net`, `emmc`, `wlan` cases |
| `tests/cmsis2-host/` | new | the host tier's entry point |
| `build/` | new (shape derived from the reference board Makefiles) | the two board images, the host tier and `wlan.mk` |

Reference tree: `19c5736` (committed bytes, `git archive`). The reference
working copy is not used.

## Ports and imported components

| Path | Origin | State |
|---|---|---|
| `components/linuxkpi/rtos/` | `components/linuxkpi/` | runtime subset: core, census, platform target, cfg80211/netdev/osdep, SDIO engine/seam, skbuff pool, `include/` Linux face. Byte-identical except the recorded divergences; the USB/PCIe/mac80211/tracing/blob units are not carried. |
| `third-party/lwip/ports/` | `ports/lwip/` | the CMSIS-RTOS2 `sys_arch`, platform hooks, GMAC netif and the 8189FTV `lkpinetif*`; the import is already patched by `third-party/lwip/patches/` |
| `third-party/fsl_sdmmc/ports/` | `ports/fsl_sdmmc/` | OS abstraction, MMC card service, dwcmshc host glue, shadows, and the 8189FTV SDIO card/host/seam units |
| `components/net80211/ports/` | `ports/net80211/` | the net80211 port/bridge for the linux-ko line; the RTOS line does not use net80211 |

The components themselves live under `third-party/` (RTOS components) or
`wireless-drivers/` (drivers); `components/linuxkpi` and `components/net80211`
are first-party. Each carries its own `ports/` and `patches/`; patches are
applied in the tree and `patches/` records them.

## Additions to imported files

Recorded because they change bytes of the imported tree:

1. `platform/os/threadx/cmsis2_event.c` (new): CMSIS-RTOS2 event-flag group API
   over ThreadX event flags. The reference backend exposes only the per-thread
   flag API, and that answers through `osThreadGetId()`, which the ThreadX
   Linux userspace port cannot answer.
2. `third-party/fsl_sdmmc/ports/sdmmc_mmc_card.c` and `.h`: one additive
   accessor, `sdmmc_mmc_id_get()`, exposing the card's CID/CSD/extended-CSD
   fields as the imported stack parsed them.
3. `platform/os/freertos/board/rk3568-itx/cmsis2/FreeRTOSConfig.h`:
   `configTOTAL_HEAP_SIZE` honours `CONFIG_FREERTOS_HEAP_SIZE` (default
   unchanged at 256 KiB); the 8189FTV image needs the measured high-water.
4. `components/linuxkpi/rtos/linuxkpi_cfg80211.c` and `.h`: the explicit
   channel-list scan entry and the one-channel debug entry. The production
   `wlan` case covers the band with four <=4-channel requests, which the vendor
   duplicates 3x (300 ms per channel); a one-shot 14-channel request (100 ms
   per channel) missed the 100 ms-beacon AP on some clean boots. The
   one-channel entry is retained for diagnostics, not as the production
   workaround. The scan diagnostic showed that several arbitrary requests in
   one boot can return empty (scan-state settling), so production uses the
   fixed four-batch sequence and validates the AP in the final table. See the
   scan RUN for the measured result.
5. `platform/os/freertos/board/rk3568-itx/cmsis2_memory_board.c` (new): the
   FreeRTOS board's heap face; the reference board images never linked the
   storage stack on the FreeRTOS backend.

## Include root

The imported files reach each other with root-anchored includes
(`#include "os/cmsis2/cmsis2.h"`, `"arch/aarch64/..."`, `"board/..."`).  The
build passes `-I$(PLATFORM)` (and `-I$(LINE)` for the app/build headers), so
those spellings resolve inside the line without editing imported lines.

## 8189FTV vendor-stack port

The vendor driver is **not** in this repository. It is `rtl8189fs` at pin
`13bbdbc` (GPL-2.0, 131 translation units, embedded 20,306-byte firmware
array), consumed byte-identically from the external tree named by the build's
`VENDOR_8189FS`. The pin, licence and external-path note live in
`wireless-drivers/rtl8189fs/`; the vendor bytes are not edited.
