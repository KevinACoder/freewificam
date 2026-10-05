#ifndef _LINUXKPI_LINUX_BCD_H_
#define _LINUXKPI_LINUX_BCD_H_
#include "../../linuxkpi_core.h"

static __always_inline __unused unsigned int
bcd2bin(unsigned int bcd)
{
	return (bcd >> 4) * 10 + (bcd & 0x0f);
}

static __always_inline __unused unsigned int
bin2bcd(unsigned int bin)
{
	return ((bin / 10) << 4) | (bin % 10);
}

#endif
