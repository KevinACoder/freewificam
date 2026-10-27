# Component licenses

This registry records the license of every vendored, imported or extracted unit
in this repository, with the obligations that follow. It exists so the
provenance of each byte in a built image is answerable without reading every
tree.

The top-level `LICENSE` covers the first-party code: BSD-3-Clause. First-party
files say so in their `SPDX-License-Identifier` line (or in their header when
they predate the registry). The units below keep their own terms; being
combined into one image or one module does not relicense them.

## Ported components (`third-party/`)

| Unit | Pin | License | Notes |
|---|---|---|---|
| `threadx` | `44d7c95c` (6.5.1.202602a) | MIT | `LICENSE.txt` at the tree root |
| `FreeRTOS-Kernel` | `3a22924e` (V11.3.1) | MIT | `LICENSE.md` at the tree root |
| `CMSIS_6` | `45dab712` (v6.3.0) | Apache-2.0 | `LICENSE` at the tree root |
| `CMSIS-FreeRTOS` | `4dd00351` (v11.3.0) | Apache-2.0 AND MIT | `LICENSE` at the tree root carries both |
| `cherryusb` | `51fef88` (v1.6.1-103) | Apache-2.0 | `LICENSE` at the tree root; eight patches applied in the tree, recorded in `patches/` (see below) |
| `lwip` | `77dcd25a` (STABLE-2_2_1_RELEASE) | BSD-3-Clause style | `COPYING` at the tree root (Swedish Institute of Computer Science three-clause block) |
| `fsl_sdmmc` | NXP stack, `MANIFEST.sha256` | BSD-3-Clause | NXP copyright blocks on the vendored files; `COPYING-BSD-3` kept next to the unit |
| `printf` | per-component pin | MIT | `LICENSE` at the unit root (mpaland/printf) |
| `tlsf` | per-component pin | BSD-3-Clause | terms in the `tlsf.h` copyright header |
| `ulog` | per-component pin | MIT | `LICENSE` at the unit root (rdpoor/ulog) |

The CherryUSB patches (`patches/cherryusb/0001` to `0008`) are first-party
changes to an Apache-2.0 tree. They add files and hunks in the same style as
the tree and carry the tree's own SPDX lines; they are recorded in
`third-party/cherryusb/patches/README.md` and anchored by that tree's digest.

## Wireless drivers (`wireless-drivers/`)

| Unit | Pin | License | Notes |
|---|---|---|---|
| `rtw89` | `79e12cc9` | **GPL-2.0 OR BSD-3-Clause** | dual SPDX on every extracted file; the project takes the BSD-3-Clause option |
| `rtw88` | `e71d9a28760f` | **GPL-2.0 OR BSD-3-Clause** | dual SPDX on the compiled files; the project takes the BSD-3-Clause option. `Kconfig` alone is GPL-2.0-only and is not compiled |
| `rtl8xxxu` | `e63969708249` | **GPL-2.0-only** | per-file SPDX on every extracted file |
| `iwlwifi` | `e71d9a28760f` | **GPL-2.0-only** (whole tree, by project ruling) | the tree mixes `GPL-2.0 OR BSD-3-Clause` files with `GPL-2.0-only` ones, and the `GPL-2.0-only` group is inside the compiled mvm path, so electing BSD per file buys nothing |
| `rtl8189fs` | `13bbdbc` | **GPL-2.0** | vendored in-tree; the embedded firmware array is part of the vendor driver source, not a separately extracted blob |

## First-party components (`components/`)

| Unit | License | Notes |
|---|---|---|
| `net80211` | BSD-3-Clause (`net80211/`, per-file NetBSD blocks) and BSD-2-Clause (`port/`, `compat/`, `crypto/`) | per-directory summary in `components/net80211/LICENSES/README.md` |
| `linuxkpi` | BSD-3-Clause | the Linux kernel API surface and the RTOS runtime, written first-party. The skbuff pool descends from a TizenRT/Realtek unit (Apache-2.0); the lineage and the change ledger are in `components/linuxkpi/rtos/skbuff/` |

## Firmware blobs

The RTOS images compile in vendor firmware files; the bytes are not committed.
`docs/FIRMWARE-NOTICES.md` carries each file's digest, its published source and
the redistribution terms:

| Blob | License |
|---|---|
| RTL8188EUS firmware (`rtlwifi/rtl8188eufw.bin`) | Realtek binary redistribution terms |
| RTL8723BU firmware (`rtlwifi/rtl8723bu_nic.bin`) | Realtek binary redistribution terms |
| RTL8851BU firmware (`rtw89/rtw8851b_fw-1.bin`) | Realtek binary redistribution terms |
| Intel AC 7260 firmware (`iwlwifi-7260-17.ucode`) | Intel binary redistribution terms (no reverse engineering) |

## Distribution obligations

- **GPL-2.0-only units** (`rtl8xxxu`, `iwlwifi`, `rtl8189fs`): a distributed
  **binary image** that contains one of them is a combined work under those
  terms. Distribution requires the corresponding source of the combined image
  and the GPL text, and the image may not be distributed under more restrictive
  terms. The obligation attaches to the distributed image, not to the
  first-party source files individually; they are not relicensed by being
  combined.
- **BSD-3-Clause election** (`rtw88`, `rtw89`): a distributed binary image
  carries the BSD-3-Clause obligations - the copyright notice, the license text
  and the disclaimer travel with the distribution, and no contributor's name is
  used to endorse anything. No GPL text is added for these units while the BSD
  arm is the one taken. A future patch written in GPL-only form would put that
  combined work back under the GPL arm.
- The copyleft units are compiled as their own translation units and touch
  first-party code only through header interfaces; first-party text is never
  merged into a copyleft file.
- **Firmware** is redistributed unmodified, in the form its vendor permits, and
  is not reverse engineered, decompiled or disassembled.
- Units whose license is unknown or absent are not vendored.

## Rules for changes

- Vendoring a unit, importing a component or adding a binary blob requires a row
  in this file in the same commit.
- Copyleft units enter the repository only against an explicit project ruling,
  and the ruling is recorded; the default is to keep them out of the tree and
  use them as read-only references.
- Moving a pin updates this file and the unit's `provenance` record together.
