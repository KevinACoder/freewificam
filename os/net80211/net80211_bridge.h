/*
 * net80211_bridge.h - the shell-facing API of the rtw88/net80211 bridge
 * (os/net80211/lkpi_bridge.c). Consumed by port/adapters/rtw88's wlanrtw
 * command so the shell world never touches net80211 types.
 */

#ifndef NET80211_BRIDGE_H
#define NET80211_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

int	net80211_bridge_up(void);
int	net80211_bridge_scan(const uint8_t *ssid, size_t len);
/* join the named BSS from the scan table (W4 open: SSID must be in the
 * table, privacy flags cleared, ni_authmode OPEN) */
int	net80211_bridge_connect(const uint8_t *ssid, size_t len);
int	net80211_bridge_get_hwaddr(uint8_t addr[6]);
void	net80211_bridge_status(void);
void	net80211_bridge_scan_dump(void);

#endif /* NET80211_BRIDGE_H */
