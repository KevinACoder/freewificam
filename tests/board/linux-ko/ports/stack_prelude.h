/*
 * @file
 * @brief Forced-include wrapper for the net80211-world translation units.
 *
 * The port's own prelude (components/net80211/ports/compat/prelude.h) does the real work.
 * This wrapper exists for one include-order fact of the kernel build: the
 * kernel's include path precedes the module's -I list, so <net/route.h> would
 * resolve to the kernel's routing header instead of the component's small
 * compat face. The compat header is pulled in here by its full path, and the
 * kernel header's guard is defined away on the command line (-D_ROUTE_H), so
 * the later <net/route.h> include is a no-op. The compat face carries only the
 * RTM_IEEE80211_* constants and <sys/socket.h>.
 */

#ifndef _WLAN_LINUX_STACK_PRELUDE_H_
#define _WLAN_LINUX_STACK_PRELUDE_H_

#include "components/net80211/ports/compat/prelude.h"
#include "components/net80211/compat/netbsd/net/route.h"

#endif /* _WLAN_LINUX_STACK_PRELUDE_H_ */
