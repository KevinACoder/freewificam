/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   cpu_freq.c
 * @brief  CPU clock control through the board firmware's SCMI clock protocol.
 *
 * The framing follows the SCMI shared-memory transport the Linux GT tree uses
 * (drivers/firmware/arm_scmi/{shmem.c,transports/smc.c,clock.c}), reduced to
 * the three messages this board needs:
 *
 *   CLOCK_ATTRIBUTES (0x3)   tx: u32 clock id        rx: u32 attributes
 *   CLOCK_RATE_GET   (0x6)   tx: u32 clock id        rx: u64 rate, Hz
 *   CLOCK_RATE_SET   (0x5)   tx: flags,id,lo,hi      rx: (none)
 *
 * Shared-memory layout (32-bit words, all Device-nGnRE on this image):
 *   +0  reserved
 *   +4  channel_status   bit0 CHANNEL_FREE, bit1 CHANNEL_ERROR
 *   +8  reserved
 *   +c  reserved
 *   +10 flags            0 = polling (no completion interrupt)
 *   +14 length           4 (header) + tx payload bytes
 *   +18 msg_header       bits[7:0] msg, [9:8] type, [17:10] protocol,
 *                        [27:18] token
 *   +1c msg_payload      status u32 first, response from +4
 *
 * The sequence is: wait FREE, mark busy, fill, DSB, SMC, poll the returned
 * header's token plus FREE/ERROR, read status, copy the response, release the
 * channel. Every wait is bounded by the 24 MHz counter; the calls run in
 * thread context and the firmware answers synchronously on this SoC, so the
 * poll usually succeeds on the first read.
 *
 * Board evidence for the transport itself is the Linux GT tree, not a guess:
 * cpufreq-dt instantiates from the CPU nodes' operating-points-v2, calls
 * clk_set_rate on this SCMI clock, and K2-11 measured T0 changing with the
 * frequency it set (196k at 1992 MHz, 148k at 1104 MHz). This driver is the
 * RTOS side of the same mechanism, and it reports failure instead of guessing
 * when the firmware refuses.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_conf.h"
#include "cpu_freq.h"
#include "generic_timer.h"

/* --- SCMI protocol constants ------------------------------------------------ */

#define SCMI_PROTOCOL_CLOCK	0x14u
#define SCMI_MSG_VERSION	0x0u
#define SCMI_MSG_ATTRIBUTES	0x3u
#define SCMI_MSG_RATE_SET	0x5u
#define SCMI_MSG_RATE_GET	0x6u
#define SCMI_MSG_GET_PERMISSIONS 0xFu
#define SCMI_CLOCK_ID_CPU	0u

#define SCMI_STATUS_SUCCESS	0u

#define SCMI_CHAN_FREE		(1u << 0)
#define SCMI_CHAN_ERROR		(1u << 1)

/* CLOCK_GET_PERMISSIONS bits. Attributes' bit 29 is NOT this; the permission
 * word is a separate message (0xF), and Linux only queries it on protocol
 * revision >= 3 when the attributes advertise it. When the query is not
 * advertised, Linux treats rate control as ALLOWED (the flag defaults to
 * false) and lets the rate_set answer for itself - this driver does the
 * same, so a quiet firmware cannot make the arm look read-only by omission. */
#define SCMI_PERM_RATE_CONTROL		(1u << 29)

#define SCMI_HDR_MSG_MASK	0xffu
#define SCMI_HDR_TOKEN_SHIFT	18u
#define SCMI_HDR_TOKEN_MASK	(0x3ffu << SCMI_HDR_TOKEN_SHIFT)

#define SCMI_RX_TIMEOUT_MS	30u

/* Clock attributes bit 29: the firmware permits rate_set on this clock. */
#define SCMI_CLOCK_ATTR_RATE_CTRL_ALLOWED	(1u << 29)

/* --- shared-memory window --------------------------------------------------- */

struct scmi_shmem {
	volatile uint32_t reserved0;
	volatile uint32_t channel_status;
	volatile uint32_t reserved1[2];
	volatile uint32_t flags;
	volatile uint32_t length;
	volatile uint32_t msg_header;
	volatile uint32_t msg_payload[64];
};

#define SCMI_SHMEM ((volatile struct scmi_shmem *)BOARD_SCMI_SHMEM_BASE)

/* --- state ------------------------------------------------------------------ */

static uint32_t g_token;
static uint32_t g_rate_hz;
static uint32_t g_smc_ret;
static uint32_t g_status;
static uint32_t g_attr;
static uint32_t g_perm;
static uint32_t g_ver;
static uint32_t g_chan0;
static bool g_probed;
static bool g_supported;
static bool g_broken;

static const uint32_t g_opps[CPU_FREQ_OPP_COUNT] = {
	408000000u, 600000000u, 816000000u, 1104000000u,
	1416000000u, 1608000000u, 1800000000u, 1992000000u,
};

/* --- SMCCC call ------------------------------------------------------------- */

static void cpu_dsb(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
}

static long scmi_smc(uint64_t func, uint64_t a1, uint64_t a2)
{
	register uint64_t x0 __asm__("x0") = func;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;

	/* SMCCC: x0-x17 are caller-saved, so the compiler must not keep anything
	 * live there across the call; "cc" because the firmware may return with
	 * arbitrary condition flags. */
	__asm__ volatile("smc #0"
			 : "+r"(x0)
			 : "r"(x1), "r"(x2)
			 : "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17", "cc", "memory");
	return (long)x0;
}

/* --- bounded wait ----------------------------------------------------------- */

static uint64_t scmi_deadline(void)
{
	uint64_t now = gtimer_count_get();
	uint64_t freq = gtimer_freq_get();

	return now + (freq * SCMI_RX_TIMEOUT_MS) / 1000u;
}

static bool scmi_expired(uint64_t deadline)
{
	return gtimer_count_get() >= deadline;
}

static uint32_t scmi_header(uint32_t msg, uint32_t token)
{
	return (msg & SCMI_HDR_MSG_MASK) |
	       ((uint32_t)SCMI_PROTOCOL_CLOCK << 10) |
	       ((token & 0x3ffu) << SCMI_HDR_TOKEN_SHIFT);
}

/* One request/response. Returns the SCMI status (0 = success), or -1 for a
 * transport timeout, or -2 when the SMC conduit itself says "not supported". */
static int scmi_xfer(uint32_t msg, const uint32_t *tx, unsigned int tx_words,
		     uint32_t *rx, unsigned int rx_words)
{
	volatile struct scmi_shmem *shm = SCMI_SHMEM;
	uint32_t token = ++g_token & 0x3ffu;
	uint64_t deadline;
	unsigned int i;
	uint32_t status;

	if (g_broken)
		return -1;

	deadline = scmi_deadline();
	while ((shm->channel_status & SCMI_CHAN_FREE) == 0u) {
		if (scmi_expired(deadline)) {
			g_broken = true;
			return -1;
		}
	}

	shm->channel_status = 0u;	/* mark busy, clear error */
	shm->flags = 0u;		/* polling: no completion interrupt */
	shm->length = 4u + tx_words * 4u;
	shm->msg_header = scmi_header(msg, token);
	for (i = 0u; i < tx_words; i++)
		shm->msg_payload[i] = tx[i];
	cpu_dsb();

	g_smc_ret = (uint32_t)scmi_smc(BOARD_SCMI_SMC_ID, 0u, 0u);
	if (g_smc_ret != 0u)
		return -2;		/* conduit refused, not a protocol error */

	deadline = scmi_deadline();
	for (;;) {
		uint32_t header = shm->msg_header;
		uint32_t chan = shm->channel_status;

		if ((header & SCMI_HDR_TOKEN_MASK) ==
			    (token << SCMI_HDR_TOKEN_SHIFT) &&
		    (chan & (SCMI_CHAN_FREE | SCMI_CHAN_ERROR)) != 0u)
			break;
		if (scmi_expired(deadline)) {
			/* The firmware may still own the window: do not overwrite it
			 * in a later request, and let the caller report the timeout. */
			g_broken = true;
			return -1;
		}
	}
	cpu_dsb();

	status = shm->msg_payload[0];
	g_status = status;
	for (i = 0u; i < rx_words; i++)
		rx[i] = shm->msg_payload[1u + i];
	shm->channel_status = SCMI_CHAN_FREE;	/* release */
	cpu_dsb();

	if (status != SCMI_STATUS_SUCCESS)
		return -1;
	return 0;
}

/* --- public API ------------------------------------------------------------- */

int cpu_freq_probe(void)
{
	uint32_t id = SCMI_CLOCK_ID_CPU;
	uint32_t attr = 0u;
	uint32_t rate[2] = { 0u, 0u };
	int ret;

	if (g_probed)
		return g_supported ? 0 : -1;
	g_probed = true;
	g_chan0 = SCMI_SHMEM->channel_status;

	/* Protocol version: diagnostic only (the permission message is gated on
	 * revision 3.x, and the test arm must not depend on it). */
	(void)scmi_xfer(SCMI_MSG_VERSION, NULL, 0u, &g_ver, 1u);

	ret = scmi_xfer(SCMI_MSG_ATTRIBUTES, &id, 1u, &attr, 1u);
	g_attr = attr;
	if (ret != 0) {
		g_supported = false;
		return ret;
	}

	/* Permissions: when the query succeeds and withholds rate control, the
	 * firmware has said no and there is nothing to guess. When it does not
	 * answer at all, a rate_set is still attempted (Linux's default). */
	ret = scmi_xfer(SCMI_MSG_GET_PERMISSIONS, &id, 1u, &g_perm, 1u);
	if (ret == 0 && (g_perm & SCMI_PERM_RATE_CONTROL) == 0u) {
		g_supported = false;
		return -2;
	}

	ret = scmi_xfer(SCMI_MSG_RATE_GET, &id, 1u, rate, 2u);
	if (ret != 0) {
		g_supported = false;
		return ret;
	}
	g_rate_hz = rate[0];
	g_supported = true;
	return 0;
}

bool cpu_freq_supported(void)
{
	return g_supported;
}

uint32_t cpu_freq_get_hz(void)
{
	uint32_t id = SCMI_CLOCK_ID_CPU;
	uint32_t rate[2] = { 0u, 0u };

	if (scmi_xfer(SCMI_MSG_RATE_GET, &id, 1u, rate, 2u) == 0)
		g_rate_hz = rate[0];
	return g_rate_hz;
}

int cpu_freq_set_hz(uint32_t hz)
{
	uint32_t id = SCMI_CLOCK_ID_CPU;
	uint32_t tx[4];
	uint32_t actual;
	uint32_t diff;

	if (!g_supported)
		return -2;

	tx[0] = 0u;			/* flags: synchronous set */
	tx[1] = id;
	tx[2] = hz;			/* low 32 bits */
	tx[3] = 0u;			/* high 32 bits */
	if (scmi_xfer(SCMI_MSG_RATE_SET, tx, 4u, NULL, 0u) != 0)
		return -1;

	actual = cpu_freq_get_hz();
	/* A readback is the contract: a rate_set that the firmware accepted but
	 * did not apply must not be reported as a successful arm. */
	diff = (actual > hz) ? (actual - hz) : (hz - actual);
	if (diff > hz / 100u)
		return -3;
	return 0;
}

uint32_t cpu_freq_last_smc(void)
{
	return g_smc_ret;
}

uint32_t cpu_freq_last_status(void)
{
	return g_status;
}

uint32_t cpu_freq_attributes(void)
{
	return g_attr;
}

uint32_t cpu_freq_permissions(void)
{
	return g_perm;
}

uint32_t cpu_freq_protocol_version(void)
{
	return g_ver;
}

uint32_t cpu_freq_channel_status(void)
{
	return g_chan0;
}

const uint32_t *cpu_freq_opps(unsigned int *count)
{
	if (count != NULL)
		*count = CPU_FREQ_OPP_COUNT;
	return g_opps;
}
