/*
 * @file
 * @brief The include_next target of the port's compat <sys/cdefs.h>.
 *
 * components/net80211/ports/compat/sys/cdefs.h loads the compiler/libc glue with
 *     #include_next <sys/cdefs.h>
 * so that the host or target copy keeps owning __THROW/__BEGIN_DECLS and
 * friends.  The kernel build has no libc <sys/cdefs.h>; this file is the
 * target, and it deliberately carries nothing: the port's own header defines
 * the NetBSD vocabulary right after this include.
 */

#ifndef _WLAN_LINUX_SYS_CDEFS_H_
#define _WLAN_LINUX_SYS_CDEFS_H_

#endif /* _WLAN_LINUX_SYS_CDEFS_H_ */
