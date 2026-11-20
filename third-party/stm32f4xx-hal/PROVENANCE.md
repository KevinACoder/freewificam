# third-party/stm32f4xx-hal - provenance

| Item | Value |
|---|---|
| Upstream | STMicroelectronics **STM32F4xx HAL driver 1.7.3** (F4 HAL, BSD-3-Clause) and the CMSIS headers it builds against: **CMSIS device F4 2.6.2** and the **CMSIS core 4.30** Cortex-M headers. The bytes are those shipped in the vendor's F429 lwIP example tree, which carries the ST releases unmodified |
| Source | `working directory D:\stm32` (`/mnt/d/stm32`), `野火【STM32F429开发板-挑战者】资料(1)/1-程序源码_教程文档/8-[野火]《LwIP应用开发实战指南》/1-书籍配套例程/ebf_stm32f429_tiaozhanzhe_lwip_code.zip` (sha256 `64ae87418612e3940862d6ea2ac22cba319296fd12fc41fcbf4938743f3b88c9`), paths `tutorial_code/lwip_dhcp/Libraries/{CMSIS,STM32F4xx_HAL_Driver}/…`. The external reference tree is read-only and is not a build input |
| Pin | no fetchable git ref: the vendor zip is the anchor. Every kept file's sha256 is in `MANIFEST.sha256` (paths relative to this directory) |
| License | BSD-3-Clause for the HAL and the CMSIS device headers (STMicroelectronics), BSD-3-Clause for the CMSIS core headers (ARM). Every kept file carries its own copyright and licence block in its header; see `docs/LICENSES.md` |
| Vendoring | vendored snapshot, byte-identical to the source zip for every kept file: **16 files**, contract = `MANIFEST.sha256`. No patch, no line-ending change |
| Fetched | 2026-11-20 from the zip named above |
| Local differences | none (zero patch): no kept file was edited |
| Compiled set | `STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_eth.c` only. The rest of the tree is the headers that closure includes (the device/core headers, the HAL module headers, and `Legacy/stm32_hal_legacy.h`, which `stm32f4xx_hal_def.h` pulls in), plus `stm32f4xx_hal_rcc.c`, which is carried but **not compiled**: the ETH driver's one RCC call is `HAL_RCC_GetHCLKFreq`, which the board glue answers from `SystemCoreClock`; compiling the module would drag the FLASH module in for clock-config code this image never runs |
| Consumers | `tests/board/rtos/platform/board/stm32f429-challenger/eth.c` (the board glue, the only translation unit that sees the HAL) and `…/stm32f4xx_hal_conf.h` (this project's HAL configuration, first-party, not part of this tree) |
| Reason | the F429 wired-NIC line takes the MAC/PHY/DMA driver from ST rather than writing a second one: the HAL's ETH driver is the vendor-proven implementation of the STM32F4 ETH peripheral (descriptor rings, MDIO, auto-negotiation, MAC/DMA configuration) and is imported verbatim. The first-party code is limited to the board seam (pins, clocks, frame-level API) and the lwIP netif bridge |

## The import is a closure, not the whole package

The source zip carries 190 HAL `Inc`/`Src` files. This tree keeps the **compiled
closure of `stm32f4xx_hal_eth.c`**: its include trace under
`platform/board/stm32f429-challenger/stm32f4xx_hal_conf.h`, plus the module
headers that trace names. Adding another
peripheral is a manifest extension, not a re-download: copy that module's `Inc`
and `Src` files from the same zip, add their rows to `MANIFEST.sha256`, and name
the unit in the profile's build fragment.

The same rule explains the CMSIS core subset: only the GCC branch's files are
kept (`core_cm4.h` and the three `core_cm*.h` it includes, plus `cmsis_gcc.h`).
The ARMCC/IAR branches of those headers are `#include`d conditionally and are
not part of this build's closure.

## Not imported

| Not imported | Why |
|---|---|
| `stm32f4xx_hal.c` | it owns `HAL_Init`/`HAL_GetTick`/`HAL_Delay`/SysTick; the FreeRTOS port owns SysTick on this board, so the time base is supplied by the board glue instead (`eth.c`) |
| `system_stm32f4xx.c` | the board's `clock.c` already sets `SystemCoreClock` and defines `SystemInit`; the device header's declarations are satisfied by those |
| the rest of the HAL modules | outside the ETH closure; the F429 platform's other peripherals are first-party register code |
