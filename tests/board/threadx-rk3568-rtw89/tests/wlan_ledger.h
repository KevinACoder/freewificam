/*
 * @file   wlan_ledger.h
 * @brief  The USB transfer ledger: one compact line a bring-up prints to say
 *         whether the receive path is moving.
 *
 * The engine's per-endpoint bookkeeping and the CherryUSB adapter's counters are
 * the two halves of one question: did the transfer leave, and did anything come
 * back?  They are printed together, from one place, so the probe case, the scan
 * case and the radio pump's periodic reading cannot drift apart.
 */

#ifndef WLAN_LEDGER_H
#define WLAN_LEDGER_H

/*
 * Print one line: the endpoint table's totals, then the adapter's submit/stage
 * counters.  `tag` names the caller's moment ("probe", "scan", "sweep") so a
 * captured log reads as a sequence.
 */
void wlan_ledger_usb(const char *tag);

#endif /* WLAN_LEDGER_H */
