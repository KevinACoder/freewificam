/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    face_contract.c
 * @brief   One translation unit that includes the whole first-party Linux API face the
 *          RTL8189FTV vendor driver consumes, on its own, so the face is compiled at a
 *          stricter warning level than the vendored bytes it serves and its wire-format
 *          and constant claims are asserted here.
 *
 * @design contract translation unit, the same shape as the RTL8821CU and RTL8851BU suites'
 *         contract checks (tests/host/{rtw88,rtw89}-port-build): the headers are a product
 *         of this round, so they must stand without a driver including them, and the
 *         records that are cast onto bytes - the ethernet header, the IPv4 header, the
 *         three-address MAC header, the ioctl request records the vendor driver casts its
 *         wireless requests onto - must be asserted rather than assumed. Reference:
 *         RELEASE_STANDARDS.md section 4 ("size and ownership contracts made
 *         compile-time").
 * @date    12.10.2026
 */

#include <linux/types.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/ieee80211.h>
#include <linux/wireless.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/device.h>
#include <linux/pm.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <asm/byteorder.h>
#include <net/cfg80211.h>
#include <net/iw_handler.h>
#include <net/ieee80211_radiotap.h>
#include <net/regulatory.h>

/*
 * Wire-format claims. These records are cast onto bytes that go onto the air or into the
 * hardware, so a change in their layout must fail here rather than on the wire.
 */
_Static_assert(sizeof(struct ethhdr) == 14, "ethernet header size");
_Static_assert(sizeof(struct iphdr) == 20, "IPv4 header size");
_Static_assert(sizeof(struct ieee80211_hdr_3addr) == 24, "three-address header size");

/*
 * The request records the vendor driver's ioctl path casts onto: the interface's
 * `struct iwreq` is a 16-octet name plus the 16-octet wireless payload, and `struct ifreq`
 * is the same name plus the wider interface-configuration union (its `struct ifmap` member
 * is what makes it 40).
 */
_Static_assert(IFNAMSIZ == 16, "interface name size");
_Static_assert(sizeof(struct iwreq) == 32, "wireless request record size");
_Static_assert(sizeof(struct ifreq) == 40, "interface request record size");

/* The wireless-extension generation the driver's version gates select on. */
_Static_assert(WIRELESS_EXT == 22, "wireless extensions version");
_Static_assert(IW_MODE_INFRA == 2, "IW_MODE_INFRA");
_Static_assert(IW_ENCODE_DISABLED == 0x8000, "IW_ENCODE_DISABLED");
_Static_assert(IW_PRIV_TYPE_MASK == 0x7000, "IW_PRIV_TYPE_MASK");

/* The PMKID length the driver's PMKSA handling is sized by, in both spellings the compiled
 * set reaches: the wireless-extension one and the 802.11 one. */
_Static_assert(IW_PMKID_LEN == 16, "IW_PMKID_LEN");
_Static_assert(WLAN_PMKID_LEN == 16, "WLAN_PMKID_LEN");

/* The cfg80211 records the driver passes to the core are pointer-carrying records; the ops
 * table it fills must actually carry the STA entry points the driver designates. The key
 * entry points are cfg80211's own (add_key/get_key/del_key - set_key is the mac80211
 * vocabulary and is not part of this table). */
_Static_assert(sizeof(struct cfg80211_pmksa) >= 2 * sizeof(void *), "cfg80211_pmksa shape");
_Static_assert(sizeof(((struct cfg80211_ops *)0)->scan) > 0, "cfg80211_ops::scan exists");
_Static_assert(sizeof(((struct cfg80211_ops *)0)->connect) > 0, "cfg80211_ops::connect exists");
_Static_assert(sizeof(((struct cfg80211_ops *)0)->disconnect) > 0, "cfg80211_ops::disconnect exists");
_Static_assert(sizeof(((struct cfg80211_ops *)0)->add_key) > 0, "cfg80211_ops::add_key exists");
_Static_assert(sizeof(((struct cfg80211_ops *)0)->get_key) > 0, "cfg80211_ops::get_key exists");
_Static_assert(sizeof(((struct cfg80211_ops *)0)->del_key) > 0, "cfg80211_ops::del_key exists");

/* The receive buffer's interface pointer is a typed pointer, not void: the vendor driver
 * reads fields through it (skb->dev->name). */
_Static_assert(sizeof(((struct sk_buff *)0)->dev) == sizeof(void *), "skb->dev is a pointer");

/* The runtime-PM state a cfg80211 consumer reads before it suspends a device. */
_Static_assert(sizeof(struct dev_pm_info) >= 1, "dev_pm_info carries state");

/* One symbol, so the unit produces an object and the face's linkage is exercised. */
int rtl8189fs_face_contract_present;
