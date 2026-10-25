/*
 * @file
 * @brief sys/null.h shim: the definition of NULL the NetBSD headers expect.
 *
 * The imported sys/queue.h includes <sys/null.h> when __NetBSD__ is defined
 * ("Include the definition of NULL only on NetBSD because sys/null.h is not
 * available elsewhere"). The port defines __NetBSD__, so the header must exist;
 * the C library's <stddef.h> is the real definition and this shim just maps the
 * name.
 *
 * @design port-owned target-header shim, same mechanism as machine/ansi.h.
 */

#ifndef _WLAN_SYS_NULL_H_
#define _WLAN_SYS_NULL_H_

#include <stddef.h>

#endif /* _WLAN_SYS_NULL_H_ */
