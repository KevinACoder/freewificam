/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    face_contract.c
 * @brief   One translation unit that includes the whole first-party Linux API face the
 *          rtw88 build subset consumes, on its own, so the face is compiled at a stricter
 *          warning level than the vendored bytes it serves and its wire-format claims are
 *          asserted here.
 *
 * @design contract translation unit, the same shape as the RTL8188EU and RTL8851BU
 *         suites' contract checks (tests/host/{rtl8xxxu,rtw89}-port-build): the headers
 *         are a product of this round, so they must stand without a driver including
 *         them, and the sizes that reach the hardware must be asserted rather than
 *         assumed. Reference: RELEASE_STANDARDS.md section 4 ("size and ownership
 *         contracts made compile-time").
 * @date    11.10.2026
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
#include <linux/bitops.h>
#include <linux/bcd.h>
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
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <asm/byteorder.h>
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
_Static_assert(sizeof(struct udphdr) == 8, "UDP header size");
_Static_assert(sizeof(struct iphdr) == 20, "IPv4 header size");

/* The 11ac vocabulary the 8821CU generation names: the HT/VHT lengths and the VHT MCS
 * support enumeration are bit-level constants of the frames it builds. */
_Static_assert(FCS_LEN == 4, "FCS length");
_Static_assert(IEEE80211_MAX_MPDU_LEN_VHT_11454 == 11454, "VHT 11454 length");
_Static_assert(IEEE80211_VHT_MCS_SUPPORT_0_7 == 0, "VHT MCS 0-7");
_Static_assert(IEEE80211_VHT_MCS_SUPPORT_0_8 == 1, "VHT MCS 0-8");
_Static_assert(IEEE80211_VHT_MCS_SUPPORT_0_9 == 2, "VHT MCS 0-9");
_Static_assert(RX_FLAG_NO_PSDU == (1u << 29), "the NO_PSDU receiver flag bit");

/* The bit helpers the driver's rate and bitmap arithmetic is built on. bcd2bin is a
 * macro and folds at compile time; ffs()/fls() are functions, so they are exercised at
 * runtime below rather than in a static assertion. */
_Static_assert(bcd2bin(0x42) == 42, "bcd2bin of a compile-time constant");

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

/* The two bit-position helpers are functions: 1-based positions of the lowest and
 * highest set bit, the contract the driver's rate arithmetic assumes. */
static int face_contract_bit_positions(void)
{
	return (ffs(0x10) == 5 && fls(0x10) == 5) ? 1 : 0;
}

/* One symbol, so the unit produces an object and the face's linkage is exercised. */
int rtw88_face_contract_present;
void *(*rtw88_face_contract_alloc)(void) = face_contract_alloc_object;
void *(*rtw88_face_contract_alloc_ptr)(struct ieee80211_tx_info *) = face_contract_alloc_pointer;
unsigned int (*rtw88_face_contract_offset)(void) = face_contract_flex_offset;
int (*rtw88_face_contract_bits)(void) = face_contract_bit_positions;
