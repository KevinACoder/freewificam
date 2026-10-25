/* SPDX-License-Identifier: BSD-3-Clause */
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
 *         forms (readl()/writel()/writeb(), include/asm-generic/io.h:188-281) are that
 *         access with the endian conversion the interface spells around it, which on
 *         this little-endian port is the identity. The vendored driver reaches this
 *         header through osdep_service.h but its Linux platform layer names no MMIO
 *         accessor on the USB path, which is why the set here is the raw one; the PCIe
 *         port's trans.c is the caller that names the ordered three. Reference for the
 *         face shape: components/linuxkpi/include/linux/usb.h:1-20.
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

/*
 * [LNX] include/asm-generic/io.h:188-281: the ordered, little-endian forms. The
 * interface reads/writes little-endian memory and returns/takes the value in CPU
 * byte order: readl() is `__le32_to_cpu((__le32 __force)__raw_readl(addr))`,
 * writel() is `__raw_writel((u32 __force)__cpu_to_le32(value), addr)`, and writeb()
 * is the raw store (a byte needs no conversion). The interface wraps each in an
 * I/O barrier (__io_br/__io_ar/__io_bw/__io_aw) that orders it against other MMIO
 * on a weakly-ordered machine; the conversion, not the barrier, is the part that
 * changes the value, and on this little-endian port __le32_to_cpu()/__cpu_to_le32()
 * are the identity, so the raw volatile access (asm/io.h above) is the whole effect
 * - stated rather than hidden, so a big-endian port would add the swap here.
 */
static __always_inline __unused u32
readl(const volatile void __iomem *addr)
{
	return __raw_readl(addr);
}

static __always_inline __unused void
writeb(u8 value, volatile void __iomem *addr)
{
	__raw_writeb(value, addr);
}

static __always_inline __unused void
writel(u32 value, volatile void __iomem *addr)
{
	__raw_writel(value, addr);
}

#endif /* _LINUXKPI_ASM_IO_H_ */
