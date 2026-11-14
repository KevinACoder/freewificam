# wireless-drivers/rtw88 - provenance

| Item | Value |
|---|---|
| Upstream | Linux `drivers/net/wireless/realtek/rtw88/` |
| Pin | commit `b98a1dfd4661670b9e16d9c74da43e0389ef7e62` |
| Vendoring | flat import, byte-identical: 98 files. Digests in `IMPORT.tsv`; verify with `tools/build/import-driver.py --driver rtw88 --check`. |
| Local differences | none (zero patch) |
| Compiled set | **20 TUs** under the feature face below; named by `sources.mk` |
| Feature face | on: RTW88_CORE=1, RTW88_USB=1, RTW88_8821C=1, RTW88_8821CU=1 |
| | off: RTW88_PCI, RTW88_SDIO, RTW88_8822B, RTW88_8822BE, RTW88_8822BS, RTW88_8822BU, RTW88_8822C, RTW88_8822CE, RTW88_8822CS, RTW88_8822CU, RTW88_8723X, RTW88_8703B, RTW88_8723CS, RTW88_8723D, RTW88_8723DE, RTW88_8723DS, RTW88_8723DU, RTW88_8821CE, RTW88_8821CS, RTW88_8821AU, RTW88_88XXA, RTW88_8821A, RTW88_8812A, RTW88_8812AU, RTW88_8814A, RTW88_8814AE, RTW88_8814AU, RTW88_DEBUG, RTW88_DEBUGFS, RTW88_LEDS, PM |

## License

| SPDX expression | files |
|---|---|
| `GPL-2.0 OR BSD-3-Clause` | 97 |
| `GPL-2.0-only` | 1 |

The tree is `GPL-2.0 OR BSD-3-Clause` in every file. The previous line took
the BSD option for its net80211 profile; this line links the driver with the
GPL-2.0-only mac80211/cfg80211 stack, so every artifact that carries it is a
GPL-2.0 link product (see `docs/LICENSES.md`). The driver bytes are
unmodified, which is why the licence table above is the tree's own.

## Why 20

The driver's own Makefile resolves to 20 translation units under this face:
16 core units (`rtw88_core-y`), the two 8821C chip units (`rtw8821c.c`,
`rtw8821c_table.c`), the 8821CU USB device unit (`rtw8821cu.c`) and the
USB bus unit (`usb.c`). The GT build compiles more: `led.c` and `wow.c` ride
CONFIG_RTW88_LEDS and CONFIG_PM (LED and suspend units, not the data path),
and the debugfs registration in `debug.c` is `CONFIG_RTW88_DEBUGFS`-gated while the
unit itself stays in the core aggregate. The GT Image reaches the same chip with
CONFIG_RTW88_8821CU=y; this face keeps the same data-path units.

## Consumers

`tests/board/rtos/build/wl80211.mk` compiles this set in the driver's
flag world (`driver_config.h` is force-included); `tools/build/s1-driver-compile`
compiles it standalone with `S1_DRIVER=rtw88` as the S1b measurement. The
`rtw8821c_fw.bin` image is a compiled-in blob, not a driver unit (tools/rtos/fw-blobs.py).
