#ifndef _LINUXKPI_ASM_UACCESS_H_
#define _LINUXKPI_ASM_UACCESS_H_
/* [S6] B8 / asm aliases: user/kernel split does not exist in this image -
 * the copy family degrades to memcpy with Linux return codes. */
#include "../linuxkpi_core.h"

#define	access_ok(addr, size)		((void)(addr), (void)(size), 1)
#define	__access_ok(addr, size)		access_ok((addr), (size))

static __always_inline __unused unsigned long
copy_from_user(void *to, const void *from, unsigned long n)
{
	if (to != NULL && from != NULL && n != 0)
		memcpy(to, from, n);
	return 0;
}

static __always_inline __unused unsigned long
copy_to_user(void *to, const void *from, unsigned long n)
{
	if (to != NULL && from != NULL && n != 0)
		memcpy(to, from, n);
	return 0;
}

#define	put_user(x, ptr)	(*(ptr) = (x), 0)
#define	get_user(x, ptr)	((x) = *(ptr), 0)
#define	__put_user(x, ptr)	put_user((x), (ptr))
#define	__get_user(x, ptr)	get_user((x), (ptr))
#endif
