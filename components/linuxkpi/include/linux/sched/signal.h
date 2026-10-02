#ifndef _LINUXKPI_LINUX_SCHED_SIGNAL_H_
#define _LINUXKPI_LINUX_SCHED_SIGNAL_H_
/* [S6] B1/B2: the signal stubs live in the core (signal_pending()=0 etc.);
 * the funnel must resolve since LINUX_VERSION_CODE >= 4.11. */
#include "../../../linuxkpi_core.h"
#endif
