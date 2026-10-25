# wireless-drivers/rtw88 - provenance

| Item | Value |
|---|---|
| Upstream | mainline Linux `drivers/net/wireless/realtek/rtw88/` (the directory as it stands in 7.3.0-rc2) |
| Pin | commit `e71d9a28760f605082308c2e1114627d406b4a29` |
| License | **GPL-2.0 OR BSD-3-Clause** - the imported files carry that SPDX form; this project takes the BSD-3-Clause option, copyright stays with the driver's authors. `main.c` states `MODULE_LICENSE("Dual BSD/GPL")`. |
| Vendoring | **Flat import** into this repository (not a submodule), byte-identical to the pin: 96 tracked files. The file digests are in `wireless-drivers/rtw88/patches/MANIFEST.sha256`; verify from the driver directory with `sha256sum -c patches/MANIFEST.sha256`. |
| Local differences | none (zero patch). `wireless-drivers/rtw88/patches/` carries only its README and the digest manifest. |
| Compiled set | the 8821CU USB build listed by `tests/board/linux-ko/build.sh` with `DRIVER=rtw88`: core (`main.c`, `mac80211.c`, `util.c`, `debug.c`, `tx.c`, `rx.c`, `mac.c`, `phy.c`, `coex.c`, `efuse.c`, `fw.c`, `ps.c`, `sec.c`, `bf.c`, `sar.c`, `regd.c`, `wow.c`, `led.c`), the 8821C chip units (`rtw8821c.c`, `rtw8821c_table.c`), the 8821CU bus unit (`rtw8821cu.c`) and the USB bus (`usb.c`). The remaining files are carried byte-identical and not compiled (other chips and buses). |
| Consumers | `tests/board/linux-ko/build.sh` with `DRIVER=rtw88`; the resulting `wlan_ko.ko` registers the USB driver and the mac80211 substitute calls into it through the seam. |
| Reason | the RTL8821CU is the project's 802.11ac USB SoftMAC arm: an older mac80211 callback face than rtw89/iwlwifi, with the USB bulk aggregation path and the `rtw88/rtw8821c_fw.bin` firmware. Keeping the tree flat and manifested makes every byte auditable next to the first-party port. |

Notes:

* The driver is byte-faithful to mainline Linux 7.3.0-rc2, commit
  `e71d9a28760f605082308c2e1114627d406b4a29`, directory
  `drivers/net/wireless/realtek/rtw88/`. The digest manifest in
  `wireless-drivers/rtw88/patches/` is the check this repository runs.
* The layout is flat (sources at the tree root), so quoted includes such as
  `#include "reg.h"` resolve in place and no include path is added for them.
* Firmware: `rtw8821c.c` requests `rtw88/rtw8821c_fw.bin` through
  `request_firmware()`. The blob is an image fact and is not vendored here; the
  board image supplies it at `/lib/firmware/rtw88/rtw8821c_fw.bin`.
* The USB IDs and the firmware path are facts of the driver bytes; the
  first-party port adds only the USB host controller and the mac80211 surface.
