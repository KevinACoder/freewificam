/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    face_contract.c
 * @brief   One translation unit that includes the whole first-party Linux API face on
 *          its own, so the face is compiled at a stricter warning level than the
 *          vendored bytes it serves and its wire-format claims are asserted here.
 *
 * @design contract translation unit, the same shape as the linuxkpi USB suite's
 *         contract check (tests/host/linuxkpi-usb-test, whose contract-check target
 *         compiles the component's headers as one unit): the headers are the product of
 *         this round, so they must stand without a driver including them, and the sizes
 *         that reach the hardware must be asserted rather than assumed. Reference:
 *         RELEASE_STANDARDS.md section 4 ("size and ownership contracts made
 *         compile-time").
 * @date    08.10.2026
 */

#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/average.h>
#include <linux/iopoll.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

/*
 * The moving average is a macro a driver stamps out; instantiating it here proves the
 * macro expands with a driver's arguments.
 */
DECLARE_EWMA(contract_rssi, 10, 16);

/*
 * Wire-format claims. These offsets are cast onto bytes that go into the hardware, so
 * a change in the header layouts must fail here rather than on the air.
 */
_Static_assert(sizeof(struct ethhdr) == 14, "ethernet header size");
_Static_assert(sizeof(struct ieee80211_hdr) == 30, "802.11 header with the fourth address");
_Static_assert(sizeof(struct ieee80211_qos_hdr) == 26, "three-address QoS header");
_Static_assert(sizeof(struct ieee80211_qos_hdr_4addr) == 32, "four-address QoS header");
_Static_assert(offsetof(struct ieee80211_mgmt, u) == 24, "management header prefix");
_Static_assert(offsetof(struct ieee80211_mgmt, u.action.category) == 24,
	       "action category follows the management header");
_Static_assert(offsetof(struct ieee80211_mgmt, u.action.action_code) == 25,
	       "action code follows the category");
_Static_assert(sizeof(struct ieee80211_rate) == 12, "band rate entry");
_Static_assert(sizeof(struct ieee80211_tx_rate) == 3, "packed transmit rate entry");

/* One symbol, so the unit produces an object and the face's linkage is exercised. */
int rtl8xxxu_face_contract_present;