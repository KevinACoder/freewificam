# components/fsl_sdmmc - provenance

| Item | Value |
|---|---|
| Upstream | the RK3568 lab standalone SDK, `third-party/fsl_sdmmc/` - an in-tree fork of NXP's `mcuxsdk-middleware-sdmmc` (BSD-3-Clause), modified by the lab and carrying the lab's own host layer and SDK shadow headers. This is the source the predecessor project's board-proven SDIO stack was taken from |
| Pin | lab tree HEAD `297a4116768b3289e2d6af345a3c091b761d4617`, remote `git@gitcode.com:zhugengyu/standalone.git`. No public URL is verified, so the bytes are anchored by `MANIFEST.sha256` rather than by a fetchable ref |
| License | BSD-3-Clause. NXP copyright blocks on `common/`, `mmc/`, `sd/`, `sdio/`, `osa/`; `COPYING-BSD-3` is carried next to the unit. See `LICENSES.md` |
| Vendoring | vendored snapshot, byte-identical to the pin for every kept file: **11 files**, contract = `MANIFEST.sha256` (sha256 per file). **Not** a git submodule: the lab tree is not a clean NXP submodule - 13 files differ from NXP upstream `aaafcf8` (including `sdio/fsl_sdio.c`, ~475 diff lines) and the lab added the host layer and SDK headers in-tree - so a pin-plus-patch submodule cannot express it |
| Fetched | 2026-10-11 from the lab checkout named by the pin |
| Local differences | none (zero patch): no kept file was edited |
| Compiled set | declared by the port suite (`tests/host/fsl-sdmmc-port-build`): `common/fsl_sdmmc_common.c` and `sdio/fsl_sdio.c` compile, together with the first-party port units in `ports/fsl_sdmmc/`. Carried byte-identical but **not compiled**: `mmc/fsl_mmc.{c,h}` and `sd/fsl_sd.{c,h}` (the SD/MMC storage protocols, outside the SDIO closure) |
| Consumers | `ports/fsl_sdmmc/` (the first-party port: seam mapping, card service, OSA implementation, SDK shadow headers) and `board/rk3568-itx/drivers/dwc_mmc.c` (the first-party host driver adapted from the lab's `drivers/mmc/fdwmmc/`) |
| Reason | SDIO is the milestone's second transport domain and the basis of the RTL8189FTV driver line. The lab tree is the board-proven source (the predecessor ported this very stack and reached enumeration and a full SDIO WiFi link); importing it byte-identically keeps the auditable baseline in-repo while this project's own code lives in the port |

Notes:

* **Trim.** The lab directory also carries `ChangeLogKSDK.txt` in `mmc/`, `sd/` and `sdio/`,
  the `osa/fsl_os_abstraction*.{c,h}` set, build scaffolding (`include.mk`, `makefile`,
  `src.mk`, `fsl_sdmmc.kconfig`) and a `partition/` middleware. None of those are kept here.
  The SDK OS-abstraction and `fsl_common.h` are replaced by first-party minimal shadows in
  `ports/fsl_sdmmc/shadow/`.
* This import is anchored to the **lab** bytes, not to NXP upstream `aaafcf8`.
* The `MANIFEST.sha256` paths are relative to this directory (`./common/...`).
