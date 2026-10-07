/*
 * @file
 * @brief ioctl(4) command-macro shell for the FreeRTOS port.
 *
 * newlib ships no <sys/ioctl.h>; the compat usb/net headers only need
 * the _IO/_IOR/_IOW/_IOWR command builders, which live in the compat
 * ioccom.h.
 *
 * @author zhugengyu
 * @date 10.09.2026
 */

#ifndef _COMPAT_SYS_IOCTL_H_
#define _COMPAT_SYS_IOCTL_H_

#include <sys/ioccom.h>

#endif /* _COMPAT_SYS_IOCTL_H_ */
