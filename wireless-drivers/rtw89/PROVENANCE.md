# wireless-drivers/rtw89 - provenance

| Item | Value |
|---|---|
| Upstream | Linux `drivers/net/wireless/realtek/rtw89/` |
| Pin | commit `b98a1dfd4661670b9e16d9c74da43e0389ef7e62` |
| Vendoring | flat import, byte-identical: 107 files. Digests in `IMPORT.tsv`; verify with `tools/build/import-driver.py --driver rtw89 --check`. |
| Local differences | none (zero patch) |
| Compiled set | **24 TUs** under the feature face below; named by `sources.mk` |
| Feature face | on: RTW89_CORE=1, RTW89_USB=1, RTW89_8851B=1, RTW89_8851BU=1 |
| | off: RTW89_PCI, RTW89_8851BE, RTW89_8852A, RTW89_8852AE, RTW89_8852AU, RTW89_8852B_COMMON, RTW89_8852B, RTW89_8852BE, RTW89_8852BU, RTW89_8852BT, RTW89_8852BTE, RTW89_8852C, RTW89_8852CE, RTW89_8852CU, RTW89_8922A, RTW89_8922AE, RTW89_8922AU, RTW89_8922D, RTW89_8922DE, RTW89_DEBUG, RTW89_DEBUGMSG, RTW89_DEBUGFS, RTW89_LEDS, RTW89_LEDS_MC, PM |

## License

| SPDX expression | files |
|---|---|
| `GPL-2.0 OR BSD-3-Clause` | 107 |

The tree is `GPL-2.0 OR BSD-3-Clause` in every file. The previous line took
the BSD option for its net80211 profile; this line links the driver with the
GPL-2.0-only mac80211/cfg80211 stack, so every artifact that carries it is a
GPL-2.0 link product (see `docs/LICENSES.md`). The driver bytes are
unmodified, which is why the licence table above is the tree's own.

## Why 24

The driver's own Makefile resolves to 24 translation units under this face:
18 core units (its `rtw89_core-y` names the three `*_be` units
unconditionally), the four 8851B chip units, the USB bus unit and the
8851BU device unit. The Wi-Fi 7 `*_be` units are compiled because the
Makefile lists them; the chip selection never reaches them, and the image's
zero-undefined gate is the check. The GT build compiles more: `debug.c`,
`led.c`, `led_mc.c` and `wow.c` ride CONFIG_RTW89_DEBUG/LEDS/PM, which this
face keeps out (logging, LED and suspend units, not the data path).

## Consumers

`tests/board/rtos/build/wl80211.mk` compiles this set in the driver's
flag world (`driver_config.h` is force-included); `tools/build/s1-driver-compile`
compiles it standalone with `S1_DRIVER=rtw89` as the S1b measurement.
