/*
 * @file   scmi_clk.c
 * @brief  SCMI clock-protocol probe over the SMC transport (N7 R5b).
 *
 * The cluster clocks are TF-A property on this board: upstream wires cpu0
 * to <&scmi_clk SCMI_CLK_CPU> (rk356x-base.dtsi:57) and never to a CRU
 * clock, and the FIT's BL31 ships the service ("clk_scmi_cpu" strings in
 * bios/u-boot-rockchip/bl31.elf). A rate set through protocol 0x14 lets
 * vendor firmware perform the APLL relock and the vdd_cpu coupling inside
 * EL3 - the exact sequences the N7 R5a round proved lethal from the running
 * core when hand-rolled (slow mode = instant death, bare relock = undervolt
 * death; runs/20261002-lwip-gigabit R5a). Linux never proved the channel
 * here (KI-017: SCMI-enabled kernels wedge in coldplug), so this module is
 * the first controlled probe: read-only commands first, rate set last.
 *
 * Transport (dual-sourced, spec discipline P5-e lesson 1):
 *   - channel: SMC 0x82000010 with page/offset = 0,0
 *     (linux drivers/firmware/arm_scmi/transports/smc.c:242-247)
 *   - message area: struct scmi_shared_mem at 0x0010f000
 *     (shmem.c:26-37; rk356x-base.dtsi:186-190)
 *   - header bitfields: msg_id[7:0] msg_type[9:8] protocol[17:10]
 *     (common.h:77-87); command type = 0
 *   - clock protocol messages (clock.c:19-34): PROTOCOL_VERSION=0x0,
 *     CLOCK_ATTRIBUTES=0x3, CLOCK_RATE_SET=0x5, CLOCK_RATE_GET=0x6;
 *     the rate-set payload is {flags, id, value_low, value_high}
 *     (struct scmi_clock_set_rate, clock.c:663-692)
 *   - SCMI_CLK_CPU = 0 (dt-bindings/clock/rk3568-cru.h:488)
 *
 * Nothing runs at boot. The shell surface is explicit: `scmi ver|attr|get`
 * are read-only; `scmi set <mhz>` accepts only the OPP ladder rows
 * (rk3568.dtsi:27-56: 408/600/816/1104/1416/1608/1800); `freq` decodes the
 * CRU state locally without firmware involvement.
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "log.h"

#include "cherrysh_adapter.h"
#include "csh.h"

#define SCMI_SHMEM_BASE		0x0010f000UL
#define SCMI_SMC_FUNC_ID	0x82000010UL
#define SCMI_PROTO_CLOCK	0x14u
#define SCMI_CLK_CPU_ID		0u

#define SCMI_MSG_VERSION	0x0u
#define SCMI_MSG_CLK_ATTRS	0x3u
#define SCMI_MSG_RATE_SET	0x5u
#define SCMI_MSG_RATE_GET	0x6u

/* struct scmi_shared_mem (shmem.c:26-37) as a uint32_t index map. */
#define SHM_CHANNEL_STATUS	1u	/* bit0: CHANNEL_FREE */
#define SHM_FLAGS		4u
#define SHM_LENGTH		5u
#define SHM_MSG_HEADER		6u
#define SHM_MSG_PAYLOAD		7u

/* OPP ladder rows (rk3568.dtsi:15-62) - the only rates `scmi set` takes. */
static const unsigned int scmi_opp_mhz[] = {
	408u, 600u, 816u, 1104u, 1416u, 1608u, 1800u,
};

static volatile uint32_t *const s_shm = (volatile uint32_t *)SCMI_SHMEM_BASE;

static long scmi_smc(void)
{
	register uint64_t r0 __asm__("x0") = SCMI_SMC_FUNC_ID;
	register uint64_t r1 __asm__("x1") = 0u;	/* channel page */
	register uint64_t r2 __asm__("x2") = 0u;	/* channel offset */

	__asm__ __volatile__("dsb sy" ::: "memory");
	/* x4-x17 are caller-saved under the SMC calling convention. */
	__asm__ __volatile__("smc #0"
			     : "+r"(r0)
			     : "r"(r1), "r"(r2)
			     : "x3", "x4", "x5", "x6", "x7", "x8", "x9",
			       "x10", "x11", "x12", "x13", "x14", "x15",
			       "x16", "x17", "memory", "cc");
	return (long)r0;
}

/* 0 on success, negative SMCCC/SCMI codes on failure. BL31 processes the
 * message synchronously inside the SMC: no doorbell polling anywhere. */
static int scmi_xfer(uint32_t msg_id, const uint32_t *tx, uint32_t tx_words,
		     uint32_t *rx, uint32_t rx_words)
{
	uint32_t i;
	int32_t status;

	s_shm[0u] = 0u;			/* reserved slot */
	s_shm[SHM_CHANNEL_STATUS] = 1u;	/* CHANNEL_FREE */
	s_shm[SHM_FLAGS] = 0u;		/* no completion interrupt wanted */
	s_shm[SHM_MSG_HEADER] = (SCMI_PROTO_CLOCK << 10) | (msg_id & 0xffu);
	s_shm[SHM_LENGTH] = 4u + tx_words * 4u;
	for (i = 0u; i < tx_words; i++) {
		s_shm[SHM_MSG_PAYLOAD + i] = tx[i];
	}

	if (scmi_smc() != 0L) {
		return -1000;	/* SMCCC call refused */
	}
	if ((s_shm[SHM_CHANNEL_STATUS] & 1u) == 0u) {
		return -1001;	/* platform left the channel busy */
	}
	status = (int32_t)s_shm[SHM_MSG_PAYLOAD];
	if (status != 0) {
		return (int)status;	/* SCMI error codes are negative */
	}
	for (i = 0u; i < rx_words; i++) {
		rx[i] = s_shm[SHM_MSG_PAYLOAD + 1u + i];
	}
	return 0;
}

/* CRU readback for `freq` - decode only, no writes anywhere. The APLL
 * fields mirror cpu_boost.c's tables (three live-PLL dumps cross-checked
 * there: APLL 0x2044/0x1441 = 816 MHz, GPLL = 1188 MHz). */
#define CRU_BASE		0xfdd20000UL
#define CRU_APLL_CON0		(CRU_BASE + 0x00u)
#define CRU_APLL_CON1		(CRU_BASE + 0x04u)
#define CRU_CLKSEL_CON(n)	(CRU_BASE + 0x100u + (n) * 4u)

static uint32_t mmio_rd32(uint32_t addr)
{
	return *(volatile uint32_t *)(uintptr_t)addr;
}

static void cmd_freq_report(chry_shell_t *csh)
{
	uint32_t sel0 = mmio_rd32(CRU_CLKSEL_CON(0));
	uint32_t con0 = mmio_rd32(CRU_APLL_CON0);
	uint32_t con1 = mmio_rd32(CRU_APLL_CON1);
	uint32_t sel3 = mmio_rd32(CRU_CLKSEL_CON(3));
	uint32_t sel4 = mmio_rd32(CRU_CLKSEL_CON(4));
	uint32_t fbdiv = con0 & 0xfffu;
	uint32_t pd1 = (con0 >> 12) & 0x7u;
	uint32_t refdiv = con1 & 0x3fu;
	uint32_t pd2 = (con1 >> 6) & 0x3u;
	uint32_t dsmpd = (con1 >> 12) & 0x1u;
	uint32_t lock = (con1 >> 10) & 0x1u;

	if (dsmpd != 0u) {
		refdiv = 1u;
	}
	if ((refdiv == 0u) || (pd1 == 0u) || (pd2 == 0u)) {
		csh_printf(csh, "cpufreq: CRU decode hit a zero divider "
				 "(con0=0x%08x con1=0x%08x)\n", con0, con1);
		return;
	}

	if ((sel0 & (1u << 6)) != 0u) {
		/* armclk mux on GPLL; APLL factors are latched but idle. */
		csh_printf(csh, "cpufreq: parent=GPLL 1188 MHz "
				 "(apll fbdiv=%u pd1=%u refdiv=%u pd2=%u "
				 "dsmpd=%u lock=%u)\n",
			   fbdiv, pd1, refdiv, pd2, dsmpd, lock);
	} else {
		csh_printf(csh, "cpufreq: parent=APLL %u MHz (fbdiv=%u "
				 "pd1=%u refdiv=%u pd2=%u dsmpd=%u lock=%u)\n",
			   (unsigned)(24u * fbdiv / refdiv / pd1 / pd2),
			   fbdiv, pd1, refdiv, pd2, dsmpd, lock);
	}
	csh_printf(csh, "cpufreq: CLKSEL3=0x%08x (atclk/32=%u gic/32=%u) "
			 "CLKSEL4=0x%08x (pclk/32=%u periph/32=%u)\n",
		   sel3, sel3 & 0x1fu, (sel3 >> 8) & 0x1fu,
		   sel4, sel4 & 0x1fu, (sel4 >> 8) & 0x1fu);
}

static void scmi_print_err(chry_shell_t *csh, const char *what, int err)
{
	if (err == -1000) {
		csh_printf(csh, "scmi: %s FAILED (SMC refused, a0!=0)\n", what);
	} else if (err == -1001) {
		csh_printf(csh, "scmi: %s FAILED (channel left busy)\n", what);
	} else {
		csh_printf(csh, "scmi: %s FAILED (SCMI status %d)\n", what, err);
	}
}

static int cmd_scmi(int argc, char **argv)
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);

	if ((argc < 2) || (strcmp(argv[1], "help") == 0)) {
		csh_printf(csh, "usage: scmi ver|attr|get|set <mhz>|freq\n"
				 "  ver/attr/get  read-only BL31 probe\n"
				 "  set <mhz>     OPP ladder only "
				 "(408/600/816/1104/1416/1608/1800)\n"
				 "  freq          local CRU decode, "
				 "no firmware involved\n");
		return 0;
	}

	if (strcmp(argv[1], "freq") == 0) {
		cmd_freq_report(csh);
		return 0;
	}

	if (strcmp(argv[1], "ver") == 0) {
		uint32_t rx[1];
		int err = scmi_xfer(SCMI_MSG_VERSION, NULL, 0u, rx, 1u);

		if (err != 0) {
			scmi_print_err(csh, "PROTOCOL_VERSION", err);
			return 0;
		}
		csh_printf(csh, "scmi: clock protocol version 0x%08x\n", rx[0]);
		return 0;
	}

	if (strcmp(argv[1], "attr") == 0) {
		uint32_t tx = SCMI_CLK_CPU_ID;
		uint32_t rx[5];	/* attributes + 16-byte name */
		char name[17];
		int err = scmi_xfer(SCMI_MSG_CLK_ATTRS, &tx, 1u, rx, 5u);
		uint32_t i;

		if (err != 0) {
			scmi_print_err(csh, "CLOCK_ATTRIBUTES", err);
			return 0;
		}
		for (i = 0u; i < 4u; i++) {
			name[i * 4u + 0u] = (char)(rx[1u + i] & 0xffu);
			name[i * 4u + 1u] = (char)((rx[1u + i] >> 8) & 0xffu);
			name[i * 4u + 2u] = (char)((rx[1u + i] >> 16) & 0xffu);
			name[i * 4u + 3u] = (char)((rx[1u + i] >> 24) & 0xffu);
		}
		name[16u] = '\0';
		csh_printf(csh, "scmi: clk %u attrs=0x%08x name='%s'\n",
			   SCMI_CLK_CPU_ID, rx[0], name);
		return 0;
	}

	if (strcmp(argv[1], "get") == 0) {
		uint32_t tx = SCMI_CLK_CPU_ID;
		uint32_t rx[2];
		uint64_t rate;
		int err = scmi_xfer(SCMI_MSG_RATE_GET, &tx, 1u, rx, 2u);

		if (err != 0) {
			scmi_print_err(csh, "CLOCK_RATE_GET", err);
			return 0;
		}
		rate = ((uint64_t)rx[1] << 32) | (uint64_t)rx[0];
		csh_printf(csh, "scmi: clk %u rate = %u Hz (%u.%03u MHz)\n",
			   SCMI_CLK_CPU_ID,
			   (unsigned)(rate & 0xffffffffu),
			   (unsigned)(rate / 1000000u),
			   (unsigned)((rate / 1000u) % 1000u));
		return 0;
	}

	if (strcmp(argv[1], "set") == 0) {
		uint32_t tx[4];
		unsigned int mhz;
		unsigned int i;
		uint64_t rate;
		int err;

		if (argc < 3) {
			csh_printf(csh, "usage: scmi set <mhz> (OPP ladder)\n");
			return 0;
		}
		mhz = (unsigned int)atoi(argv[2]);
		for (i = 0u; i < sizeof(scmi_opp_mhz) / sizeof(scmi_opp_mhz[0]);
		     i++) {
			if (scmi_opp_mhz[i] == mhz) {
				break;
			}
		}
		if (i == sizeof(scmi_opp_mhz) / sizeof(scmi_opp_mhz[0])) {
			csh_printf(csh, "scmi: %u MHz is not an OPP row\n",
				   mhz);
			return 0;
		}

		rate = (uint64_t)mhz * 1000000u;
		tx[0] = 0u;	/* flags: synchronous */
		tx[1] = SCMI_CLK_CPU_ID;
		tx[2] = (uint32_t)(rate & 0xffffffffu);
		tx[3] = (uint32_t)(rate >> 32);
		err = scmi_xfer(SCMI_MSG_RATE_SET, tx, 4u, NULL, 0u);
		if (err != 0) {
			scmi_print_err(csh, "CLOCK_RATE_SET", err);
			return 0;
		}
		csh_printf(csh, "scmi: rate-set %u MHz accepted, "
				 "verify with 'scmi get' + 'freq'\n", mhz);
		return 0;
	}

	csh_printf(csh, "scmi: unknown subcommand '%s'\n", argv[1]);
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_scmi, scmi, "scmi ver|attr|get|set <mhz>|freq",
			  "SCMI clock probe (BL31 carrier) + CRU readback");
