# third-party/rtw89 - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/rtw89.git (branch `linux-7.3-rc2`) - an orphan export of the mainline driver directory, pinned so the driver bytes can be diffed and audited without a kernel checkout |
| Pin | commit `79e12cc959d80b20f9b8499f885faf62cee1ade1` (branch `linux-7.3-rc2` tip; the branch is a single commit with no parent) |
| License | **GPL-2.0 OR BSD-3-Clause** - all 107 tracked files carry that SPDX form (61 `//` form, 44 `/* */` or `#` form), so the whole tree is dual-licensed. Each bus unit states `MODULE_LICENSE("Dual BSD/GPL")`. This project takes the **BSD-3-Clause option**; copyright stays with the driver's authors. Distribution obligations are recorded with the import commit. |
| Vendoring | **Flat import** into this repository (not a submodule), byte-identical to the pin. The file digests are in `patches/rtw89/MANIFEST.sha256`; verify from this directory with `sha256sum -c ../../patches/rtw89/MANIFEST.sha256`. Verified 107/107 against the pinned tree and against the kernel's 7.3.0-rc2 `drivers/net/wireless/realtek/rtw89/` at the import commit. |
| Local differences | none (zero patch). `patches/rtw89/` carries only its README and the digest manifest. The extracted `Makefile` is not used as a build shell by the kernelmod line: the build lists the translation units and passes the configuration (`CONFIG_RTW89_CORE`, `CONFIG_RTW89_USB`, `CONFIG_RTW89_8851B`, `CONFIG_RTW89_8851BU`) itself. |
| Compiled set | 28 translation units for the 8851BU USB build: 18 core (`core.c`, `mac80211.c`, `mac.c`, `mac_be.c`, `phy.c`, `phy_be.c`, `fw.c`, `cam.c`, `efuse.c`, `efuse_be.c`, `regd.c`, `sar.c`, `coex.c`, `ps.c`, `chan.c`, `ser.c`, `acpi.c`, `util.c`), plus `wow.c` (`CONFIG_PM`), `debug.c` (`CONFIG_RTW89_DEBUG`), `led.c`/`led_mc.c` (`CONFIG_RTW89_LEDS`/`_MC`), four chip units (`rtw8851b.c`, `rtw8851b_table.c`, `rtw8851b_rfk.c`, `rtw8851b_rfk_table.c`), `rtw8851bu.c` and the USB bus (`usb.c`). The remaining files are carried byte-identical and not compiled (other chips, PCI/SDIO buses). |
| Consumers | the kernelmod line's Linux module (`tests/board/linux-ko/`, `ports/linux/`), which compiles the driver against the kernel's own mac80211 types and the first-party substitute; the linuxnative line builds the same bytes as the kernel's `rtw89_*` modules against the debug kernel. |
| Reason | the RTL8851BU is the 802.11ax USB/SoftMAC line that exercises the newer mac80211 driver contract (`vif_cfg_changed`/`link_info_changed`, `sta_state`, channel contexts) against net80211. rtw89 is the mainline driver for the family, so it reaches net80211 through the same seam as rtw88 with a wider API face. Keeping the tree flat and manifested makes each byte auditable next to the first-party sources. |

Notes:

* The driver is byte-faithful to mainline Linux 7.3.0-rc2, commit
  `e71d9a28760f605082308c2e1114627d406b4a29`, directory
  `drivers/net/wireless/realtek/rtw89/`. All 107 files were compared against that
  tree at the import commit (107/107, zero differences); the digest manifest in
  `patches/rtw89/` is the check this repository runs.
* The layout is flat (sources at the tree root), so quoted includes such as
  `#include "reg.h"` resolve in place and no include path is added for them.
* The 8851BU exposes its vendor-specific interface as `0bda:b851` (plus the
  vendor-branded IDs in `rtw8851bu.c`); it needs no mode switch. The driver's id
  table carries the USB IDs directly.
* Firmware: the driver requests `rtw89/rtw8851b_fw-1.bin` first and falls back
  to `rtw89/rtw8851b_fw.bin` (`fw.c` walks the format number downwards from the
  chip's maximum, `fw.h` builds the name). The blobs are image facts, not driver
  bytes; the kernelmod line bakes them into its initramfs.
