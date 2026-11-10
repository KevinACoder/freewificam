# wireless-drivers/iwlwifi - provenance

| Item | Value |
|---|---|
| Upstream | Linux `drivers/net/wireless/intel/iwlwifi/` |
| Pin | commit `63a2a8ab9cd31a9821f5c86c05cbb0be88b82e7b` |
| Vendoring | flat import, byte-identical: 294 files. Digests in `IMPORT.tsv`; verify with `tools/build/import-driver.py --driver iwlwifi --check`. |
| Local differences | none (zero patch) |
| Compiled set | **72 TUs** under the feature face below; named by `sources.mk` |
| Feature face | on: IWLMVM=1 |
| | off: IWLDVM, IWLMLD, IWLMEI, IWLWIFI_DEBUGFS, IWLWIFI_DEVICE_TRACING, IWLWIFI_LEDS, PM_SLEEP, ACPI, EFI |

## License

| SPDX expression | files |
|---|---|
| `GPL-2.0` | 3 |
| `GPL-2.0 OR BSD-3-Clause` | 241 |
| `GPL-2.0-only` | 48 |
| `unknown` | 2 |

The driver is a GPL-2.0 module (`iwl-drv.c` states `MODULE_LICENSE("GPL")`).
Every artifact that links it is GPL-2.0; see `docs/LICENSES.md` and
`components/liblinux80211/docs` (the stack's own licence note).

## Why 72 and not 74

The previous line's `sources.mk` listed 74 entries: 72 driver translation units
plus the compiled-in firmware blob and the board case. The driver's own
Makefiles resolve to 72 under this face; the other two entries were the
consumer's, not the driver's.

## Consumers

`tests/board/rtos/build/wl80211.mk` compiles this set into the
`rk3568-iwl7260-mac80211` profile; `tools/build/s1-driver-compile` compiles it
standalone into `runs/S1/driver/` (the S1b measurement).
