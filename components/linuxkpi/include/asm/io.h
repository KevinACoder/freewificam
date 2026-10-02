#ifndef _LINUXKPI_ASM_IO_H_
#define _LINUXKPI_ASM_IO_H_
/* [S6] B8: the ioremap/io accessors live in the core (memcpy_*_io family);
 * the osdep funnel includes this unconditionally. */
#include "../linuxkpi_core.h"
#endif
