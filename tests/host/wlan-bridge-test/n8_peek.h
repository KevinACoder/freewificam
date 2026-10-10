/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    n8_peek.h
 * @brief   The harness's scalar view into the net80211 world: scan-table
 *          contents, the stack's state, the join trigger. Scalars and byte
 *          arrays only - the same rule the port's seam carries, for the same
 *          reason (the two worlds must never share a struct).
 */

#ifndef N8_PEEK_H
#define N8_PEEK_H

#include <stdint.h>
#include <stddef.h>

/* 0 and the candidate's ssid/freq when the BSSID is in the scan table; -1 when
 * it is not */
int	n8_peek_scan_table(const uint8_t bssid[6], uint8_t *ssid,
	    size_t *ssid_len, uint32_t *freq);
/* the scan candidate's match-relevant facts */
int	n8_peek_node_info(const uint8_t bssid[6], unsigned int *capinfo,
	    unsigned int *chan_ieee, int *chan_active, unsigned int *rate_count,
	    unsigned int *esslen);
/* configure the desired SSID before a scan (the joiner's own step) */
void	n8_peek_set_ssid(const uint8_t *ssid, size_t len);
/* the stack's current state value */
int	n8_peek_state(void);
/* the state values the cases compare against (the imported numbering) */
int	n8_peek_run_state(void);
int	n8_peek_scan_state(void);
int	n8_peek_auth_state(void);
int	n8_peek_hwaddr(uint8_t addr[6]);

/* the supplicant-visible policy state the connect path sets */
int	n8_peek_roaming(void);
int	n8_peek_privacy(void);
int	n8_peek_authmode(void);
int	n8_peek_authmode_8021x(void);

/* the stack-side half of a key install: is the slot live, which index is the
 * default transmit key, and did the pairwise install authorize the node */
int	n8_peek_key_defined(int pairwise, int key_idx);
int	n8_peek_def_txkey(void);
int	n8_peek_node_authorized(void);

/* the number of candidates in the scan table */
int	n8_peek_scan_rows(void);

/* the channel index the stack currently sits on (what the beacon parser's
 * off-channel check reads); -1 when there is no interface */
int	n8_peek_curchan(void);

/* the CCMP round trip, stack-side: install the pairwise key on the joined node
 * (what the supplicant does after the handshake), clear the privacy flag again,
 * seal one AP-to-station frame with it (the peer's crypto), and read the named
 * MIC failure counter. */
int	n8_peek_install_ccmp_key(const uint8_t *key, size_t len);
void	n8_peek_clear_privacy(void);
int	n8_peek_seal_from_ap(const uint8_t *payload, size_t len, uint16_t seq,
	    uint8_t *out, size_t *out_len);
uint32_t n8_peek_ccmpmic_count(void);

/* red-control back door: clear the stack's scan flags (the state end_scan
 * leaves), simulating a scan_start that forgot to re-arm them */
void	n8_poke_clear_active_scan(void);

#endif /* N8_PEEK_H */
