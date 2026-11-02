# RK3568 ITX (the reference board)

Every hardware result in this project comes from one board: an RK3568 ITX
board (the vendor's name for it is RK3568-E4AP5G1-ITX), a small server board
built on the Rockchip RK3568B2 SoC. This page records what is on the board,
which connector this project uses for what, and how an image gets in, so the
wiring behind a measured result is answerable from the repository alone.

## The board

| | |
|---|---|
| Board | RK3568 ITX (RK3568-E4AP5G1-ITX); the device tree names it `rockchip,rk3568-e4ap5g1-itx` |
| SoC | Rockchip RK3568B2, four Arm Cortex-A55 cores, 64-bit |
| Memory | One DDR3 SODIMM slot; side-band ECC is on by default, so half the module's capacity is spent on ECC |
| Storage | An eMMC module socket, optional SPI-NOR flash; the SD socket is a recovery path, not the boot medium |
| Power | 12 V DC in (6-16 V accepted), around 6 W typical |
| Form factor | ITX |

A good part of the board's I/O hangs off a PCIe packet switch rather than the
SoC's own controllers, so "on the board" and "on the SoC" are not the same
thing here. The two 2.5 GbE ports and most of the M.2 wiring are behind that
switch; the 1 GbE port and the console are on the SoC.

## On-board peripherals

| Block | Instance | Base | Notes |
|---|---|---|---|
| Console UART | uart2 | `0xfe660000` | RS232 rear console, 115200 8N1 |
| Ethernet | gmac0 | `0xfe2a0000` | RGMII to an RTL8211F PHY |
| Ethernet | gmac1 | `0xfe010000` | RGMII to an RTL8211F PHY; the rear 1 GbE port |
| 2.5 GbE | two Intel i226 | (PCIe) | Behind the PCIe switch, not a SoC MAC |
| SD / MMC | sdmmc0 | `0xfe2b0000` | 4-bit socket, 3.3 V signalling |
| eMMC | sdhci | `0xfe310000` | 8-bit, 52 MHz, module socket |
| Boot flash | sfc (SPI-NOR) | -- | Optional; carries the boot FIT on boards that use it |
| USB 2.0 | ehci0 / ehci1 | `0xfd800000`, `0xfd880000` | Two USB 2.0 ports |
| USB 3.0 | xhci0 / xhci1 | `0xfd000000`, `0xfcc00000` | Two USB 3.0 ports; one doubles as the maskrom/OTG download port |
| PCIe 3.0 | pcie3x2 | `0xfe280000` (APB) | x2 electrical; dedicated PHY, not a combo PHY |
| PCIe 2.0 | pcie2x1 | `0xfe260000` (APB) | Lanes to the M.2 sockets and the PCIe switch |
| Display | HDMI, DP, MIPI DSI | -- | HDMI and DP on the rear panel |
| Power monitor | INA3221 | i2c1 `0x40` | Three-rail (3V3 / 5V / 12V) monitor |
| RTC | RX8025 | i2c1 `0x32` | CR2032 backed |
| PMIC | RK809 + SYR827 | i2c0 `0x20` / `0x40` | System rails and the CPU regulator |

The base addresses that this project programs are declared in
[`../tests/board/rtos/platform/board/rk3568-itx/board_conf.h`](../tests/board/rtos/platform/board/rk3568-itx/board_conf.h)
and the per-driver headers; the table above is the summary.

## Pin and PHY multiplexing

The RK3568 exposes three combo PHYs. Each can carry USB 3.0 SuperSpeed, SATA,
or PCIe, depending on the board's wiring and the firmware's configuration:

| Combo PHY | Board usage |
|---|---|
| combphy0 | USB 3.0 or SATA, selected at run time |
| combphy1 | USB 3.0 or SATA, selected at run time |
| combphy2 | USB 3.0, SATA or PCIe |

Two GPIO lines select the SATA side of the mux: `multiphy_mux_sel0` on
GPIO0_A5 and `multiphy_mux_sel1` on GPIO0_A6. The device tree additionally
uses `rockchip,dis-u3otgN-port` to switch off the USB 3.0 side of a combo PHY
when that PHY is given to SATA; on the board's 3-SATA variant both mux selects
are driven to SATA. That is why the USB 3.0 ports on this board run at USB 2.0
speed in the configurations this project uses, and why the SuperSpeed lanes do
not appear as a separate controller.

The PCIe 3.0 x2 slot does **not** use a combo PHY: it has its own dedicated
PHY. A board port that assumes the slot is behind combphy2 probes nothing.

The remaining multiplexing is ordinary pin muxing:

| Pin group | Pins / function |
|---|---|
| `gmac0_*` | RGMII (MIIM, TX/RX bus, clock) for gmac0 |
| `gmac1m0_*` | RGMII for gmac1, mux option 0 |
| `sdmmc0_*` | 4-bit SD bus, clock, command, card detect (GPIO0_A4) |
| `emmc_*` | 8-bit eMMC bus, clock, command, data strobe |
| `fspi_*` | SPI-NOR flash |
| `uart2_*` | Console UART |
| GPIO controls | USB power enables, PCIe reset (GPIO2_D6), PCIe 3V3 enable (GPIO0_D4), PMIC interrupt (GPIO0_A3), user LED (GPIO4_C1) |

## What this project drives

| Board connector | Project use | Code in this tree |
|---|---|---|
| Rear console (uart2) | Board console: the boot firmware's prompt and every captured transcript | `tests/board/rtos/platform/board/rk3568-itx/drivers/uart.c` |
| Rear 1 GbE port (gmac1) | The management link: image download over TFTP, DHCP, host-to-board traffic | `drivers/dwc_eqos.c`, `drivers/rtl8211f.c` |
| SD socket (sdmmc0) | The SDIO WiFi part (RTL8189FTV), wired to the module rather than to a removable card | `drivers/dwc_mmc.c`, `third-party/fsl_sdmmc/ports/` |
| USB 2.0 ports (ehci0/1) | The USB WiFi parts (RTL8188EUS, RTL8723BU, RTL8851BU), the USB camera (UVC), and the USB disk used by the mass-storage case | `ports/cherryusb/`, `third-party/cherryusb/` |
| PCIe 3.0 x2 slot (pcie3x2) | The Intel AC 7260 card | `drivers/pcie/`, `ports/pcie/` |
| M.2 slot (pcie2x1) | An NVMe drive | `drivers/pcie/`, `drivers/nvme/` |
| eMMC module (sdhci) | The block-device case's target | `drivers/dwc_mshc.c` |
| DC input | Switched by an external, network-controlled socket, so a cold power cycle is part of a round | bench only |

The two 2.5 GbE ports, the HDMI/DP outputs, the I2C monitor and the RTC are
not driven by any code in this tree. The SPI-NOR flash only ever appears as
boot media, configured by the firmware.

## Firmware sources

Both firmware trees live on GitHub; this repository does not vendor them.

| Component | Repository | Branch | Board inputs |
|---|---|---|---|
| Boot firmware | <https://github.com/KevinACoder/u-boot-rockchip> | `next-dev` | `rk3568-e4ap5g1-itx_defconfig`, `arch/arm/dts/rk3568-e4ap5g1-itx.dts`; `scripts/build-fit.sh` packages the SD boot FIT (`u-boot.itb`) |
| Reference Linux kernel | <https://github.com/KevinACoder/linux> | `v7.3-rc2-firefly` | `arch/arm64/boot/dts/rockchip/rk3568-e4ap5g1-itx.dts`; the measured reference image is commit `e71d9a28760f` (`7.3.0-rc2-ge71d9a28760f`) |

The boot chain is the vendor SPL, then ATF/OP-TEE, then U-Boot proper;
`scripts/build-fit.sh` builds U-Boot proper and packages the FIT from a
verified base FIT.  The Linux tree is the one the linuxnative reference runs
on: build `Image` and `rk3568-e4ap5g1-itx.dtb`, boot with an initramfs, and use
the method in [`../NET-TESTING.md`](../NET-TESTING.md) for the numbers.

## Boot path and memory

The RTOS images are flat raw binaries, not ELF files booted by the firmware:

- the boot firmware receives the image over TFTP (`tftp 0xa000000 <image>.bin`)
  and enters it with `go 0xa000000`; `bootelf` is unusable on this board
  because its ELF path copies no segments and faults at the jump;
- the image is linked to run from `0xa000000`, and `.text.boot` is first in the
  link order because the entry point is the load address, not an ELF header;
- the RAM window is `[0x0a000000, 0x10000000)`: below it belongs to the boot
  firmware (ATF/OP-TEE), at or above `0x10000000` an access raises an external
  abort;
- the console is 115200 8N1, and the first thing the image prints is its build
  tag, which carries `git describe` of the tree it was built from.

The link script also accepts `board_ram_base` / `board_ram_top` overrides, so a
carried board line with a different load window links from the same platform
tree; the RK3568 defaults above stay in place when no override is given.

## Reset and power

- The bench drives the DC input through a network-controlled socket; a cold
  power cycle is the reset of record for a boot result.
- The RTOS images drive USB VBUS through the board's GPIO3 lines (A0 for the
  USB 2.0 group, A1 for the USB 3.0 socket group), so every boot gives each
  port a cold VBUS ramp.
- The PCIe slots reset through GPIO2_D6 (pcie3x2) and GPIO3_PC1 (pcie2x1).
- The SD socket's 3.3 V rail is always on and cannot be switched in software;
  the SDIO part is reset through its own chip reset instead.

## About the vendor's figures

This page is text only. The board vendor's photographs and block diagram are
not carried in this repository (no redistribution grant is recorded for the
vendor manual they come from); the facts above are stated so a reader does not
need them.

## Related

- [`../README.md`](../README.md) - what the repository is and which lines it carries.
- [`RELEASE-NOTES-0.0.2.md`](RELEASE-NOTES-0.0.2.md) - the measured state of each line.
- [`../tests/board/rtos/SOURCE.md`](../tests/board/rtos/SOURCE.md) - where the platform tree came from.
- [`FIRMWARE-NOTICES.md`](FIRMWARE-NOTICES.md) - the firmware files the wireless lines load.
