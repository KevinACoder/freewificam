# third-party/stm32f4xx-hal - provenance

| Item | Value |
|---|---|
| Upstream | STMicroelectronics STM32CubeF4 firmware package, `STM32Cube_FW_F4_V1.28.0` (2025-07-23) |
| Artifact | `stm32cube_fw_f4_v1283.zip`, SHA-256 `7105d44b0a1f017d28821b78357471311b4853272751e5692a828c459a09feaa` |
| License | HAL driver and device headers: BSD-3-Clause (`COPYING-BSD-3`, `COPYING-BSD-3-CMSIS`). CMSIS 5 core: Apache-2.0 (`COPYING-APACHE-2.0`). Package terms: `LICENSE-Package.md` |
| Vendoring | **partial, byte-identical**: 13 files, contract `MANIFEST.sha256` (sha256 per file). The package's other drivers, projects and middlewares are not imported |
| Fetched | 2026-11-16 |
| Local differences | none (zero patch) |
| Compiled set | `Src/stm32f4xx_ll_sdmmc.c`; the headers are include-only |
| First-party companion | `tests/board/rtos/platform/board/stm32f429-challenger/sdio/stm32f4xx_hal_conf.h` selects only the low-level face (device header + HAL definitions + LL SDMMC) and compiles the HAL asserts out |
| Consumers | the F429 SDIO host adapter (`sdmmc_host_stm32f4_sdio.c`) and the 8189FTV SDIO probe |
| Reason | The 8189FTV card needs an SDIO host on the F429's SDIO controller. The project decision was to port the ST HAL library's low-level SDMMC layer as-is rather than rewrite the register sequence. The high-level HAL SD memory-card driver (CMD8/ACMD41 enumeration) is deliberately **not** imported and is not used for the SDIO card; the card service stays in the fsl_sdmmc stack |

Notes:

* This is a **minimal LL subset**, not a CubeMX project. The vendor SD-card
  reference (`bsp_sdio_sd.c`, book example 36) is the source for the board
  choices (pins, 24 MHz, 4-bit bus, FIFO address, DMA stream); the register
  layer is the ST LL driver carried here.
* The package's `Drivers/CMSIS/Include` core is the M-profile CMSIS 5 core the
  F429 device header expects. The repo's `third-party/CMSIS_6` is the
  A-profile tree and is not used by these device headers.
* `MANIFEST.sha256` paths are relative to this directory.
