# Provenance — AP6181 WICED port

## The driver core

| | |
|---|---|
| Vendor tree | `~/workspace/stm32_lab/os/wifi-ap6181-f429-code` (outside this repository) |
| WICED version | `Wiced_006.002.001.0002` (`WiFi_SDK/version.txt`) |
| WICED commit in that version file | `7947be1d4e41e9ee5c35b74a4c044ef9c30dfa35` |
| Tree head | the vendor example repository's `HEAD`; `vendor/PIN.txt` records the value the manifest was made from |
| Materializer | `tools/build/wiced-tree.py` (whitelist + sha256 manifest) |
| Materialized into | `wireless-drivers/ap6181/vendor/` — git-ignored |

The whitelist is the compiled/included subset only: the WWD core
(`WICED/WWD`, 67 files), the generic platform resource layer (`wwd_resources.c`,
`platform_resource.c`, the MCU headers), `WICED/internal`, `WiFi_SDK/include`,
the three utility libraries WWD's link table names (TLV, ring buffer, linked
list), the 43362 resource directory, the example's firmware unit and the two
version/licence files.  The SDIO host, the RTOS port, the buffer pool, the
network seam and the power/reset glue are **ours** (`ports/f429/`); the vendor
equivalents are deliberately not copied.

The tree is materialized rather than referenced in place so the objects and
their paths are stable and so `--check` can name drift.  It is git-ignored
because the WICED sources are Cypress-proprietary: the per-file header grants
use with Cypress integrated circuits only and prohibits redistribution.  The
first-party port is BSD-3-Clause; an image that links the vendor tree is for use
with Cypress ICs only and is not a redistributable artifact (see
`docs/LICENSES.md`).

Re-check:

```bash
tools/build/wiced-tree.py --check
```

## The firmware

| | |
|---|---|
| Source | `wifi_lwip_AP/User/43362A2_bin.c` in the vendor tree (the copy the vendor AP example builds) |
| Size | 210,624 bytes (the C array `wifi_firmware_image_data`) |
| sha256 (bytes) | `394d30949d3ffe7098d3d59a488afa36e32b11cb2f380fc4b4ecd447dd83eb4d` |
| NVRAM | `WiFi_SDK/WICED/platform/MCU/STM32F4xx/wifi_nvram_image.h`, consumed by the vendor `wwd_resources.c` |
| Resource layer | `host_platform_resource_size` / `..._read_indirect` in `wwd_resources.c`; `resource_read` in `platform_resource.c` |

The SDK's generated `resources/firmware/43362/` unit is a different packaging of
the same chip family and is not the one the vendor example builds; the example's
unit is the one this port compiles, and its digest above is the anchor.  Both
the WWD and the scan/client examples carry byte-identical copies of it.

## Why this is a port and not an import

The other drivers under `wireless-drivers/` are verbatim imports of GPL or
dual-licensed Linux trees, carried byte-identical with an `IMPORT.tsv`.  This
component cannot follow that rule: the vendor core may not be redistributed, so
`IMPORT.tsv` would be a list of files the repository is not allowed to contain.
The equivalent record is `vendor/MANIFEST.sha256` plus this file: the pin, the
file set, the digests, and the licence boundary.  `tools/build/wiced-tree.py
--check` is the reproducer.