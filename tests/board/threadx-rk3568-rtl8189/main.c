/**
 * @file    main.c
 * @brief   ThreadX board RTL8189FTV image: base + shell + the SDIO bus line +
 *          the vendored Realtek SDIO vendor driver over it + the case table.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); the shape is the SDIO
 *          image's (tests/board/threadx-rk3568-sdio/main.c) with the wireless
 *          images' start-up wiring added (tests/board/threadx-rk3568-rtw89/
 *          main.c: the linuxkpi log sink and the core services).
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU; this
 * file brings the console up, wires the linuxkpi core, initialises the kernel
 * facade, creates the runner thread and starts the scheduler.
 *
 * The log sink is the first thing wired, and deliberately so: the linuxkpi core
 * hands every line its callers produce to a sink it was given, and with no sink
 * the whole driver world is silent - the driver's probe trace, which is the
 * evidence its case judges, would be lost with no sign that it was ever
 * produced.
 *
 * The SDIO backend is not installed here: the driver registers itself through
 * the module-init channel, and the first case brings the bus and the card up, so
 * a bring-up failure is a case verdict with its reason on the console rather
 * than a boot that stops before the judgment channel exists.
 */

#include <stdint.h>
#include <string.h>

#include "board_suite.h"
#include "os/cmsis2/cmsis2.h"

#include "arch_asm.h"
#include "log.h"
#include "uart.h"

#ifndef FW_BUILD_TAG
#define FW_BUILD_TAG "local"
#endif

void lkpi_log_sink_register(void (*sink)(ulog_level_t level, const char *line));
void lkpi_core_init(void);

/* the WPA world's wiring: the supplicant backend instance and the message
 * observer (both scalars-only faces) */
int lkpi_wpa_cfg80211_init(void);
void wlan8189_wpa_obs_init(void);

/*
 * The linuxkpi world's console outlet: every line the core and the driver
 * produce (its own `RTW: ` prints included) lands on the board log, stamped and
 * line-disciplined like the rest of the image.
 */
/*
 * The driver's per-frame diagnostics repeat for every frame it processes (its
 * receive walk's bounds report and the concurrent-mode clone compare). Each line
 * differs in the addresses and lengths it carries, so the log layer's
 * identical-line collapse does not fold them - a prefix gate is needed, and this
 * is it: the first few lines of a family are printed, the rest are dropped
 * silently. The level travels through unchanged, so the level filter still
 * decides what is seen at all.
 */
#define SINK_SIG_LEN	28
#define SINK_SIG_KEEP	2

static char wlan8189_sink_sig[SINK_SIG_LEN];
static unsigned int wlan8189_sink_repeats;

static void wlan8189_sink_collapse(ulog_level_t level, const char *line)
{
	char sig[SINK_SIG_LEN];
	size_t i = 0;

	while (line[i] != '\0' && i + 1 < sizeof(sig)) {
		sig[i] = line[i];
		i++;
	}
	sig[i] = '\0';

	if (strcmp(sig, wlan8189_sink_sig) == 0) {
		if (++wlan8189_sink_repeats > SINK_SIG_KEEP)
			return;	/* the same family again: dropped, not printed */
	} else {
		wlan8189_sink_repeats = 0;
		memcpy(wlan8189_sink_sig, sig, sizeof(sig));
	}
	log_emit(level, "lkpi: %s\n", line);
}

static void wlan8189_lkpi_sink(ulog_level_t level, const char *line)
{
	static char buf[200];
	size_t n = 0;

	if (line == NULL)
		return;
	while (line[n] != '\0' && n < sizeof(buf) - 1)
		n++;
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		n--;
	memcpy(buf, line, n);
	buf[n] = '\0';
	if (n != 0)
		/* The line is terminated here: the driver's own print format
		 * strings do not all end with a newline, and the console only
		 * splits a line at its length cap - an unterminated print glues
		 * onto whatever is written next, and the split then cuts a line
		 * (a judged anchor included) in half. */
		wlan8189_sink_collapse(level, buf);
}

static const osThreadAttr_t runner_thread_attr = {
	.name = "regress",
	.priority = osPriorityNormal,
	/* The driver's bring-up runs on this thread and is the deepest chain in the
	 * tree: the SDIO card service, the host glue and the controller each hold a
	 * frame, and the driver's own probe path - power sequence, firmware
	 * download, HAL init - runs on top of them. */
	.stack_size = 16384U,
};

/* Unrecoverable bring-up failure: report and park (a failure that cannot
 * speak is indistinguishable from a dead board). */
static void wlan8189_fatal( const char *what )
{
	uart_early_puts( "fatal: " );
	uart_early_puts( what );
	uart_early_puts( "\n" );
	uart_early_puts( "fatal: parked; power-cycle to recover\n" );

	ARCH_DAIF_SET( 0xf );
	for( ;; ) {
		ARCH_WFE();
	}
}

int main( void )
{
	uart_early_init();

	uart_early_puts( "threadx-rk3568-rtl8189 bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );

	if( osKernelInitialize() != osOK ) {
		wlan8189_fatal( "kernel init failed" );
	}

	/* the linuxkpi core services, and the console outlet its callers print
	 * through (without it the driver's whole world is silent) */
	lkpi_log_sink_register( wlan8189_lkpi_sink );
	lkpi_core_init();

	/*
	 * The supplicant chain (each step the same idempotent shape as the WLAN
	 * image's): the message observer latches the association anchors the join
	 * case and the judge read, and the backend instance registers the nine
	 * vendor operations against the substitute's seam - this line's driver is a
	 * FullMAC stack that talks cfg80211, so the backend is the cfg80211
	 * instance rather than the net80211 bridge's.
	 */
	wlan8189_wpa_obs_init();
	if( lkpi_wpa_cfg80211_init() != 0 ) {
		LOG_ERROR( "wlan8189: wpa backend registration failed" );
	} else {
		LOG_INFO( "wlan8189: wpa backend registered" );
	}

	if( osThreadNew( board_suite_run, 0, &runner_thread_attr ) == 0 ) {
		wlan8189_fatal( "runner thread create failed" );
	}

	if( osKernelStart() != osOK ) {
		wlan8189_fatal( "scheduler start failed" );
	}

	/* Not reached: the kernel does not return. */
	wlan8189_fatal( "scheduler returned" );
	return 0;
}
