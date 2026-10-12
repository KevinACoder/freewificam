/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/regulatory.h
 * @brief   The regulatory vocabulary a driver's wiphy uses: the flag bits it sets on
 *          the wiphy, the per-rule flag bits, and the regulatory domain table it
 *          submits through wiphy_apply_custom_regulatory().
 *
 * @design declaration-only compatibility face: the flags and the domain records are
 *         transcribed with the interface's values and field order (include/net/
 *         regulatory.h) because the stack compares them against the values a driver
 *         submits and the driver's own static domain tables are written against them;
 *         the request records and the hint entry point arrive with the consumer that
 *         names them. The rule-construction macros the compiled set names are carried
 *         with the records. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_NET_REGULATORY_H_
#define _LINUXKPI_NET_REGULATORY_H_

#include "../../linuxkpi_core.h"

#include <linux/ieee80211.h>

/* [LNX] include/net/regulatory.h:160-169 (the enumerators the compiled set names, at
 * their computed positions) */
enum ieee80211_regulatory_flags {
	REGULATORY_CUSTOM_REG			= BIT(0),
	REGULATORY_STRICT_REG			= BIT(1),
	REGULATORY_DISABLE_BEACON_HINTS		= BIT(2),
	REGULATORY_COUNTRY_IE_IGNORE		= BIT(4),
};

/* [LNX] include/uapi/linux/nl80211.h:5077-5082: the DFS region enumeration the
 * regulatory domain record names. It lives in the interface's uapi header, reached
 * through this header on a Linux build; stated here, next to its only consumer. */
enum nl80211_dfs_regions {
	NL80211_DFS_UNSET	= 0,
	NL80211_DFS_FCC		= 1,
	NL80211_DFS_ETSI	= 2,
	NL80211_DFS_JP		= 3,
};

/* [LNX] include/uapi/linux/nl80211.h:5030-5059: the per-rule flags the compiled set
 * names, at their computed positions, with the interface's historical aliases. */
enum nl80211_reg_rule_flags {
	NL80211_RRF_NO_OFDM		= 1 << 0,
	NL80211_RRF_NO_CCK		= 1 << 1,
	NL80211_RRF_NO_INDOOR		= 1 << 2,
	NL80211_RRF_NO_OUTDOOR		= 1 << 3,
	NL80211_RRF_DFS			= 1 << 4,
	NL80211_RRF_PTP_ONLY		= 1 << 5,
	NL80211_RRF_PTMP_ONLY		= 1 << 6,
	NL80211_RRF_NO_IR		= 1 << 7,
	__NL80211_RRF_NO_IBSS		= 1 << 8,
	NL80211_RRF_AUTO_BW		= 1 << 11,
	NL80211_RRF_IR_CONCURRENT	= 1 << 12,
	NL80211_RRF_NO_HT40MINUS	= 1 << 13,
	NL80211_RRF_NO_HT40PLUS		= 1 << 14,
	NL80211_RRF_NO_80MHZ		= 1 << 15,
	NL80211_RRF_NO_160MHZ		= 1 << 16,
};
#define	NL80211_RRF_PASSIVE_SCAN	NL80211_RRF_NO_IR
#define	NL80211_RRF_NO_IBSS		NL80211_RRF_NO_IR
#define	NL80211_RRF_GO_CONCURRENT	NL80211_RRF_IR_CONCURRENT

/* [LNX] include/net/regulatory.h:171-182 */
struct ieee80211_freq_range {
	u32 start_freq_khz;
	u32 end_freq_khz;
	u32 max_bandwidth_khz;
};

struct ieee80211_power_rule {
	u32 max_antenna_gain;
	u32 max_eirp;
};

/* [LNX] include/net/regulatory.h:195-205 */
struct ieee80211_wmm_ac {
	u16 cw_min;
	u16 cw_max;
	u16 cot;
	u8 aifsn;
};

struct ieee80211_wmm_rule {
	struct ieee80211_wmm_ac client[IEEE80211_NUM_ACS];
	struct ieee80211_wmm_ac ap[IEEE80211_NUM_ACS];
};

/* [LNX] include/net/regulatory.h:207-214 */
struct ieee80211_reg_rule {
	struct ieee80211_freq_range freq_range;
	struct ieee80211_power_rule power_rule;
	struct ieee80211_wmm_rule wmm_rule;
	u32 flags;
	u32 dfs_cac_ms;
	bool has_wmm;
	s8 psd;
};

/* [LNX] include/net/regulatory.h:217-223 */
struct ieee80211_regdomain {
	struct rcu_head rcu_head;
	u32 n_reg_rules;
	char alpha2[3];
	enum nl80211_dfs_regions dfs_region;
	struct ieee80211_reg_rule reg_rules[];
};

/* [LNX] include/net/regulatory.h:225-244: the rule-construction macros the compiled
 * set names, with the interface's conversions. */
#define	REG_RULE_EXT(start, end, bw, gain, eirp, dfs_cac, reg_flags)	\
{									\
	.freq_range.start_freq_khz = MHZ_TO_KHZ(start),			\
	.freq_range.end_freq_khz = MHZ_TO_KHZ(end),			\
	.freq_range.max_bandwidth_khz = MHZ_TO_KHZ(bw),			\
	.power_rule.max_antenna_gain = DBI_TO_MBI(gain),		\
	.power_rule.max_eirp = DBM_TO_MBM(eirp),			\
	.flags = reg_flags,						\
	.dfs_cac_ms = dfs_cac,						\
}

#define	REG_RULE(start, end, bw, gain, eirp, reg_flags) \
	REG_RULE_EXT(start, end, bw, gain, eirp, 0, reg_flags)

#endif /* _LINUXKPI_NET_REGULATORY_H_ */
