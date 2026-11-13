# wireless-drivers/rtl8189fs - provenance

| Item | Value |
|---|---|
| Upstream | Linux `drivers/net/wireless/realtek/rtl8189fs/` |
| Pin | commit `b98a1dfd4661670b9e16d9c74da43e0389ef7e62` |
| Vendoring | import under `src/`, byte-identical: 533 files. Digests in `IMPORT.tsv`; verify with `tools/build/import-driver.py --driver rtl8189fs --check`. |
| Local differences | none (zero patch) |
| Compiled set | **131 TUs** under the feature face below; named by `sources.mk` |
| Feature face | the vendor Makefile's own CONFIG_ block (the GT build's), with `CONFIG_PROC_DEBUG`,
| | `CONFIG_WIFI_MONITOR` and `CONFIG_BR_EXT` off; stated in `driver_config.h` |

## License

| SPDX expression | files |
|---|---|
| `GPL-2.0-only` | 2 |
| `unknown` | 531 |

Most vendor files carry the long GPL header rather than an SPDX tag, so
their row above reads `unknown`; the tree's `LICENSE` (GPL-2.0) governs.

The tree is GPL-2.0. The embedded firmware
array (`FW_NIC`, 20,306 B) is part of the driver source and travels under the
same terms; it is not a separately extracted blob. Every artifact that links
it is GPL-2.0; see `docs/LICENSES.md`.

## 131 units, and why the face keeps all of them

The vendor Makefile assembles its link table from `:=` lists plus
`hal/phydm/phydm.mk`; `make -pn` on this tree resolves `8189fs-y` to 132
objects with one duplicate (`sdio_ops_linux.o` is named twice), i.e. 131 units.
The RTOS face changes two preprocessor answers (`CONFIG_PROC_DEBUG` off,
`CONFIG_WIFI_MONITOR` off) and thereby no unit: `rtw_proc.c` is one big
`#ifdef CONFIG_PROC_DEBUG` and compiles empty.

## Consumers

`tests/board/rtos/build/wl80211.mk` compiles this set in the driver's
flag world (the `driver_config.h` face is force-included); `tools/build/
s1-driver-compile` compiles it standalone as the S1b measurement for this line.
