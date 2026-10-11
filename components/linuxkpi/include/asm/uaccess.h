/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    asm/uaccess.h
 * @brief   User-space memory access: the copy-in/copy-out pair and the range check
 *          drivers put in front of them.
 *
 * @design declaration-only compatibility face, transcribed from the interface's
 *         asm-generic/uaccess.h and include/linux/uaccess.h. The interface's contract is
 *         a byte count left uncopied: `raw_copy_from_user`/`raw_copy_to_user` (the arch
 *         primitives, asm-generic/uaccess.h:80-95) return "the amount left to copy", and
 *         the block forms on top of them (include/linux/uaccess.h:217-231) return that
 *         same count, so a caller's `if (copy_from_user(...))` means "some bytes did not
 *         arrive". This port has a single address space - the driver runs as a plain
 *         thread on the image with no user/kernel split - so there is no address a copy
 *         can fail on, and the honest body is the plain byte copy with a zero result,
 *         which is exactly the interface's own NOMMU form (asm-generic/uaccess.h:80-95,
 *         CONFIG_UACCESS_MEMCPY). `access_ok()` is the same statement one layer up: the
 *         interface's generic check documents that architectures without an MMU "should
 *         always return true" (include/asm-generic/access_ok.h:22-23,44-46), and that is
 *         what this port is. A port that gains a real user/kernel split must give these
 *         bodies a faulting copy and a real access_ok(), not just one of the two.
 *         Only the forms the compiled sources name are carried; get_user()/put_user(),
 *         the string copies and the nofault forms arrive with a caller that needs them.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_ASM_UACCESS_H_
#define _LINUXKPI_ASM_UACCESS_H_

#include "../../linuxkpi_core.h"

/*
 * [LNX] arm64's access_ok() (arch/arm64/include/asm/uaccess.h:38-51) over the generic
 * helper (include/asm-generic/access_ok.h:38-46): `static inline int access_ok(const
 * void __user *addr, unsigned long size)`, likely-wrapped. With no MMU the generic
 * helper answers true for every range, and the port keeps the call sites' shape and the
 * interface's type (int), not its comparison.
 */
static __always_inline __unused int
access_ok(const void __user *addr, unsigned long size)
{
	(void)addr;
	(void)size;
	return 1;
}

/*
 * [LNX] include/asm-generic/uaccess.h:80-95 (CONFIG_UACCESS_MEMCPY form): the arch
 * primitives copy and return the number of bytes left uncopied. Declared as the
 * `unsigned long __must_check` the interface's prototypes use
 * (include/linux/uaccess.h:59-60 states the contract in words).
 */
static __always_inline __unused unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	memcpy(to, (const void __force *)from, n);
	return 0;
}

static __always_inline __unused unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	memcpy((void __force *)to, from, n);
	return 0;
}

/*
 * [LNX] include/linux/uaccess.h:126-156: `__copy_from_user_inatomic()`,
 * `__copy_from_user()`, `__copy_to_user_inatomic()` and `__copy_to_user()`, all
 * `static __always_inline __must_check unsigned long (...)`. On this port they are the
 * same single address space copy as the raw primitives; they stay separate names
 * because that is how the interface's call sites spell "already checked, do not
 * zero-pad" and the names must exist for those call sites to compile.
 */
static __always_inline __unused unsigned long
__copy_from_user_inatomic(void *to, const void __user *from, unsigned long n)
{
	return raw_copy_from_user(to, from, n);
}

static __always_inline __unused unsigned long
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return raw_copy_from_user(to, from, n);
}

static __always_inline __unused unsigned long
__copy_to_user_inatomic(void __user *to, const void *from, unsigned long n)
{
	return raw_copy_to_user(to, from, n);
}

static __always_inline __unused unsigned long
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return raw_copy_to_user(to, from, n);
}

/*
 * [LNX] include/linux/uaccess.h:217-231:
 * `static __always_inline unsigned long __must_check copy_from_user(void *to, const
 * void __user *from, unsigned long n)` and copy_to_user()'s twin. The interface wraps
 * the object-size check and `_copy_from_user()`; with no user address space there is
 * nothing to check, and the copy itself is the zero-residue copy above - so the caller's
 * "did any byte fail to move" test reads false, which is the truth on this port.
 */
static __always_inline __unused unsigned long
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return raw_copy_from_user(to, from, n);
}

static __always_inline __unused unsigned long
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return raw_copy_to_user(to, from, n);
}

#endif /* _LINUXKPI_ASM_UACCESS_H_ */
