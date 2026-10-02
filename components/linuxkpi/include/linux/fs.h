#ifndef _LINUXKPI_LINUX_FS_H_
#define _LINUXKPI_LINUX_FS_H_
/* [S6] B8 / W0L-S1: shapes live in the core; CONFIG_EFUSE_CONFIG_FILE pulls
 * this funnel unconditionally. */
#include "../../linuxkpi_core.h"

#define	O_RDONLY		0x0000
#define	O_WRONLY		0x0001
#define	O_RDWR			0x0002
#define	O_CREAT			0x0040
#define	O_APPEND		0x0400

#define	MAX_NON_LFS		((1UL << 31) - 1)
#endif
