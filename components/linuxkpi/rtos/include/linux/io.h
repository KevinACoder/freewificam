/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/io.h
 * @brief   The include a driver's MMIO section names; the accessors themselves
 *          are the architecture's.
 *
 * @design declaration-only compatibility face, and a pure forward. The interface
 *         (include/linux/io.h) is a thin layer over the architecture's asm/io.h:
 *         it adds the typed copy helpers and the memremap family and includes the
 *         architecture header for the actual reads and writes. This port's
 *         architecture header (asm/io.h) already carries the raw and the ordered
 *         accessors a compiled consumer names, so this header is the include-path
 *         spelling of that set - one definition, reached by the interface's own
 *         name. The interface reaches it from include/linux/pci.h:40, which is how
 *         a PCI driver's register access compiles without naming asm/io.h; the
 *         PCI face includes this header for the same reason.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    13.10.2026
 */

#ifndef _LINUXKPI_LINUX_IO_H_
#define _LINUXKPI_LINUX_IO_H_

#include "../../linuxkpi_core.h"
#include <asm/io.h>

#endif /* _LINUXKPI_LINUX_IO_H_ */
