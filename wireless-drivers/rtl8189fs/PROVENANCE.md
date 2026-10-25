# wireless-drivers/rtl8189fs - provenance

| Item | Value |
|---|---|
| Upstream | vendor `rtl8189fs` driver (Realtek RTL8188F SDIO family), upstream mirror `https://github.com/KevinACoder/rtl8189ES_linux.git` (branch `rtl8189fs`) |
| Pin | commit `13bbdbc` (`13bbdbc750bfaa37ff47dff063edcc857fad0584`) |
| License | **GPL-2.0** - the driver is GPL-2.0; the embedded firmware array is part of the vendor driver and is **not** redistributed by this repository. |
| Vendoring | **vendored in-tree at the pin.** The 131 translation units and the embedded 20,306-byte firmware array are under `src/`; the build defaults to that directory and needs no external path. The source is GPL-2.0 (see `LICENSE`); the embedded firmware is part of the GPL-2.0 driver source, not a separately extracted blob. |
| Local differences | none. The RTOS build compiles `src/` unmodified; `ports/` is first-party. If a vendor defect must be fixed, the patch is recorded under `patches/` and applied to a build-local copy. |
| Consumers | the RTOS bin platform's `wlan` case (`components/linuxkpi/rtos`, `third-party/fsl_sdmmc/ports`, `third-party/lwip/ports`, `tests/board/rtos/`) and the L2 linux-ko vendor port under `ports/rtl8189f/`. |
| Reason | the RTL8189FTV is the project's SDIO FullMAC-style vendor-stack line: host MLME in the vendor driver, firmware does PHY/rate/EDCA/data movement, and the port runs the vendor bytes on FreeRTOS/ThreadX through linuxkpi. The external pin keeps the GPL/firmware bytes out of the release repository until the licence review is closed. |

Notes:

* The vendor stack is not a submodule and is not carried here; the build errors
  if `VENDOR_8189FS` is not named.
* The firmware version reported by the driver is `ver=e sig=0x88f1`; the
  embedded array is 20,306 bytes. The direct firmware anchor is a separate TRM
  item, not a claim of this provenance file.
* The L2 vendor port under `ports/rtl8189f/` is first-party; it is not the
  vendor driver and does not change the pin.
