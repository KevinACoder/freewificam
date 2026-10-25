/*
 * @file
 * @brief Interrupt-priority shells for the imported stack.
 *
 * The ports do not emulate interrupt priorities: exclusion is carried by the
 * port serializer (wlan_port_serializer_lock/unlock around every driver entry),
 * so the spl*() family is a scheduling barrier in name only and the interrupt
 * path only defers (see osal/cmsis2/README notes and the port README). The
 * imported code also probes cpu_intr_p() inside KASSERT()s, which this port
 * compiles out (KASSERT is sizeof-based in the prelude).
 *
 * @design kernel-services shim: same declaration face as the component's compat
 * header (which in turn included a board-specific <hal/ipl.h>; this port owns
 * the ipl type itself, so no board header is needed).
 */

#ifndef _WLAN_SYS_INTR_H_
#define _WLAN_SYS_INTR_H_

#include <sys/types.h>

typedef int ipl_t;

#ifndef IPL_NONE
#define IPL_NONE 0
#endif
#ifndef IPL_SOFTNET
#define IPL_SOFTNET 1
#endif
#ifndef IPL_SOFTSERIAL
#define IPL_SOFTSERIAL 1
#endif
#ifndef IPL_NET
#define IPL_NET 2
#endif

ipl_t splraiseipl(ipl_t);
ipl_t splnet(void);
ipl_t splsoftserial(void);
void splx(ipl_t);
bool cpu_intr_p(void);
bool cpu_softintr_p(void);

/* Deferred work: the port has no soft-interrupt thread of its own; the driver
 * round wires this to the port worker when the first consumer appears. No
 * net80211 translation unit in the current compile set calls it. */
#define SOFTINT_NET 1
void *softint_establish(int flags, void (*func)(void *), void *arg);
void softint_schedule(void *sih);

#endif /* _WLAN_SYS_INTR_H_ */
