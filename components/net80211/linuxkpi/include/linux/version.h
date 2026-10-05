#ifndef _LINUXKPI_LINUX_VERSION_H_
#define _LINUXKPI_LINUX_VERSION_H_
#include "../../linuxkpi_core.h"

/* The Linux version the verbatim drivers are compiled against; the rtw88
 * dist carries 6.12-era branches (the board-proven anchor from the NetBSD
 * bring-up rounds). */
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6, 12, 0)
#define KERNEL_VERSION_STRING "6.12.0"

#endif
