/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   hostap_rx.c
 * @brief  Host semantic case for the hostapd RX seam: the cfg80211_rx_mgmt_ext
 *         wrap's filter/queue and the task-context drain.
 *
 * What this proves on the host: only an AP vif's management frames are taken
 * over (everything else goes to the real function), the frame is copied whole,
 * the queue is FIFO and bounded (a full queue drops the newcomer, it does not
 * overwrite), and the drain hands each frame to hostapd's delivery entry in
 * order with the frame's frequency.  What it cannot prove: that hostapd's MLME
 * accepts the frames (that is A2 on the board).
 */
#include <string.h>

#include "kpi_test.h"
#include "linuxkpi_core.h"
#include <linux/errno.h>
#include <net/cfg80211.h>
#include "linux80211/hostap_ap.h"

/* the --wrap entry (hostap_rx_wrap.c); the bench calls it directly */
bool __wrap_cfg80211_rx_mgmt_ext(struct wireless_dev *wdev,
				 struct cfg80211_rx_info *info);

/* --- the two symbols the wrap needs from the hostap world ---------------- */

struct rx_record {
	unsigned int	len;
	int		freq;
	unsigned char	fc;	/* frame-control byte */
	unsigned char	tag;	/* byte 24: the case's ordering marker */
};

static struct rx_record rx_seen[32];
static unsigned int rx_count;
static int rx_rc;
static unsigned int real_calls;
static bool real_last_ret;

int
lkpi_hostap_mgmt_rx(const void *frame, unsigned int len, int freq)
{
	const unsigned char *p = frame;

	if (rx_count < (sizeof(rx_seen) / sizeof(rx_seen[0]))) {
		rx_seen[rx_count].len = len;
		rx_seen[rx_count].freq = freq;
		rx_seen[rx_count].fc = p[0];
		rx_seen[rx_count].tag = len > 24u ? p[24] : 0u;
	}
	rx_count++;
	return rx_rc;
}

bool
__real_cfg80211_rx_mgmt_ext(struct wireless_dev *wdev,
			    struct cfg80211_rx_info *info)
{
	(void)wdev;
	(void)info;
	real_calls++;
	return real_last_ret;
}

/* --- the bench ----------------------------------------------------------- */

static struct wireless_dev ap_wdev;
static struct wireless_dev sta_wdev;
static unsigned char frame[64];
static struct cfg80211_rx_info info;

static void
frame_init(unsigned char subtype, unsigned char tag)
{
	memset(frame, 0, sizeof(frame));
	frame[0] = (unsigned char)(subtype << 4);	/* mgmt, no flags */
	frame[1] = 0x00;
	frame[24] = tag;
}

static void
case_reset(void)
{
	rx_count = 0;
	rx_rc = 0;
	real_calls = 0;
	real_last_ret = true;
	memset(&ap_wdev, 0, sizeof(ap_wdev));
	memset(&sta_wdev, 0, sizeof(sta_wdev));
	ap_wdev.iftype = NL80211_IFTYPE_AP;
	sta_wdev.iftype = NL80211_IFTYPE_STATION;
	memset(&info, 0, sizeof(info));
	info.buf = frame;
	info.len = 32;
	info.freq = 2462;
}

int
kpi_case_hostap_rx(void)
{
	unsigned int i;

	/* 1. the AP vif's management frame is taken over, in order, with freq */
	case_reset();
	frame_init(0x04, 0xa1);		/* probe request */
	KPI_ASSERT_EQ(__wrap_cfg80211_rx_mgmt_ext(&ap_wdev, &info), false,
		      "AP mgmt frame is consumed by the seam");
	frame_init(0x00, 0xa2);		/* assoc request */
	__wrap_cfg80211_rx_mgmt_ext(&ap_wdev, &info);
	KPI_ASSERT_EQ(real_calls, 0u, "the real path is not called for AP mgmt");
	KPI_ASSERT_EQ(lkpi_hostap_poll(), 0, "drain succeeds");
	KPI_ASSERT_EQ(rx_count, 2u, "both frames reached hostapd");
	KPI_ASSERT_EQ(rx_seen[0].len, 32u, "the whole frame is copied");
	KPI_ASSERT_EQ(rx_seen[0].freq, 2462, "the RX frequency travels with it");
	KPI_ASSERT_EQ(rx_seen[0].fc, 0x40, "probe request first byte");
	KPI_ASSERT_EQ(rx_seen[1].fc, 0x00, "assoc request first byte");
	KPI_ASSERT_EQ(rx_seen[0].tag, 0xa1, "the probe request's bytes travel whole");
	KPI_ASSERT_EQ(rx_seen[1].tag, 0xa2, "the assoc request's bytes travel whole");

	/* 2. a station vif and a data frame go to the real function */
	case_reset();
	KPI_ASSERT_EQ(__wrap_cfg80211_rx_mgmt_ext(&sta_wdev, &info), true,
		      "station vif is not intercepted");
	KPI_ASSERT_EQ(real_calls, 1u, "the station path reaches the real call");
	/* a data frame: type 2 (0x08 in the frame-control byte), not mgmt */
	memset(frame, 0, sizeof(frame));
	frame[0] = 0x08;
	frame[1] = 0x01;
	KPI_ASSERT_EQ(__wrap_cfg80211_rx_mgmt_ext(&ap_wdev, &info), true,
		      "data frames are not intercepted");
	KPI_ASSERT_EQ(real_calls, 2u, "data frames reach the real call");

	/* 3. the queue is bounded: a full queue drops the newcomer, FIFO holds */
	case_reset();
	for (i = 0; i < 20u; i++) {
		frame_init(0x04, (unsigned char)i);
		(void)__wrap_cfg80211_rx_mgmt_ext(&ap_wdev, &info);
	}
	KPI_ASSERT_EQ(lkpi_hostap_poll(), 0, "drain succeeds after overflow");
	KPI_ASSERT_EQ(rx_count, 16u, "the queue holds exactly its capacity");
	KPI_ASSERT_EQ(rx_seen[0].tag, 0u, "FIFO: first queued is first out");
	KPI_ASSERT_EQ(rx_seen[15].tag, 15u, "FIFO: the 16th is the last kept");

	/* 4. an oversized frame is refused, not truncated */
	case_reset();
	info.len = 1500u;
	KPI_ASSERT_EQ(__wrap_cfg80211_rx_mgmt_ext(&ap_wdev, &info), false,
		      "oversized frame still consumed");
	KPI_ASSERT_EQ(lkpi_hostap_poll(), 0, "nothing to drain");
	KPI_ASSERT_EQ(rx_count, 0u, "oversized frame was refused");

	/* 5. a delivery failure is reported by the drain */
	case_reset();
	frame_init(0x00, 0xc0);
	(void)__wrap_cfg80211_rx_mgmt_ext(&ap_wdev, &info);
	rx_rc = -ENODEV;
	KPI_ASSERT_EQ(lkpi_hostap_poll(), -ENODEV, "drain reports the delivery error");

	return 0;
}
