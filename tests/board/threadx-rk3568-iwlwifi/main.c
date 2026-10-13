/**
 * @file    main.c
 * @brief   ThreadX board iwlwifi (Intel AC 7260) image: base + shell + the PCIe
 *          bus line (ITS message interrupts, DesignWare root complex) + the
 *          linuxkpi PCI engine + the WLAN bring-up chain + the case table.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  facade consumer - the application talks to the kernel only through
 *          the CMSIS-RTOS2 facade (os/cmsis2/cmsis2.h); the shape is the PCIe
 *          image's (tests/board/threadx-rk3568-pcie/main.c) with the wireless
 *          images' start-up wiring added (tests/board/threadx-rk3568-rtw89/
 *          main.c: the linuxkpi log sink and the core services), and the USB
 *          backend replaced by the linuxkpi PCI engine's board adapter.
 *
 * Boot chain: start.S has already normalised to EL1 and enabled the MMU; this
 * file brings the console up, wires the linuxkpi core and the PCI backend,
 * walks the driver registry, registers the firmware blob, wires the bridge and
 * the supplicant chain, creates the runner thread and starts the scheduler.
 *
 * The log sink is the first thing wired, and deliberately so: the linuxkpi core
 * hands every line its callers produce (the driver's own dev_info/dev_err
 * prints included) to a sink it was given, and with no sink the whole driver
 * world is silent - the driver's probe trace, which is the evidence its case
 * judges, would be lost with no sign that it was ever produced.
 *
 * The PCI bus is not brought up here: the first case starts the engine, so a
 * bring-up failure is a case verdict with its reason on the console rather than
 * a boot that stops before the judgment channel exists.  The wiring order below
 * is what has to happen before the scheduler: the backend must be registered
 * before the engine starts, the driver registry must be walked before the
 * engine's probe pass can match the id table, and the firmware bytes must be
 * findable before the driver's probe asks for them.  All three are idempotent.
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
void lkpi_pcie_port_install(void);
void lkpi_module_init_run(void);
int lkpi_fw_iwl7260_fw_register(void);
int wlan_seam_adapter_init(void);

/* the WPA world's wiring: the supplicant backend instance and the message
 * observer (both scalars-only faces) */
int wlan_wpa_vendor_init(void);
void wlan_wpa_obs_init(void);
int wlan_radio_start(void);

/*
 * The linuxkpi world's console outlet: every line the core and the driver
 * produce (its own `iwlwifi ...` prints included) lands on the board log,
 * stamped and line-disciplined like the rest of the image.
 */
static void iwlwifi_lkpi_sink(ulog_level_t level, const char *line)
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
		log_emit(level, "lkpi: %s\n", buf);
}

static const osThreadAttr_t runner_thread_attr = {
	.name = "regress",
	.priority = osPriorityNormal,
	/* The driver's bring-up runs on this thread and is the deepest chain in
	 * the tree: the PCI engine's probe pass, the driver's own probe path
	 * (transport bring-up, firmware download, NVM read) and the mac80211
	 * start each hold a frame. */
	.stack_size = 16384U,
};

/* Unrecoverable bring-up failure: report and park (a failure that cannot
 * speak is indistinguishable from a dead board). */
static void iwlwifi_fatal( const char *what )
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

	uart_early_puts( "threadx-rk3568-iwlwifi bring-up " );
	uart_early_puts( FW_BUILD_TAG );
	uart_early_puts( "\n" );

	if( osKernelInitialize() != osOK ) {
		iwlwifi_fatal( "kernel init failed" );
	}

	/*
	 * The WLAN chain's start-up order (each step is a hard prerequisite of
	 * the next; the board log line after each names its result, so a boot
	 * that stops early says where):
	 *
	 *   1. the linuxkpi core services - the system workqueue the driver's
	 *      delayed work (rate adaptation, calibration) and the engine's
	 *      deferred paths ride on; without it that work is silently dropped
	 *      (lkpi_queue_delayed_work returns false on a NULL queue).
	 *   2. the linuxkpi PCI engine's board backend, before the engine
	 *      starts (the engine refuses with -NODEV without one).
	 *   3. the driver registry walk (.lkpi_init), which runs the vendored
	 *      driver's module_init and registers its PCI id table.
	 *   4. the firmware blob, before the driver's probe asks for it.
	 *   5. the bridge's upward hooks (the seam adapter).
	 */
	lkpi_log_sink_register(iwlwifi_lkpi_sink);
	lkpi_core_init();
	LOG_INFO( "iwlwifi: core services up\n" );
	lkpi_pcie_port_install();
	LOG_INFO( "iwlwifi: pcie backend registered\n" );
	lkpi_module_init_run();
	LOG_INFO( "iwlwifi: driver registry walked\n" );
	if( lkpi_fw_iwl7260_fw_register() != 0 ) {
		LOG_ERROR( "iwlwifi: firmware registration failed\n" );
	} else {
		LOG_INFO( "iwlwifi: firmware blob registered\n" );
	}
	wlan_seam_adapter_init();
	LOG_INFO( "iwlwifi: chain wired (backend + registry + firmware + bridge)" );

	/*
	 * The supplicant chain (each step the same idempotent shape as the other
	 * bridge images): the message observer latches the association anchors a
	 * later join case and the judge read, the backend instance registers the
	 * vendor operations against the bridge, and the radio's dwell pump takes
	 * over the channel walk for every sweep - the supplicant's own included.
	 */
	wlan_wpa_obs_init();
	if( wlan_wpa_vendor_init() != 0 ) {
		LOG_ERROR( "iwlwifi: wpa backend registration failed" );
	} else {
		LOG_INFO( "iwlwifi: wpa backend registered" );
	}
	if( wlan_radio_start() != 0 ) {
		LOG_ERROR( "iwlwifi: radio pump start failed" );
	}

	if( osThreadNew( board_suite_run, 0, &runner_thread_attr ) == 0 ) {
		iwlwifi_fatal( "runner thread create failed" );
	}

	if( osKernelStart() != osOK ) {
		iwlwifi_fatal( "scheduler start failed" );
	}

	/* Not reached: the kernel does not return. */
	iwlwifi_fatal( "scheduler returned" );
	return 0;
}
