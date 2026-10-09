/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    face_contract.c
 * @brief   One translation unit that includes the whole first-party Linux API face the
 *          rtw89 build subset consumes, on its own, so the face is compiled at a
 *          stricter warning level than the vendored bytes it serves and its wire-format
 *          claims are asserted here.
 *
 * @design contract translation unit, the same shape as the RTL8188EU suite's contract
 *         check (tests/host/rtl8xxxu-port-build): the headers are a product of this
 *         round, so they must stand without a driver including them, and the sizes that
 *         reach the hardware must be asserted rather than assumed. Reference:
 *         RELEASE_STANDARDS.md section 4 ("size and ownership contracts made
 *         compile-time").
 * @date    10.10.2026
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
#include <linux/bitfield.h>
#include <linux/workqueue.h>
#include <linux/sort.h>
#include <linux/devcoredump.h>
#include <linux/uuid.h>
#include <linux/dmi.h>
#include <linux/udp.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/acpi.h>
#include <linux/rfkill.h>
#include <linux/pci.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <net/regulatory.h>

/*
 * Wire-format and interface ABI claims. These records are cast onto bytes that go onto
 * the air or into the hardware, so a change in their layout must fail here rather than
 * on the wire.
 */
_Static_assert(sizeof(struct ethhdr) == 14, "ethernet header size");
_Static_assert(sizeof(struct ieee80211_hdr_3addr) == 24, "three-address header size");
_Static_assert(sizeof(struct ieee80211_radiotap_header) == 8, "radiotap base header");
_Static_assert(sizeof(struct ieee80211_radiotap_vht) == 12, "radiotap VHT record");
_Static_assert(sizeof(struct ieee80211_radiotap_he) == 12, "radiotap HE record");
_Static_assert(sizeof(struct ieee80211_radiotap_tlv) == 4, "radiotap TLV record");
_Static_assert(sizeof(struct ieee80211_he_cap_elem) == 17, "HE capability element");
_Static_assert(sizeof(struct udphdr) == 8, "UDP header size");
_Static_assert(sizeof(struct iphdr) == 20, "IPv4 header size");

/* The allocation family's two spellings must both compile at the size the caller expects. */
static void *face_contract_alloc_object(void)
{
	return kzalloc_obj(struct ieee80211_tx_info);
}

/* The pointer spelling of the same macro, which the driver also uses. */
static void *face_contract_alloc_pointer(struct ieee80211_tx_info *info)
{
	return kzalloc_obj(*info, GFP_KERNEL);
}

static unsigned int face_contract_flex_offset(void)
{
	return (unsigned int)offsetof(struct ieee80211_tx_info, rate_driver_data);
}

/* One symbol, so the unit produces an object and the face's linkage is exercised. */
int rtw89_face_contract_present;
void *(*rtw89_face_contract_alloc)(void) = face_contract_alloc_object;
void *(*rtw89_face_contract_alloc_ptr)(struct ieee80211_tx_info *) = face_contract_alloc_pointer;
unsigned int (*rtw89_face_contract_offset)(void) = face_contract_flex_offset;
