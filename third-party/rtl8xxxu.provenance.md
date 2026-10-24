# third-party/rtl8xxxu - provenance

| Item | Value |
|---|---|
| Upstream | mainline Linux `drivers/net/wireless/realtek/rtl8xxxu/` (the directory as it stands in 7.3.0-rc2) |
| Pin | commit `e71d9a28760f605082308c2e1114627d406b4a29` |
| License | **GPL-2.0-only** - every imported file carries `SPDX-License-Identifier: GPL-2.0-only` as its first line (13/13). Copyright stays with the driver's authors. The driver's `core.c` states `MODULE_LICENSE("GPL")`. |
| Vendoring | **Flat import** into this repository, byte-identical to the pin. The file digests are in `patches/rtl8xxxu/MANIFEST.sha256`; verify from this directory with `sha256sum -c ../../patches/rtl8xxxu/MANIFEST.sha256`. Verified 13/13 against the pinned kernel tree at the import commit. |
| Local differences | none (zero patch). `patches/rtl8xxxu/` carries only its README and the digest manifest. One file in this directory is first-party rather than extracted: `README.md`, which records the source, the module list and the license line. |
| Compiled set | all nine `.c` files (`core.c`, `8188e.c`, `8188f.c`, `8192c.c`, `8192e.c`, `8192f.c`, `8710b.c`, `8723a.c`, `8723b.c`). The driver's own `Makefile` lists all nine for the module's link closure: `core.c` references the other chips' `fileops`, so trimming the set would leave undefined symbols. The kernelmod build lists the units itself because it wires the driver into one module with the stack; the extracted `Makefile` is not used as a build shell there. |
| Consumers | `tests/board/linux-ko/build.sh` with `DRIVER=rtl8xxxu`; the resulting module registers the driver on the USB bus and the mac80211 substitute calls into it through the seam. |
| Reason | the RTL8188EUS is the project's first **8051-class USB SoftMAC** line: an older mac80211 callback face than rtw88/rtw89, a register mailbox with four 4-byte boxes and a busy bit instead of a class/function H2C channel, and a 196-byte firmware download block. Keeping the tree flat and manifested makes every byte auditable next to the first-party sources. |

Notes:

* The driver is byte-faithful to mainline Linux 7.3.0-rc2, commit
  `e71d9a28760f605082308c2e1114627d406b4a29`, directory
  `drivers/net/wireless/realtek/rtl8xxxu/`. The digest manifest in
  `patches/rtl8xxxu/` is the check this repository runs.
* The layout is flat (sources at the tree root), so quoted includes such as
  `#include "regs.h"` resolve in place and no include path is added for them.
* The RTL8188EUS exposes its vendor-specific interface as `0bda:8179` and needs
  no mode switch: the driver's id table matches it directly, and the 8188e unit
  rejects cut-I silicon on its own.
* Firmware: `8188e.c` asks for `rtlwifi/rtl8188eufw.bin` through
  `request_firmware()`. The blob is an image fact and is not vendored here; the
  board image supplies it at `/lib/firmware/rtlwifi/rtl8188eufw.bin`.
* The chip's firmware channel is a **4-box register mailbox** (`HMBOX0..3`,
  `0x1d0..0x1dc`) with the `HMTFR` busy bit (`0x1cc`) as its only flow control;
  there is no per-command acknowledgement. The 8188e fileops use the gen2
  mailbox writer for the media-status report and do not use the gen1
  join-bss/rssi/rate-mask commands. There is no C2H event window for this chip
  (`hw_feature_report = 0`); TX reports arrive in the receive descriptor.
  Scan is a host software scan and rate adaptation runs in the host driver.
