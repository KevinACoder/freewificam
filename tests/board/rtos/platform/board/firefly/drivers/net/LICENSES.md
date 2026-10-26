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
