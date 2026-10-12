# Component licenses

This registry records the license of every vendored, extracted or imported unit in this
repository, together with the obligations that follow from them. It exists so that the
provenance of each byte in a built image is answerable without reading every tree.

The top-level `LICENSE` for first-party code is added with the bootstrap milestone; until
then this file is the authority for component terms. First-party files in this repository
carry `SPDX-License-Identifier: BSD-3-Clause` (files written before this registry exists
carry the same terms in their file header).

## Vendored trees (`third-party/`, pinned submodules)

| Unit | License | Notes |
|---|---|---|
| `threadx` | MIT | Eclipse ThreadX, `LICENSE.txt` at the tree root |
| `FreeRTOS-Kernel` | MIT | `LICENSE.md` at the tree root |
| `CMSIS_6` | Apache-2.0 | `LICENSE` at the tree root |
| `CMSIS-FreeRTOS` | Apache-2.0 AND MIT | `LICENSE` at the tree root carries both |
| `cherryusb` | Apache-2.0 | `LICENSE` at the tree root |
| `lwip` | BSD-3-Clause style | `COPYING` at the tree root (Swedish Institute of Computer Science three-clause block) |
| `FreeRTOS-Kernel-tests` | MIT | the test tree and its `CMock` submodule both carry MIT text |
| `rtl8xxxu` | **GPL-2.0-only** | per-file `SPDX-License-Identifier: GPL-2.0-only` on every extracted file (13/13); no other SPDX form in the tree. Mainline Linux driver for the RTL8188EU family, extracted byte-faithfully from Linux 7.3-rc2. See "Distribution obligations" below |
| `rtw89` | **GPL-2.0 OR BSD-3-Clause** | per-file dual SPDX on every extracted file (107/107, the only SPDX form in the tree); no separate license file at the tree root; each bus unit states `MODULE_LICENSE("Dual BSD/GPL")`. Mainline Linux SoftMAC driver for the RTL8851BU family, extracted byte-faithfully from Linux 7.3-rc2. This project takes the **BSD-3-Clause option** for it. See "Distribution obligations" below |
| `rtw88` | **GPL-2.0 OR BSD-3-Clause** | dual SPDX on 96 `.c`/`.h` files and the `Makefile` (97 of 98 tracked files); the one exception is `Kconfig`, whose line is `GPL-2.0-only` - it is build metadata and is not compiled; no separate license file at the tree root; each bus unit states `MODULE_LICENSE("Dual BSD/GPL")`. Mainline Linux SoftMAC driver for the RTL8821CU family, extracted byte-faithfully from Linux 7.3-rc2. This project takes the **BSD-3-Clause option** for it. See "Distribution obligations" below |
| `rtl8189fs` | **GPL-2.0-only** | no SPDX line anywhere and no separate license file; 518 of the 532 tracked files carry the Realtek "version 2 of the GNU General Public License" block at the top, 2 are kernel-derived GPL-2.0 (`os_dep/linux/rhashtable.{c,h}`), 2 carry no license header and 10 are build metadata or scripts; no second license form is present, and the tree needs `grep -a` (ISO-8859 bytes). Realtek vendor driver for the RTL8189FTV (RTL8188F) SDIO part, vendored byte-faithfully from the fork's `rtl8189fs` branch. See "Distribution obligations" below |

## Imported units (`components/`)

| Unit | License | Notes |
|---|---|---|
| `net80211` | BSD-3-Clause (`net80211/`, NetBSD per-file blocks) and BSD-2-Clause (`port/`, `compat/`, `crypto/` per-file blocks) | per-directory summary kept in `components/net80211/LICENSES/README.md`; the retired driver directory of the upstream library was ISC (not present in this tree) |
| `skbuff` | Apache-2.0 | TizenRT/Realtek lineage, `LICENSE.Apache-2.0` kept next to the unit |
| `linuxkpi` | BSD-3-Clause (code files); `skbuff/skbuff_pool.h` self-describes an Apache-2.0 lineage | declarations of the Linux kernel API surface, written first-party; the pool body `skbuff/skbuff_pool.c` is first-party BSD-3-Clause written against the Apache-2.0 TizenRT/Realtek fact source (lineage and the change ledger are in `components/linuxkpi/skbuff/`) |
| `cherrysh` | Apache-2.0 | `LICENSE` at the unit root |
| `printf` | MIT | `LICENSE` at the unit root (mpaland/printf) |
| `ulog` | MIT | `LICENSE` at the unit root (rdpoor/ulog, Copyright (c) 2019 Robert Poor); the level engine behind the console log outlets |
| `tlsf` | BSD-3-Clause | terms are in the `tlsf.h` copyright header (including the endorsement clause); no separate license file |
| `fsl_sdmmc` | BSD-3-Clause | NXP copyright blocks (plus the lab's in-tree modifications) on the vendored `common/`, `mmc/`, `sd/`, `sdio/`, `osa/` files; `COPYING-BSD-3` is kept next to the unit and `MANIFEST.sha256` anchors the bytes. Imported byte-faithfully from the RK3568 lab standalone SDK (see `components/fsl_sdmmc/PROVENANCE.md`) |
| `fatfs` | FatFs license (BSD-style, one condition) | ChaN FatFs R0.16; `LICENSE.txt` is kept at the unit root and `MANIFEST.sha256` anchors the seven vendored files. The configuration header (`ffconf.h`) is **not** vendored - it lives in the first-party port `ports/fatfs/` (see `components/fatfs/PROVENANCE.md`) |

## Binaries and data

| Unit | License | Notes |
|---|---|---|
| `rtl8188eufw.bin` (Realtek firmware, compiled in as `components/linuxkpi/fw/rtl8188eufw_blob.c`) | Realtek binary redistribution terms | unmodified binary and therefore redistributable; the notice is kept at `components/linuxkpi/fw/rtl8188eufw.LICENCE.txt` and the bytes are anchored by the digest in `components/linuxkpi/fw/MANIFEST.sha256` |
| `rtw8851b_fw-1.bin` (Realtek firmware, compiled in as `components/linuxkpi/fw/rtw8851b_fw_blob.c`) | Realtek binary redistribution terms (the same notice as the 8188EU image) | unmodified binary and therefore redistributable; the notice is kept at `components/linuxkpi/fw/rtw8851b_fw.LICENCE.txt` and the bytes are anchored by the digest in `components/linuxkpi/fw/MANIFEST.sha256` |
| `rtw88/rtw8821c_fw.bin` (Realtek firmware, compiled in as `components/linuxkpi/fw/rtw8821c_fw_blob.c`) | Realtek binary redistribution terms (the same notice as the 8188EU and 8851B images) | unmodified binary and therefore redistributable; the notice is kept at `components/linuxkpi/fw/rtw8821c_fw.LICENCE.txt` and the bytes are anchored by the digest in `components/linuxkpi/fw/MANIFEST.sha256` |
| `docs/boards/rk3568-e4ap5g1-itx/` (board photographs and the block diagram) | board vendor's user manual for the RK3568-E4AP5G1-ITX (IndiumEngine) | reproduced to document the hardware this project targets; vendor documentation, not first-party, and not covered by the project's BSD-3-Clause license. No redistribution grant is recorded for it: remove this directory and the figures it backs in `docs/boards/rk3568-e4ap5g1-itx.md` before a public release unless a grant is confirmed |

## Distribution obligations

* First-party source files keep their own terms (BSD-3-Clause). They are not relicensed by
  being combined with a copyleft unit, and a source-only distribution of first-party files
  is unaffected.
* `third-party/rtl8xxxu` is GPL-2.0-only. A distributed **binary image** that contains it is
  a combined work under those terms: distribution requires the corresponding source of the
  combined image and the GPL text, and the image may not be distributed under more
  restrictive terms than GPL-2.0-only allows. This obligation attaches to the distributed
  image, not to the first-party source files individually.
* The GPL unit is built as its own translation units and touches first-party code only
  through header interfaces; first-party text is never merged into a GPL file. Keeping the
  driver a pinned submodule with zero local patches is what makes the obligation auditable.
* `third-party/rtw89` is offered as `GPL-2.0 OR BSD-3-Clause` and this project takes the
  **BSD-3-Clause option** for it. A distributed binary image containing it therefore
  carries the BSD-3-Clause obligations - the copyright notice, the license text and the
  disclaimer travel with the distribution, and no contributor's name is used to endorse
  anything - and no GPL text is added for this unit, because the GPL arm is not the one
  taken. A future patch to this tree written in GPL-only form would put that combined work
  back under the GPL arm.
* `third-party/rtw88` is offered as `GPL-2.0 OR BSD-3-Clause` and this project takes the
  **BSD-3-Clause option** for it, on the same footing as `rtw89`: a distributed binary
  image containing it carries the BSD-3-Clause obligations, and no GPL text is added for
  this unit. The single `GPL-2.0-only` file in the tree is `Kconfig`, which is not compiled
  into any image and therefore does not pull the combined work under the GPL arm; if a
  future patch to this tree is written in GPL-only form, that changes.
* `third-party/rtl8189fs` is GPL-2.0-only, on the same footing as `rtl8xxxu`: a distributed
  **binary image** that contains it is a combined work under those terms - distribution
  requires the corresponding source of the combined image and the GPL text, and the image
  may not be distributed under more restrictive terms than GPL-2.0-only allows. The
  obligation attaches to the distributed image, not to the first-party source files
  individually. The unit is built as its own translation units and touches first-party code
  only through header interfaces, first-party text is never merged into a GPL file, and it
  stays a pinned submodule with zero local patches.
* Units whose license is unknown or absent are not vendored. A new unit adds a row here in
  the same commit that vendors it.

## Rules for changes

* Vendoring a unit, importing a component or adding a binary blob requires a row in this
  file in the same commit; the reviewer checks the row against the tree's own license
  files.
* Copyleft units enter this repository only against an explicit project ruling, and the
  ruling is recorded in the repository charter; the default is to keep them out of the
  tree and use them as read-only references.