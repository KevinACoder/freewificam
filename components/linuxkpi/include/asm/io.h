/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    asm/io.h
 * @brief   The architecture's I/O entry point: the raw MMIO accessors for a
 *          single-address-space port.
 *
 * @design declaration-only compatibility face, transcribed from the interface's
 *         architecture I/O header (arch/arm64/include/asm/io.h:26-96, which defines
 *         __raw_readb/__raw_readw/__raw_readl/__raw_readq and the __raw_write* twins as
 *         a volatile access at the caller's address) and from the generic fallbacks
 *         (include/asm-generic/io.h:115-176). On a machine with no MMU window to remap,
 *         the raw accessor is the volatile access itself, which is what both the
 *         interface's arch file and its generic fallback say; the ordered/little-endian
 *         forms (readb()/readl(), include/asm-generic/io.h:188-249) are that access
 *         followed by a byte swap, and the port is little-endian, so they arrive with
 *         the first caller that needs them rather than being stated twice now. The
 *         vendored driver reaches this header through osdep_service.h but its Linux
 *         platform layer names no MMIO accessor on the USB path, which is why the set
 *         here is the raw one. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_ASM_IO_H_
#define _LINUXKPI_ASM_IO_H_

#include "../../linuxkpi_core.h"

/* [LNX] arch/arm64/include/asm/io.h:26-52 */
static __always_inline __unused void
__raw_writeb(u8 val, volatile void __iomem *addr)
{
	*(volatile u8 __iomem *)addr = val;
}

static __always_inline __unused void
__raw_writew(u16 val, volatile void __iomem *addr)
{
	*(volatile u16 __iomem *)addr = val;
}

static __always_inline __unused void
__raw_writel(u32 val, volatile void __iomem *addr)
{
	*(volatile u32 __iomem *)addr = val;
}

static __always_inline __unused void
__raw_writeq(u64 val, volatile void __iomem *addr)
{
	*(volatile u64 __iomem *)addr = val;
}

/* [LNX] arch/arm64/include/asm/io.h:54-98 */
static __always_inline __unused u8
__raw_readb(const volatile void __iomem *addr)
{
	return *(const volatile u8 __iomem *)addr;
}

static __always_inline __unused u16
__raw_readw(const volatile void __iomem *addr)
{
	return *(const volatile u16 __iomem *)addr;
}

static __always_inline __unused u32
__raw_readl(const volatile void __iomem *addr)
{
	return *(const volatile u32 __iomem *)addr;
}

static __always_inline __unused u64
__raw_readq(const volatile void __iomem *addr)
{
	return *(const volatile u64 __iomem *)addr;
}

#endif /* _LINUXKPI_ASM_IO_H_ */
