# Component licenses

This registry records the license of every vendored, extracted or imported unit in this
repository, together with the obligations that follow from them. It exists so that the
provenance of each byte in a built image is answerable without reading every tree.

The top-level `LICENSE` for first-party code is added with the bootstrap milestone; until
then this file is the authority for component terms. First-party files in this repository
carry `SPDX-License-Identifier: BSD-2-Clause` (files written before this registry exists
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

## Imported units (`components/`)

| Unit | License | Notes |
|---|---|---|
| `net80211` | BSD-3-Clause (`net80211/`, NetBSD per-file blocks) and BSD-2-Clause (`port/`, `compat/`, `crypto/` per-file blocks) | per-directory summary kept in `components/net80211/LICENSES/README.md`; the retired driver directory of the upstream library was ISC (not present in this tree) |
| `skbuff` | Apache-2.0 | TizenRT/Realtek lineage, `LICENSE.Apache-2.0` kept next to the unit |
| `linuxkpi` | BSD-2-Clause (code files); `skbuff/skbuff_pool.h` self-describes an Apache-2.0 lineage | declarations of the Linux kernel API surface, written first-party; the pool body `skbuff/skbuff_pool.c` is first-party BSD-2-Clause written against the Apache-2.0 TizenRT/Realtek fact source (lineage and the change ledger are in `components/linuxkpi/skbuff/`) |
| `cherrysh` | Apache-2.0 | `LICENSE` at the unit root |
| `printf` | MIT | `LICENSE` at the unit root (mpaland/printf) |
| `tlsf` | BSD-3-Clause | terms are in the `tlsf.h` copyright header (including the endorsement clause); no separate license file |

## Binaries and data

| Unit | License | Notes |
|---|---|---|
| `rtl8188eufw.bin` (Realtek firmware, compiled in as `components/linuxkpi/fw/rtl8188eufw_blob.c`) | Realtek binary redistribution terms | unmodified binary and therefore redistributable; the notice is kept at `components/linuxkpi/fw/rtl8188eufw.LICENCE.txt` and the bytes are anchored by the digest in `components/linuxkpi/fw/MANIFEST.sha256` |

## Distribution obligations

* First-party source files keep their own terms (BSD-2-Clause). They are not relicensed by
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
* Units whose license is unknown or absent are not vendored. A new unit adds a row here in
  the same commit that vendors it.

## Rules for changes

* Vendoring a unit, importing a component or adding a binary blob requires a row in this
  file in the same commit; the reviewer checks the row against the tree's own license
  files.
* Copyleft units enter this repository only against an explicit project ruling, and the
  ruling is recorded in the repository charter; the default is to keep them out of the
  tree and use them as read-only references.