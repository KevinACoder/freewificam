# wireless-drivers/brcmfmac - provenance

| Item | Value |
|---|---|
| Upstream | Linux `drivers/net/wireless/broadcom/brcm80211/` |
| Pin | commit `b98a1dfd4661670b9e16d9c74da43e0389ef7e62` |
| Vendoring | flat import, byte-identical: 136 files. Digests in `IMPORT.tsv`; verify with `tools/build/import-driver.py --driver brcmfmac --check`. |
| Local differences | none (zero patch) |
| Compiled set | **24 TUs** under the feature face below; named by `sources.mk` |
| Feature face | on: BRCMUTIL=1, BRCMFMAC=1, BRCMFMAC_SDIO=1, BRCMFMAC_PROTO_BCDC=1 |
| | off: BRCMSMAC, BRCMFMAC_PROTO_MSGBUF, BRCMFMAC_USB, BRCMFMAC_PCIE, BRCMDBG, BRCM_TRACING, OF, DMI, ACPI, PM |

## License

| SPDX expression | files |
|---|---|
| `GPL-2.0` | 1 |
| `GPL-2.0-only` | 1 |
| `ISC` | 96 |
| `unknown` | 38 |

Every brcmfmac/ and brcmutil/ file is `ISC` (the driver's own SPDX tags);
the imported brcm80211/ tree also carries the GPL-2.0-only brcmsmac/ sources
and the bca/cyw helper trees, which are not compiled here.  The compiled
brcmfmac set is ISC, but the image links the GPL-2.0-only cfg80211, so the
artifact is a GPL-2.0 link product (see `docs/LICENSES.md`);

## Why 24

The driver's own Makefiles resolve to 24 units under this face:
15 brcmfmac core units, 2 BCDC units (`bcdc.o`, `fwsignal.o`), 2 SDIO
units (`sdio.o`, `bcmsdh.o`), the three built-in helper cores the Makefile
names under `CONFIG_BRCMFMAC=y` (`wcc/core.o`, `cyw/core.o`,
`bca/core.o`), and `brcmutil/{utils,d11}.o`.  The feasibility probe measured
the SDIO closure at 19 + brcmutil + wcc; the two extra core units are the
Makefile's own built-in branch and the C0 compile decides whether they stay.

## Consumers

`tests/board/rtos/build/wl80211.mk` compiles this set in the driver's
flag world (`driver_config.h` is force-included); `tools/build/s1-driver-compile`
compiles it standalone with `S1_DRIVER=brcmfmac`.  The 43362 firmware and NVRAM
are compiled-in blobs (`tools/rtos/fw-blobs.py`), not driver units.
