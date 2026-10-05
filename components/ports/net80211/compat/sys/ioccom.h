/*
 * @file
 * @brief ioctl encoding, kept compatible with the NetBSD layout.
 */

#ifndef _SYS_IOCCOM_H_
#define _SYS_IOCCOM_H_

#include <sys/types.h>

#define IOCPARM_MASK 0x1fff
#define IOCPARM_SHIFT 16
#define IOCGROUP_SHIFT 8
#define IOCPARM_LEN(x) (((x) >> IOCPARM_SHIFT) & IOCPARM_MASK)
#define IOCBASECMD(x) ((x) & ~(IOCPARM_MASK << IOCPARM_SHIFT))
#define IOCGROUP(x) (((x) >> IOCGROUP_SHIFT) & 0xff)
#define IOC_VOID 0x20000000
#define IOC_OUT 0x40000000
#define IOC_IN 0x80000000
#define IOC_DIRMASK 0xe0000000

#define IOC(inout, group, num, len) \
	((unsigned long) ((inout) | (((len) & IOCPARM_MASK) << IOCPARM_SHIFT) | \
	((group) << IOCGROUP_SHIFT) | (num)))
/* upstream's spelling: audio(4)'s ioctl dispatch mixes IOCGROUP() with the
 * _IOC form, so both have to resolve */
#define _IOC(inout, group, num, len) IOC(inout, group, num, len)
#define _IO(g, n) IOC(IOC_VOID, (g), (n), 0)
#define _IOR(g, n, t) IOC(IOC_OUT, (g), (n), sizeof(t))
#define _IOW(g, n, t) IOC(IOC_IN, (g), (n), sizeof(t))
#define _IOWR(g, n, t) IOC(IOC_IN | IOC_OUT, (g), (n), sizeof(t))

#endif /* _SYS_IOCCOM_H_ */
