# rtos/ - where these files come from

The RTOS bin platform is imported, not re-invented.  Every file under `rtos/`
except the application layer and the few additions listed below is a verbatim
copy of the RTOS reference tree at commit `19c5736`, taken with
`git archive` (the reference tree's working copy carries unrelated work in
progress, so only committed bytes were imported).

| Directory | Origin (reference path) | State |
|---|---|---|
| `arch/aarch64/` | `arch/aarch64/` | byte-identical (17 files) |
| `board/rk3568-itx/` | `board/rk3568-itx/` | byte-identical subset: console, MMIO, reset, clock parameters, the eMMC (dwcmshc) and GMAC (dwc_eqos + RK3568 glue + RTL8211F) drivers.  The PCIe, NVMe, USB and CherryUSB board files are not carried. |
| `os/baremetal/` | `os/baremetal/` | byte-identical |
| `os/cmsis2/` | `os/cmsis2/` | byte-identical (facade plus the host and board stand-ins for the CMSIS Core headers) |
| `os/freertos/` | `os/freertos/` | byte-identical except that the board's USB configuration variant is not carried |
| `os/threadx/` | `os/threadx/` | byte-identical plus `cmsis2_event.c` (new, see below) |
| `components/printf/` | `components/printf/` | byte-identical, carries its own `PROVENANCE.md` and `MANIFEST.sha256` |
| `components/ulog/` | `components/ulog/` | byte-identical, carries its own provenance |
| `components/tlsf/` | `components/tlsf/` | byte-identical, carries its own provenance |
| `components/fsl_sdmmc/` | `components/fsl_sdmmc/` | byte-identical (the imported NXP card stack), carries its own `PROVENANCE.md` and `MANIFEST.sha256` |
| `ports/lwip/` | `ports/lwip/` | byte-identical subset: the CMSIS-RTOS2 `sys_arch`, the platform hooks, the GMAC netif bridge and their headers.  The wireless (`wlannetif*`, `lkpinetif*`, `wlan_tcp_trace`) units are not carried; the linuxkpi header they reach for is a different line's component. |
| `ports/fsl_sdmmc/` | `ports/fsl_sdmmc/` | byte-identical subset: the OS abstraction, the MMC card service, the dwcmshc host glue and their shadows.  The SDIO and SD paths are not carried this round. |
| `app/` | new | the case runner and the four cases (tick, cmsis2, net, emmc), written for this repository |
| `tests/cmsis2-host/` | new | the host tier's entry point (the same conformance case, the kernels' own host ports underneath) |
| `build/Makefile` | new (shape derived from the reference board Makefiles) | the two board images and the host tier |

## Additions to imported files

Two additions are ours and are recorded here because they change bytes of the
imported tree:

1. `os/threadx/cmsis2_event.c` (new file, not an edit): the CMSIS-RTOS2
   event-flag group API over ThreadX event-flag groups.  The reference backend
   exposes the per-thread flag API only, and that API answers through
   `osThreadGetId()`, which the ThreadX Linux userspace port cannot answer (it
   does not publish the running thread) - so the group API is the portable
   shape, and it is also the shape a producer/consumer pair wants.
2. `ports/fsl_sdmmc/sdmmc_mmc_card.c` and `.h`: one **additive** accessor,
   `sdmmc_mmc_id_get()`, which exposes the card's CID/CSD/extended-CSD fields
   as the imported stack parsed them.  The stack keeps the decoded structs and
   not the raw register images, and the round's acceptance compares those
   fields against the Linux side's readback.  No existing function changed.

The FreeRTOS board's heap face (`os/freertos/board/rk3568-itx/cmsis2_memory_board.c`)
is also new: the reference board images never linked the storage stack on the
FreeRTOS backend, so the facade's allocator pair had no board implementation
for it.

## Include root

The imported files reach each other with root-anchored includes
(`#include "os/cmsis2/cmsis2.h"`, `"arch/aarch64/..."`, `"components/..."`).
The build passes `-I$(ROOT)/rtos`, so those spellings resolve inside this
directory without editing a single imported line.  New files under `rtos/`
follow the same convention.