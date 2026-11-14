# wireless-drivers/rtl8xxxu - provenance

| Item | Value |
|---|---|
| Upstream | Linux `drivers/net/wireless/realtek/rtl8xxxu/` |
| Pin | commit `b98a1dfd4661670b9e16d9c74da43e0389ef7e62` |
| Vendoring | flat import, byte-identical: 13 files. Digests in `IMPORT.tsv`; verify with `tools/build/import-driver.py --driver rtl8xxxu --check`. |
| Local differences | none (zero patch) |
| Compiled set | **9 TUs** under the feature face below; named by `sources.mk` |
| Feature face | on: RTL8XXXU=1 |
| | off: RTL8XXXU_UNTESTED |

## License

| SPDX expression | files |
|---|---|
| `GPL-2.0-only` | 13 |

Every file of the tree carries `SPDX-License-Identifier: GPL-2.0-only`
(the module's own `MODULE_LICENSE` line names no alternative), so the driver
adds no licensing option to this line: an artifact that links it with the
GPL-2.0-only mac80211/cfg80211 stack is a GPL-2.0 link product (see
`docs/LICENSES.md`). The driver bytes are unmodified, which is why the
licence table above is the tree's own.

## Why 9

The driver's own Makefile resolves to nine translation units under this
face (`rtl8xxxu-y := core.o 8192e.o 8723b.o 8723a.o 8192c.o 8188f.o
8188e.o 8710b.o 8192f.o`): one module whose `fileops` table names every
chip it supports, so a chip that is not the target still contributes its fileops
and its register/PHY callbacks to the same module, and the chip selection is a
run-time `id_table` match plus the firmware blob, not a build-time unit list.
The GT build compiles the same nine under `CONFIG_RTL8XXXU=m`; this face
states that symbol and leaves `CONFIG_RTL8XXXU_UNTESTED` off (the untested
parts' id-table block only, 0bda:8179 is in the tested table).

## Consumers

`tests/board/rtos/build/wl80211.mk` compiles this set in the driver's
flag world (`driver_config.h` is force-included); `tools/build/s1-driver-compile`
compiles it standalone with `S1_DRIVER=rtl8xxxu` as the S1b measurement. The
`rtlwifi/rtl8188eufw.bin` image is a compiled-in blob, not a driver unit
(tools/rtos/fw-blobs.py).
