/*
 * @file
 * @brief The port-side service face: allocation seam, lifecycle, log seam.
 *
 * The imported stack reaches the port through the compat headers under
 * components/net80211/ports/compat/ and through port/port.h; this header is what the port's
 * own layers (osal, net) share among themselves.
 *
 * @design ports & adapters: the allocation seam is the adapter boundary for
 * memory. One allocation path for everything (malloc(9)/kmem(9) callers and the
 * mbuf clusters), so the memory subsystem has exactly one place to replace; the
 * host build backs it with the C library, a target build replaces
 * osal/cmsis2/osal_memory.c.
 */

#ifndef _WLAN_OSAL_H_
#define _WLAN_OSAL_H_

#include <stddef.h>

/* Bring up the port services: the tick base (hz) and the serializer. Call once
 * before the first net80211 use; safe to call again. */
void wlan_osal_init(void);

/* The serializer entry points are declared in port/port.h; this pair is the
 * port's own addition for the blocking waits (tsleep, condition variables):
 * release the serializer completely, then take it back with the depth it had. */
void wlan_port_serializer_suspend(void);
void wlan_port_serializer_resume(void);

/* ------------------------------------------------------------------ */
/* Allocation seam                                                     */
/* ------------------------------------------------------------------ */

void *wlan_osal_alloc(size_t size);
void *wlan_osal_zalloc(size_t size);
void wlan_osal_free(void *p);

/* Accounting for contracts and diagnostics: live bytes and the outstanding
 * (not yet freed) block count. A completed round trip returns both to their
 * starting values. */
unsigned long wlan_osal_live_blocks(void);
unsigned long wlan_osal_live_bytes(void);

/* ------------------------------------------------------------------ */
/* Log seam                                                            */
/* ------------------------------------------------------------------ */

/* Single output point for the imported sources (kprintf) and the port. The
 * default writes to stdout; a target build redirects it. The format attribute is
 * spelled with __printf__ because the BSD-world translation units map printf to
 * kprintf. */
void wlan_osal_log(const char *fmt, ...)
    __attribute__((format(__printf__, 1, 2)));

#endif /* _WLAN_OSAL_H_ */
