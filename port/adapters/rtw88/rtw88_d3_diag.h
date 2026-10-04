/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * rtw88_d3_diag.h - shell face for the D-3 unicast-RX forensics group
 * (S9 W4/W5 断链轮).  The counters/dumps live in components/rtw88
 * {usb.c,rx.c,mac80211.c}; every hunk is ledgered in
 * components/rtw88/DIVERGENCES.md (D-3), removal condition:
 * 单播 RX 断链结案后整组撤除（含本头与 rtw88_cmds 的 diag/quiet）。
 */
#ifndef RTW88_D3_DIAG_H
#define RTW88_D3_DIAG_H

/* usb.c: chip->host DMA truth (unicast-mgmt classify + drop counters) */
void rtw_d3_usb_rx_diag_dump(void);
void rtw_d3_usb_rx_diag_reset(void);

/* rx.c: the five silent -EINVAL exits of rtw_rx_query_rx_desc */
void rtw_d3_query_diag_dump(void);
void rtw_d3_query_diag_reset(void);

/* mac80211.c: clear/restore RCR BIT_AM|BIT_AB (silence the beacon flood
 * for the "pump/FIFO overflow vs WMAC comparator" fork) */
void rtw_d3_rcr_quiet(int on);

/* mac80211.c: set/clear RCR BIT_AAP (accept ALL unicast) - splits
 * "chip APM comparator misjudges" from "the AP never sent the frame" */
void rtw_d3_rcr_promisc(int on);

/* linuxkpi_core.c: skb canary / 32K-churn counters */
void lkpi_skb_diag_dump(void);

#endif /* RTW88_D3_DIAG_H */
