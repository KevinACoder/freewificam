/*
 * @file    wlan_wpa_obs.c
 * @brief   The supplicant's message observer for this image: it registers on
 *          the supplicant's message callback and latches the anchors the join
 *          case and the judge watch for.
 *
 * @design observer over the public message seam - the same mechanism the host
 *         port suite uses (tests/host/wpa-port-test/main.c's msg_capture):
 *         wpa_msg_register_cb is the supplicant's own public hook for
 *         structured progress messages, so the case never parses the console
 *         and never reaches into the supplicant's internals.
 *
 * @author  zhugengyu
 * @date    09.10.2026
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <includes.h>

#include "utils/common.h"
#include "utils/wpa_debug.h"

#include "wlan_wpa_obs.h"

static volatile int obs_negotiation;
static volatile int obs_connected;
static volatile int obs_assoc;
static char obs_bssid[18];

static int
obs_contains(const char *hay, size_t hay_len, const char *needle)
{
	size_t n = strlen(needle);
	size_t i;

	if (n == 0 || hay_len < n) {
		return 0;
	}
	for (i = 0; i + n <= hay_len; i++) {
		if (memcmp(hay + i, needle, n) == 0) {
			return 1;
		}
	}
	return 0;
}

/* the message callback: runs on the supplicant's thread, so it only latches
 * flags and copies bytes - no printing, no blocking */
static void
wlan_wpa_obs_msg(void *ctx, int level, enum wpa_msg_type type,
    const char *txt, size_t len)
{

	(void)ctx;
	(void)level;
	(void)type;

	if (obs_contains(txt, len, "CTRL-EVENT-CONNECTED") ||
	    obs_contains(txt, len, "CTRL-EVENT-ASSOC-REJECT")) {
		obs_assoc = 1;
	}
	if (obs_contains(txt, len, "[PTK=CCMP GTK=CCMP]")) {
		obs_negotiation = 1;
	}
	if (obs_contains(txt, len, "CTRL-EVENT-CONNECTED")) {
		/* An open network installs no key, so the PTK line above never
		 * appears and a case that waits for it would reject a link that
		 * is already usable.  The connected event is the point at which
		 * the supplicant reports the association complete for both the
		 * keyed and the open case. */
		obs_negotiation = 1;
		/* "... completed [id=0 id_str=]" or the older "completed" form:
		 * the BSSID sits right after "Connection to " when present */
		const char *p = txt;
		size_t i;

		obs_connected = 1;
		for (i = 0; i + 17 <= len; i++) {
			if (memcmp(p + i, "Connection to ", 14) == 0) {
				memcpy(obs_bssid, p + i + 14, 17);
				obs_bssid[17] = '\0';
				break;
			}
		}
	}
}

void
wlan_wpa_obs_init(void)
{

	obs_negotiation = 0;
	obs_connected = 0;
	obs_assoc = 0;
	obs_bssid[0] = '\0';
	wpa_msg_register_cb(wlan_wpa_obs_msg);
}

int
wlan_wpa_obs_negotiation_completed(void)
{

	return obs_negotiation;
}

int
wlan_wpa_obs_connected(void)
{

	return obs_connected;
}

const char *
wlan_wpa_obs_bssid_txt(void)
{

	return obs_bssid[0] != '\0' ? obs_bssid : "?";
}

int
wlan_wpa_obs_assoc_seen(void)
{

	return obs_assoc;
}
