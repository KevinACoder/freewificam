# Licenses - firefly board NIC drivers

## `gmac.{c,h}` - on-SoC Synopsys DWC GMAC driver (dwmac1000-class)

SPDX-License-Identifier: BSD-3-Clause

Derived work. The register sequences (reset/config order, DMA bus and
operating mode values), the enhanced-descriptor ring contract and the MDIO
access mechanics originate in the reference standalone SDK's Ethernet MAC
driver, Copyright (C) 2022, Phytium Technology Co., Ltd. All rights
reserved (BSD-3-Clause, https://opensource.org/licenses/BSD-3-Clause). The
SDK tree is pinned outside this repository; its BSD-3-Clause notice and
disclaimer apply to the derived portions and are retained in the file
headers. The naming, the structure, the standard-clause-22 PHY handling and
the copy-based polled data path are this project's own.

## `e1000e.{c,h,_regs.h}` - Intel 82574L PCIe NIC driver

SPDX-License-Identifier: BSD-3-Clause

First-party (this repository's own work, re-imported from the reference
`firefly` branch byte-identical; see `tests/board/rtos/SOURCE.md`).

## `xmac.{c,h}` - firefly v2 on-SoC Cadence GEM-class MAC driver

SPDX-License-Identifier: BSD-3-Clause

First-party (this repository's own work). The register map, the descriptor
layout and the SerDes clock-select programming are hardware facts: the offsets
and bit positions were read from the SoC parameter map and cross-checked
against the board's working mainline driver for the same port, which runs it at
line rate. No source file was copied or translated - the ring ownership
discipline, the polled copy data path, the clause-22 PHY handling and the
diagnostic surface are written here, in this family's own shape (the sibling
`gmac.{c,h}` next to this file).

Two behaviours in `xmac.c` were settled by reading how the port is driven in
the reference standalone SDK's lwIP port (BSD-3-Clause, Copyright (C) 2022,
Phytium Technology Co., Ltd.; the same tree and notice the `gmac` entry above
records). They are silicon facts about the controller, re-expressed in this
file's own words rather than copied:

- Transmit is kicked by setting STARTTX in the network control register after
  the descriptors are handed over; there is no doorbell register in the legacy
  set.
- The transmit queue base is programmed once, before the transmitter starts,
  and is not written again while it runs. Re-pointing it mid-stream
  desynchronises the DMA from the software cursor and wedges the transmitter
  on the used-bit-read error.
