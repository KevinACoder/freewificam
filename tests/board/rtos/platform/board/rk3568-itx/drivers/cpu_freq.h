/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   cpu_freq.h
 * @brief  CPU clock control through the board firmware's SCMI clock protocol.
 *
 * The RK3568's four Cortex-A55s take their clock from the SCMI clock domain
 * exposed by TF-A (protocol 0x14, clock id 0). Linux reaches it through
 * cpufreq-dt; this driver reaches it directly over the SCMI SMC transport the
 * board DTB declares (arm,scmi-smc, smc-id 0x82000010, shmem 0x10f000). There
 * is no CRU poking here: the clock belongs to the firmware, and a direct CRU
 * write would disagree with the OPP voltages the firmware already knows.
 *
 * The driver is deliberately narrow: probe the channel, read the operating
 * point, set a rate, read it back. Verification (PMCCNTR effective-MHz check,
 * OPP sweep, restore) is the caller's job, because the cycle counter lives in
 * the app tier (app/net_prof.h) and a board driver must not depend on it.
 *
 * Error contract:
 *   0   success
 *  -1   SCMI transport error (channel timeout, protocol status != 0)
 *  -2   rate control not allowed (CLOCK_ATTRIBUTES bit 29 clear) or SMC
 *       returned "not supported"
 *  -3   readback does not match the requested rate within 1%
 * No function blocks forever: every wait is bounded by the counter.
 */

#ifndef FREEWIFICAM_CPU_FREQ_H
#define FREEWIFICAM_CPU_FREQ_H

#include <stdbool.h>
#include <stdint.h>

/* The eight OPPs in the board DTB (Hz). The firmware accepts a rate in Hz; a
 * caller that wants a different rate may try it, but these are the values the
 * board's OPP table declares and the ones the round records. */
#define CPU_FREQ_OPP_COUNT	8u

/* Probe the SCMI channel and read the current rate. Idempotent. */
int cpu_freq_probe(void);

/* True when the firmware answered the attributes query and allowed rate_set. */
bool cpu_freq_supported(void);

/* Current rate as reported by CLOCK_RATE_GET, in Hz (0 before a probe). */
uint32_t cpu_freq_get_hz(void);

/* Set the CPU clock in Hz and confirm with a readback. */
int cpu_freq_set_hz(uint32_t hz);

/* The OPP table, in Hz, ascending. */
const uint32_t *cpu_freq_opps(unsigned int *count);

/* Diagnostics for the probe line: what the firmware actually answered. A
 * nonzero SMC return means the conduit refused the call (NOT_SUPPORTED); a
 * zero return with a clear attribute bit 29 means the clock itself is
 * read-only. Both are reported as data, not guessed at. */
uint32_t cpu_freq_last_smc(void);
uint32_t cpu_freq_last_status(void);
uint32_t cpu_freq_attributes(void);
uint32_t cpu_freq_permissions(void);
uint32_t cpu_freq_protocol_version(void);
uint32_t cpu_freq_channel_status(void);

#endif /* FREEWIFICAM_CPU_FREQ_H */
