/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   net_pps.c
 * @brief  Case net_pps (rk3568-itx): a serial-gated 64-byte UDP pps sink.
 *
 * The pps-bound round measures one number: T0, the highest offered 64-byte
 * UDP pps at zero loss. A userspace receiver makes that number about the
 * receiver (context switches, copies, the socket queue); this case is the
 * board-side end of the same kernel-sink accounting the Linux GT arm uses -
 * one raw UDP PCB, no sockets, no copies, no per-packet printing - plus the
 * counters needed to say where any missing frame went.
 *
 * Control is by serial, not by a TCP protocol, because the sender is the
 * canonical ppsgen over on the Pi and its peer is a plain UDP port:
 *
 *   freq <khz>     set the CPU clock through the SCMI driver, read it back,
 *                  and report the effective frequency from PMCCNTR
 *   freqtest       walk every OPP with the same verification, restore the boot
 *                  rate, and print one PASS/FAIL line
 *   go <tag>       snapshot the counters, arm the collection window
 *   stop <tag>     snapshot again and print the window
 *   halt           stop the case (the runner prints the image verdict)
 *
 * The window's duration is the sender's, not the board's: the host reads
 * `PPSGEN END secs=` from the Pi and the analyzer divides by it. The board
 * side prints its own clock readings too, but they are diagnostics - a serial
 * command can lag a sleep by 0.2 s on a one-core boot, and a deflated offered
 * rate is exactly the kind of measurement error this round exists to avoid.
 *
 * Accounting (all deltas over the window):
 *
 *   offered   = rx_frames + mmc_rx_fifo_overflow        (frames the MAC saw)
 *   delivered = lwip_stats.udp.recv                     (UDP input calls)
 *   lost_mac  = mmc_rx_fifo_overflow                    (receive FIFO overrun)
 *   lost_drv  = rx_dropped                              (driver staging)
 *   lost_brg  = pool_fail + input_fail                  (bridge)
 *   lost_stk  = udp.drop + ip.drop + link.drop + mem.err + pbuf.err
 *   closure   = |delivered + all losses - offered| / offered
 *
 * `callbacks` is a second source for `delivered` (the PCB callback), and
 * `udp_in` (MIB2 udpindatagrams) is a third. The strong verdict is closure:
 * a clean window has every frame accounted for at one of the layers.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cases.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/stats.h"
#include "lwip/memp.h"
#include "lwip/udp.h"

#include "log.h"

#include "dwc_eqos.h"
#include "eqos_types.h"
#include "eqos_rk3568.h"
#include "rtl8211f.h"
#include "ethnetif.h"

#include "generic_timer.h"
#include "net_prof.h"

#include "board_conf.h"
#include "cpu_freq.h"
#include "uart.h"

/* The bench's wired port: gmac1 (fe010000). gmac0 has no cable on this board. */
#define PPS_GMAC		1u
#define PPS_IP3			200u
#define PPS_GW3			1u
#define PPS_PORT		18768u

#define PPS_CMD_MAX		64u
#define PPS_TAG_MAX		32u
#define PPS_POLL_MS		5u
#define PPS_WINDOW_MAX_MS	15000u

/* ---------------------------------------------------------------------------
 * Board glue: the GMAC line and the lwIP stack. The sequence is the one net.c
 * and net_mdk_netio.c run; kept here rather than factored because those cases
 * are the reference for their own rounds and a shared helper would change
 * them under their evidence.
 * ------------------------------------------------------------------------- */

static struct netif g_netif;
static osSemaphoreId_t g_rx_sem;
static int g_line_up;

static void rx_notify(unsigned int index)
{
	(void)index;
	if (g_rx_sem != NULL)
		(void)osSemaphoreRelease(g_rx_sem);
}

static void rx_thread(void *arg)
{
	(void)arg;
	for (;;) {
		(void)osSemaphoreAcquire(g_rx_sem, osWaitForever);
		dwc_eqos_watchdog(PPS_GMAC);
		(void)ethnetif_poll(&g_netif);
	}
}

static void service_thread(void *arg)
{
	(void)arg;
	for (;;) {
		osDelay(100u);
		dwc_eqos_watchdog(PPS_GMAC);
	}
}

static int line_up(void)
{
	const struct dwc_eqos_plat *plat;
	eqos_link_info_t link;
	uint32_t configure;
	ip4_addr_t ip, mask, gw;
	uint8_t mac[6];

	if (g_line_up)
		return 0;

	plat = eqos_board_plat(PPS_GMAC);
	if (plat == NULL || dwc_eqos_initialize(PPS_GMAC, plat) != 0) {
		CASE_ANCHOR("pps: FAIL eqos init\n");
		return -1;
	}
#if defined(CONFIG_GMAC_RX_ZC) && CONFIG_GMAC_RX_ZC
	/* Arm zero copy before the port powers on: a frame that completes with
	 * no lend-out hook installed is refused and dropped by the driver. */
	ethnetif_zc_arm();
#endif
	if (dwc_eqos_power_control(PPS_GMAC, EQOS_POWER_FULL) != EQOS_OK) {
		CASE_ANCHOR("pps: FAIL gmac power\n");
		return -1;
	}
	if (rtl8211f_initialize(PPS_GMAC, (void *)(uintptr_t)PPS_GMAC,
				eqos_board_mdio_read,
				eqos_board_mdio_write) != EQOS_OK) {
		CASE_ANCHOR("pps: FAIL phy init\n");
		return -1;
	}
	if (rtl8211f_power_control(PPS_GMAC, EQOS_POWER_FULL) != EQOS_OK) {
		CASE_ANCHOR("pps: FAIL phy power\n");
		return -1;
	}
	(void)rtl8211f_set_interface(PPS_GMAC, EQOS_INTERFACE_RGMII);
	if (rtl8211f_set_mode(PPS_GMAC,
			      EQOS_PHY_AUTO_NEGOTIATE |
			      EQOS_PHY_SPEED_1G) != EQOS_OK) {
		CASE_ANCHOR("pps: FAIL phy autoneg\n");
		return -1;
	}
	{
		unsigned int waited = 0u;

		for (;;) {
			link = rtl8211f_get_link_info(PPS_GMAC);
			if (link.speed != 0u)
				break;
			if (waited >= 15000u) {
				CASE_ANCHOR("pps: FAIL link timeout\n");
				return -1;
			}
			(void)osDelay(100u);
			waited += 100u;
		}
	}
	/* EQOS_DUPLEX_FULL is 0 in this driver's mask encoding: only HALF sets a
	 * bit, so OR-ing the duplex value would program half duplex. */
	configure = link.speed | (link.duplex == EQOS_DUPLEX_HALF ?
				  EQOS_DUPLEX_Msk : 0u);
	(void)dwc_eqos_control(PPS_GMAC, DWC_EQOS_CONFIGURE, configure);
	(void)dwc_eqos_control(PPS_GMAC, DWC_EQOS_CONTROL_TX, 1);
	(void)dwc_eqos_control(PPS_GMAC, DWC_EQOS_CONTROL_RX, 1);

	CASE_ANCHOR("pps: link speed=%lu duplex=%lu\n",
		    (unsigned long)link.speed, (unsigned long)link.duplex);

	g_rx_sem = osSemaphoreNew(0xFFFFu, 0u, NULL);
	if (g_rx_sem == NULL) {
		CASE_ANCHOR("pps: FAIL rx sem\n");
		return -1;
	}
	dwc_eqos_set_rx_notify(rx_notify);

	if (osThreadNew(rx_thread, NULL,
			&(const osThreadAttr_t){ .name = "pps-rx",
						 .priority = osPriorityNormal,
						 .stack_size = 4096U }) == NULL ||
	    osThreadNew(service_thread, NULL,
			&(const osThreadAttr_t){ .name = "pps-svc",
						 .priority = osPriorityLow,
						 .stack_size = 2048U }) == NULL) {
		CASE_ANCHOR("pps: FAIL threads\n");
		return -1;
	}

	if (case_net_stack_up() != 0) {
		CASE_ANCHOR("pps: FAIL stack\n");
		return -1;
	}

	IP4_ADDR(&ip, 192, 168, 0, PPS_IP3);
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gw, 192, 168, 0, PPS_GW3);

	LOCK_TCPIP_CORE();
	ethnetif_add(&g_netif, PPS_GMAC, &ip, &mask, &gw);
	UNLOCK_TCPIP_CORE();

	(void)dwc_eqos_get_mac_address(PPS_GMAC, mac);
	CASE_ANCHOR("pps: addr 192.168.0.%u/24 mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		    PPS_IP3, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	g_line_up = 1;
	return 0;
}

/* ---------------------------------------------------------------------------
 * Receive path: one raw PCB, no socket, no queue, no copy. The callback's
 * only jobs are the counts and freeing the pbuf; anything else would price
 * the instrument into the number it measures.
 * ------------------------------------------------------------------------- */

static struct udp_pcb *g_pcb;
static volatile int g_in_window;
static volatile uint32_t g_callbacks;
static volatile uint32_t g_core_pkts[4];

static uint32_t read_mpidr(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
	return (uint32_t)v;
}

static void pps_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
		     const ip_addr_t *addr, u16_t port)
{
	uint32_t core;

	(void)arg;
	(void)pcb;
	(void)addr;
	(void)port;

	if (p == NULL)
		return;
	if (g_in_window) {
		core = (read_mpidr() >> BOARD_MPIDR_CORE_SHIFT) & 0x3u;
		g_callbacks++;
		g_core_pkts[core]++;
	}
	pbuf_free(p);
}

/* ---------------------------------------------------------------------------
 * Snapshots and the window ledger.
 * ------------------------------------------------------------------------- */

struct pps_snap {
	uint32_t rx_frames;
	uint32_t rx_dropped;
	uint32_t rx_ru;
	uint32_t rx_irqs;
	uint32_t ovf;
	uint32_t crc;
	unsigned long polls;
	unsigned long pool_fail;
	unsigned long input_fail;
	unsigned long fed;
	uint32_t udp_recv;
	uint32_t udp_drop;
	uint32_t ip_drop;
	uint32_t link_drop;
	uint32_t mem_err;
	uint32_t pbuf_err;
	uint32_t udp_in;
	uint32_t ip_in;
	uint32_t ip_in_discards;
	uint32_t callbacks;
	uint32_t cores[4];
	uint64_t cycles;
	uint64_t ticks;
};

static struct pps_snap g_a;

static void snap_take(struct pps_snap *s)
{
	struct dwc_eqos_stats ds;
	struct dwc_eqos_mmc_stats mmc;

	memset(s, 0, sizeof(*s));
	memset(&ds, 0, sizeof(ds));
	memset(&mmc, 0, sizeof(mmc));
	dwc_eqos_get_stats(PPS_GMAC, &ds);
	dwc_eqos_get_mmc_stats(PPS_GMAC, &mmc);
	ethnetif_get_diag(&s->polls, &s->pool_fail, &s->input_fail, &s->fed);

	s->rx_frames = ds.rx_frames;
	s->rx_dropped = ds.rx_dropped;
	s->rx_ru = ds.rx_buf_unavail;
	s->rx_irqs = ds.rx_irqs;
	s->ovf = mmc.rx_fifo_overflow;
	s->crc = mmc.rx_crc_error;

#if LWIP_STATS
	s->udp_recv = lwip_stats.udp.recv;
	s->udp_drop = lwip_stats.udp.drop;
	s->ip_drop = lwip_stats.ip.drop;
	s->link_drop = lwip_stats.link.drop;
	s->mem_err = lwip_stats.mem.err;
#endif
#if LWIP_STATS && MEMP_STATS
	if (lwip_stats.memp[MEMP_PBUF] != NULL)
		s->pbuf_err = lwip_stats.memp[MEMP_PBUF]->err;
#endif
#if MIB2_STATS
	s->udp_in = lwip_stats.mib2.udpindatagrams;
	s->ip_in = lwip_stats.mib2.ipindelivers;
	s->ip_in_discards = lwip_stats.mib2.ipindiscards;
#endif

	s->callbacks = g_callbacks;
	s->cores[0] = g_core_pkts[0];
	s->cores[1] = g_core_pkts[1];
	s->cores[2] = g_core_pkts[2];
	s->cores[3] = g_core_pkts[3];
	s->cycles = net_prof_cpu_cycles();
	s->ticks = gtimer_count_get();
}

static uint32_t mhz_x100_from(uint64_t cycles, uint64_t ticks)
{
	uint64_t hz;

	if (ticks == 0u)
		return 0u;
	hz = (cycles * (uint64_t)gtimer_freq_get()) / ticks;
	return (uint32_t)((hz * 100u) / 1000000u);
}

static uint32_t pps_ratio_ppm(uint32_t part, uint32_t whole)
{
	if (whole == 0u)
		return 0u;
	return (uint32_t)(((uint64_t)part * 1000000u) / whole);
}

static void win_report(const char *tag)
{
	struct pps_snap b;
	uint32_t frames, ovf, delivered, lost_drv, lost_brg, lost_stk;
	uint32_t offered, callbacks, udp_in_delta, ip_in_delta, ip_disc;
	uint32_t closure_ppm, loss_ppm, mhz;
	uint64_t cycles, ticks;

	snap_take(&b);

	frames = b.rx_frames - g_a.rx_frames;
	ovf = b.ovf - g_a.ovf;
	delivered = b.udp_recv - g_a.udp_recv;
	lost_drv = b.rx_dropped - g_a.rx_dropped;
	lost_brg = (uint32_t)((b.pool_fail - g_a.pool_fail) +
			      (b.input_fail - g_a.input_fail));
	lost_stk = (b.udp_drop - g_a.udp_drop) + (b.ip_drop - g_a.ip_drop) +
		   (b.link_drop - g_a.link_drop) + (b.mem_err - g_a.mem_err) +
		   (b.pbuf_err - g_a.pbuf_err);
	offered = frames + ovf;
	callbacks = b.callbacks - g_a.callbacks;
	udp_in_delta = b.udp_in - g_a.udp_in;
	ip_in_delta = b.ip_in - g_a.ip_in;
	ip_disc = b.ip_in_discards - g_a.ip_in_discards;
	cycles = b.cycles - g_a.cycles;
	ticks = b.ticks - g_a.ticks;
	mhz = mhz_x100_from(cycles, ticks);

	{
		uint32_t accounted = delivered + ovf + lost_drv + lost_brg + lost_stk;
		uint32_t diff = (accounted > offered) ? (accounted - offered) :
							(offered - accounted);

		closure_ppm = pps_ratio_ppm(diff, offered);
	}
	loss_ppm = (offered > delivered) ? pps_ratio_ppm(offered - delivered, offered) : 0u;

	CASE_ANCHOR("pps: win tag=%s offered=%lu delivered=%lu loss_ppm=%lu "
		    "closure_ppm=%lu frames=%lu\n",
		    tag, (unsigned long)offered, (unsigned long)delivered,
		    (unsigned long)loss_ppm, (unsigned long)closure_ppm,
		    (unsigned long)frames);
	CASE_ANCHOR("pps: win2 tag=%s ovf=%lu rx_dropped=%lu pool_fail=%lu "
		    "input_fail=%lu udp_drop=%lu ip_drop=%lu link_drop=%lu "
		    "mem_err=%lu pbuf_err=%lu\n",
		    tag, (unsigned long)ovf, (unsigned long)lost_drv,
		    (unsigned long)(b.pool_fail - g_a.pool_fail),
		    (unsigned long)(b.input_fail - g_a.input_fail),
		    (unsigned long)(b.udp_drop - g_a.udp_drop),
		    (unsigned long)(b.ip_drop - g_a.ip_drop),
		    (unsigned long)(b.link_drop - g_a.link_drop),
		    (unsigned long)(b.mem_err - g_a.mem_err),
		    (unsigned long)(b.pbuf_err - g_a.pbuf_err));
	CASE_ANCHOR("pps: win3 tag=%s udp_in=%lu ip_in=%lu ip_disc=%lu callbacks=%lu "
		    "cores=%lu,%lu,%lu,%lu cycles=%llu mhz_x100=%lu\n",
		    tag, (unsigned long)udp_in_delta, (unsigned long)ip_in_delta,
		    (unsigned long)ip_disc, (unsigned long)callbacks,
		    (unsigned long)(b.cores[0] - g_a.cores[0]),
		    (unsigned long)(b.cores[1] - g_a.cores[1]),
		    (unsigned long)(b.cores[2] - g_a.cores[2]),
		    (unsigned long)(b.cores[3] - g_a.cores[3]),
		    (unsigned long long)cycles, (unsigned long)mhz);
}

/* ---------------------------------------------------------------------------
 * CPU clock commands.
 * ------------------------------------------------------------------------- */

/* Busy-spin for `ms` milliseconds and report the effective CPU frequency in
 * hundredths of a MHz. WFI would under-report (the core stops), so this is a
 * real spin; 10 ms is short enough to run between cells and long enough that
 * the 24 MHz reference's quantisation is below 0.1%. */
static uint32_t spin_mhz_x100(uint32_t ms)
{
	uint64_t t0, t1, c0, c1;
	uint64_t want;

	t0 = gtimer_count_get();
	want = ((uint64_t)gtimer_freq_get() * ms) / 1000u;
	c0 = net_prof_cpu_cycles();
	while ((gtimer_count_get() - t0) < want)
		;
	t1 = gtimer_count_get();
	c1 = net_prof_cpu_cycles();
	return mhz_x100_from(c1 - c0, t1 - t0);
}

static void freq_report(uint32_t requested_hz, int ret)
{
	uint32_t actual = cpu_freq_get_hz();
	uint32_t mhz = spin_mhz_x100(10u);

	CASE_ANCHOR("pps: freq req_khz=%lu readback_khz=%lu mhz_x100=%lu ret=%d\n",
		    (unsigned long)(requested_hz / 1000u),
		    (unsigned long)(actual / 1000u),
		    (unsigned long)mhz, ret);
}

static bool parse_u32(const char *s, uint32_t *out)
{
	uint32_t v = 0u;

	if (s == NULL || *s == '\0')
		return false;
	while (*s >= '0' && *s <= '9') {
		v = v * 10u + (uint32_t)(*s - '0');
		s++;
		if (v > 100000000u)
			return false;
	}
	if (*s != '\0')
		return false;
	*out = v;
	return true;
}

static void cmd_freq(const char *arg)
{
	uint32_t khz;
	uint32_t hz;
	int ret;

	if (arg == NULL || *arg == '\0' || !parse_u32(arg, &khz)) {
		CASE_ANCHOR("pps: cmd err=freq-usage\n");
		return;
	}
	if (khz < 100000u || khz > 3000000u) {
		CASE_ANCHOR("pps: cmd err=freq-range\n");
		return;
	}
	hz = khz * 1000u;
	ret = cpu_freq_set_hz(hz);
	freq_report(hz, ret);
}

static void cmd_freqtest(void)
{
	const uint32_t *opps;
	unsigned int count = 0u;
	unsigned int i;
	unsigned int pass = 0u;
	uint32_t boot_hz = cpu_freq_get_hz();
	int first_ret = 0;

	opps = cpu_freq_opps(&count);
	for (i = 0u; i < count; i++) {
		uint32_t want_khz = opps[i] / 1000u;
		uint32_t want_mhz100 = (opps[i] / 1000000u) * 100u;
		uint32_t actual;
		uint32_t mhz;
		uint32_t diff_ppm;
		uint32_t mhz_delta;
		int ret = cpu_freq_set_hz(opps[i]);

		if (ret != 0 && first_ret == 0)
			first_ret = ret;
		actual = cpu_freq_get_hz();
		mhz = spin_mhz_x100(10u);
		/* The cycle-counter check is the one that sees the core: a
		 * firmware that answers 1992 MHz while the PLL still runs at 816
		 * passes a readback and fails this. 1% covers the reference's
		 * quantisation. */
		diff_ppm = pps_ratio_ppm(actual > opps[i] ? actual - opps[i] :
					 opps[i] - actual, opps[i]);
		mhz_delta = (mhz > want_mhz100) ? (mhz - want_mhz100) :
						  (want_mhz100 - mhz);
		if (ret == 0 && diff_ppm <= 10000u &&
		    mhz_delta * 100u <= want_mhz100)
			pass++;
		CASE_ANCHOR("pps: freqtest khz=%lu ret=%d readback_khz=%lu "
			    "mhz_x100=%lu\n",
			    (unsigned long)want_khz, ret,
			    (unsigned long)(actual / 1000u),
			    (unsigned long)mhz);
	}

	if (boot_hz != 0u)
		(void)cpu_freq_set_hz(boot_hz);
	{
		uint32_t mhz = spin_mhz_x100(10u);

		CASE_ANCHOR("pps: freqtest done pass=%u/%u restored_khz=%lu "
			    "mhz_x100=%lu first_ret=%d\n",
			    pass, count, (unsigned long)(boot_hz / 1000u),
			    (unsigned long)mhz, first_ret);
	}
}

/* ---------------------------------------------------------------------------
 * Command loop.
 * ------------------------------------------------------------------------- */

static char g_cmd[PPS_CMD_MAX];
static uint32_t g_cmd_len;
static char g_tag[PPS_TAG_MAX];
static int g_halt;
static uint64_t g_window_t0;

static void tag_copy(char *dst, const char *src)
{
	uint32_t i = 0u;

	while (src != NULL && src[i] != '\0' && i + 1u < PPS_TAG_MAX) {
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
}

static void cmd_go(const char *tag)
{
	if (g_in_window) {
		CASE_ANCHOR("pps: cmd err=already-in-window\n");
		return;
	}
	tag_copy(g_tag, (tag != NULL && *tag != '\0') ? tag : "cell");
	g_window_t0 = gtimer_count_get();
	g_callbacks = 0u;
	g_core_pkts[0] = 0u;
	g_core_pkts[1] = 0u;
	g_core_pkts[2] = 0u;
	g_core_pkts[3] = 0u;
	snap_take(&g_a);
	g_in_window = 1;
	CASE_ANCHOR("pps: go tag=%s\n", g_tag);
}

static void cmd_stop(const char *tag)
{
	if (!g_in_window) {
		CASE_ANCHOR("pps: cmd err=not-in-window\n");
		return;
	}
	g_in_window = 0;
	if (tag != NULL && *tag != '\0')
		tag_copy(g_tag, tag);
	win_report(g_tag);
}

static void cmd_halt(void)
{
	g_halt = 1;
	CASE_ANCHOR("pps: halt\n");
}

static void cmd_dispatch(char *line)
{
	char *arg;
	char *sp;

	while (*line == ' ' || *line == '\t')
		line++;
	sp = line;
	while (*sp != '\0' && *sp != ' ' && *sp != '\t')
		sp++;
	if (*sp != '\0') {
		*sp = '\0';
		arg = sp + 1;
		while (*arg == ' ' || *arg == '\t')
			arg++;
	} else {
		arg = NULL;
	}
	if (*line == '\0')
		return;
	if (strcmp(line, "go") == 0) {
		cmd_go(arg);
	} else if (strcmp(line, "stop") == 0) {
		cmd_stop(arg);
	} else if (strcmp(line, "freq") == 0) {
		cmd_freq(arg);
	} else if (strcmp(line, "freqtest") == 0) {
		cmd_freqtest();
	} else if (strcmp(line, "halt") == 0) {
		cmd_halt();
	} else {
		CASE_ANCHOR("pps: cmd err=unknown\n");
	}
}

static void cmd_poll(void)
{
	int ch;

	while (uart_early_try_getc(&ch)) {
		if (ch == '\r' || ch == '\n') {
			if (g_cmd_len != 0u) {
				g_cmd[g_cmd_len] = '\0';
				cmd_dispatch(g_cmd);
				g_cmd_len = 0u;
			}
			continue;
		}
		if (ch == 8 || ch == 127) {
			if (g_cmd_len != 0u)
				g_cmd_len--;
			continue;
		}
		if (g_cmd_len + 1u < sizeof(g_cmd))
			g_cmd[g_cmd_len++] = (char)ch;
	}
}

/* ---------------------------------------------------------------------------
 * Case entry.
 * ------------------------------------------------------------------------- */

int case_net_pps_run(void)
{
	CASE_ANCHOR("rtos: case net_pps start\n");

	if (line_up() != 0)
		return 1;

	/* CPU clock first: the boot rate is part of the evidence, and a firmware
	 * that refuses rate control must be reported before any traffic. */
	{
		int ret = cpu_freq_probe();
		uint32_t boot_hz = cpu_freq_get_hz();

		CASE_ANCHOR("pps: freq probe ret=%d supported=%u ver=0x%lx "
			    "attrs=0x%lx perm=0x%lx status=%lu boot_khz=%lu\n",
			    ret, cpu_freq_supported() ? 1u : 0u,
			    (unsigned long)cpu_freq_protocol_version(),
			    (unsigned long)cpu_freq_attributes(),
			    (unsigned long)cpu_freq_permissions(),
			    (unsigned long)cpu_freq_last_status(),
			    (unsigned long)(boot_hz / 1000u));
	}

	/* PMCCNTR probe: a self-checking enable, printed before anything can
	 * fault. The label is the case's own so a log reader does not have to
	 * know which case's helper emitted it. */
	if (net_prof_cpu_probe_init() == 0) {
		CASE_ANCHOR("pps: cpu probe ok=1 mhz_x100=%lu\n",
			    (unsigned long)spin_mhz_x100(1u));
	} else {
		CASE_ANCHOR("pps: cpu probe ok=0 (PMCCNTR not accessible)\n");
	}

	/* Raw PCB on the pps port: no socket, no receive queue, no copy. */
	LOCK_TCPIP_CORE();
	g_pcb = udp_new();
	if (g_pcb != NULL) {
		if (udp_bind(g_pcb, IP_ADDR_ANY, PPS_PORT) != ERR_OK) {
			udp_remove(g_pcb);
			g_pcb = NULL;
		} else {
			udp_recv(g_pcb, pps_recv, NULL);
		}
	}
	UNLOCK_TCPIP_CORE();
	if (g_pcb == NULL) {
		CASE_ANCHOR("pps: FAIL raw pcb\n");
		return 1;
	}

	CASE_ANCHOR("pps: ready addr=192.168.0.%u port=%u\n",
		    PPS_IP3, (unsigned)PPS_PORT);

	for (;;) {
		uint64_t max_ticks;

		cmd_poll();
		if (g_halt)
			break;
		/* A window that never gets its `stop` (serial lost) closes itself:
		 * a hung window would silently absorb the next cell's boot traffic.
		 * The sender's own `secs` still defines the rate, the board clock
		 * only bounds how long the ledger stays open. */
		max_ticks = ((uint64_t)gtimer_freq_get() *
			     PPS_WINDOW_MAX_MS) / 1000u;
		if (g_in_window &&
		    (gtimer_count_get() - g_window_t0) > max_ticks) {
			g_in_window = 0;
			win_report(g_tag);
		}
		osDelay(PPS_POLL_MS);
	}
	return 0;
}
