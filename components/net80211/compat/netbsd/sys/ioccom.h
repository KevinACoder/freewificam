/*
 * @file
 * @brief ioctl encoding, kept compatible with the NetBSD layout.
 */

#ifndef _SYS_IOCCOM_H_
#define _SYS_IOCCOM_H_

#include <sys/types.h>

#define IOCPARM_MASK 0x1fff
#define IOCPARM_LEN(x) (((x) >> 16) & IOCPARM_MASK)
#define IOC_VOID 0x20000000
#define IOC_OUT 0x40000000
#define IOC_IN 0x80000000
#define IOC_DIRMASK 0xe0000000

#define IOC(inout, group, num, len) \
	((unsigned long) ((inout) | (((len) & IOCPARM_MASK) << 16) | \
	((group) << 8) | (num)))
#define _IOR(g, n, t) IOC(IOC_OUT, (g), (n), sizeof(t))
#define _IOW(g, n, t) IOC(IOC_IN, (g), (n), sizeof(t))
#define _IOWR(g, n, t) IOC(IOC_IN | IOC_OUT, (g), (n), sizeof(t))

#endif /* _SYS_IOCCOM_H_ */
