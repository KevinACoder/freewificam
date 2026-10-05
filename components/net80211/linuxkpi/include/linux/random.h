#ifndef _LINUXKPI_LINUX_RANDOM_H_
#define _LINUXKPI_LINUX_RANDOM_H_
#include "../../linuxkpi_core.h"

long lkpi_get_random_long(void);
unsigned int lkpi_get_random_int(void);
unsigned char lkpi_get_random_u8(void);

#define	get_random_long()	lkpi_get_random_long()
#define	get_random_u8()		lkpi_get_random_u8()
#define	get_random_u32()	((u32)get_random_int())
#define	get_random_u16()	((u16)get_random_int())
#define	get_random_mask_addr(buf, addr, mask) lkpi_get_random_mask_addr((buf), (addr), (mask))
void lkpi_get_random_mask_addr(u8 *, const u8 *, const u8 *);
#define	prandom_u32()		get_random_u32()
#define	prandom_bytes(buf, n)	lkpi_prandom_bytes((buf), (n))
void lkpi_prandom_bytes(void *buf, size_t n);

#endif
