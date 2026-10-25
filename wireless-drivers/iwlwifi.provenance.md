# wireless-drivers/iwlwifi - provenance

| Item | Value |
|---|---|
| Upstream | mainline Linux `drivers/net/wireless/intel/iwlwifi/` (the directory as it stands in 7.3.0-rc2) |
| Pin | commit `e71d9a28760f605082308c2e1114627d406b4a29` |
| License | **mixed SPDX, GPL-2.0 for the module**: 238 files `GPL-2.0 OR BSD-3-Clause`, 46 files `GPL-2.0-only` (dvm/mei/tests/devtrace and `mvm/rs.c`); `iwl-drv.c` states `MODULE_LICENSE("GPL")`. Copyright stays with the driver's authors. The firmware image is **not** vendored here and carries its own licence (`LICENCE.iwlwifi_firmware` in the image). |
| Vendoring | **Flat import** into this repository, byte-identical to the pin: 294 files (155 `.c`, 130 `.h`, Kconfig/Makefile). The file digests are in `wireless-drivers/iwlwifi/patches/MANIFEST.sha256`; verify from `wireless-drivers/iwlwifi/` with `sha256sum -c patches/MANIFEST.sha256`. Verified 294/294 against the pinned kernel tree at the import commit. |
| Local differences | none (zero patch). `wireless-drivers/iwlwifi/patches/` carries only its README and the digest manifest. |
| Compiled set | **7260 mvm subset only**, listed by `tests/board/linux-ko/build.sh` with `DRIVER=iwlwifi`: core (`iwl-drv`, `iwl-io`, `iwl-debug`, NVM/phy-db/utils, `fw/*`, `iwl-trans`), PCIe gen1/2 transport, all `cfg/7000`-family tables the IWLMVM id table needs, and `mvm/*` (including the unconditional mld five). DVM, MLD, MEI, tests, and the IWLMLD-only cfg/rf families are vendored but not compiled. The list is re-derived from the driver's `Makefile`/`mvm/Makefile` plus the ko kernel `.config`; the RTOS line's 72-TU list is a seed, not the authority. |
| Consumers | `tests/board/linux-ko/build.sh` with `DRIVER=iwlwifi`; the resulting `wlan_ko.ko` registers the PCI driver (`module_pci_driver`) and the `iwlmvm` op_mode, and the mac80211 substitute calls into it through the seam. |
| Reason | the AC 7260 is the project's first **PCIe SoftMAC** line and the second firmware shape: TLV container + ALIVE, host command queue (TFD + doorbell) and RX-queue notifications, only `hw_scan` (no software scan), and a mac80211 rate-control registration. Keeping the tree flat and manifested makes every byte auditable next to the first-party seam. |

Notes:

* The driver is byte-faithful to mainline Linux 7.3.0-rc2, commit
  `e71d9a28760f605082308c2e1114627d406b4a29`, directory
  `drivers/net/wireless/intel/iwlwifi/`. The digest manifest in
  `wireless-drivers/iwlwifi/patches/` is the check this repository runs.
* The layout is flat (sources at the tree root and in `cfg/`, `fw/`, `mvm/`,
  `pcie/`), so quoted includes such as `#include "iwl-trans.h"` and
  `#include "fw/api/commands.h"` resolve in place.
* The 7260 exposes `8086:08b1` (subsystem `08b1:5070` on the lab card) and is
  driven by the `iwlmvm` op_mode; firmware `iwlwifi-7260-17.ucode` (API 17,
  1,049,340 B) is requested through `request_firmware()` and supplied by the
  image at `/lib/firmware/`.
* The chip has a **single MSI** (`mq_rx_supported` false), a TFD ring per TX
  queue (31 queues, 256 descriptors) and one RX queue; there is no H2C
  register mailbox and no per-command ACK. Completion is a response or a
  notification in the RX queue.
* Scan is `hw_scan` only: the ops table has `.hw_scan`/`.cancel_hw_scan` and no
  `.sw_scan_start`, so the net80211 bridge must use the request/complete
  contract.
