/*
 * @file   usbh_glue.c
 * @brief  RK3568 CherryUSB glue: the strong low-level hooks the HCD ports
 *         expect, plus the dcache plumbing and the console formatter.
 *
 * Ported from the prior repo's port/adapters/cherryusb/usbh_glue.c
 * (board-verified 2026-09-19 dual EHCI / 2026-09-29 dual xHCI) and adapted
 * to this repo's base layer (SPEC S5/cherryusb-xhci-port §3.4):
 *  - the console sink is board_log (the stamped line assembler, S1 §3.4.2);
 *  - the CMSIS irq_ctrl IRQ_SetHandler/Enable trio becomes this repo's
 *    gicv3 irq_register/irq_set_priority/irq_enable - the registration
 *    carries the busid through the handler argument, which is what the
 *    CMSIS no-arg handler shape needed four trampolines for;
 *  - the shared domain sequence lives in port/adapters/usb/usb_domain.c
 *    (one copy of the register protocol; the NetBSD backend linked the
 *    same file upstream - in this repo it serves future backends);
 *  - the interrupt dispatch still goes through upstream's multi-HC
 *    dispatcher USBH_IRQHandler(busid).
 *
 * The invalidate stays a bare ivac on purpose: it is only ever called on
 * buffers the CPU is about to read after the device wrote them, and a
 * clean-and-invalidate (civac) would write the CPU's stale dirty lines back
 * over the DMA data (S1 §3.3.4).
 *
 * EHCI half (usb_hc_low_level2_init / usbh_get_port_speed / the EHCI
 * branch below) compiles only under CONFIG_USBHOST_EHCI - the P5-a image
 * is xHCI-only (SPEC N4); the P5c round flips the key and these join.
 *
 * @date   01.10.2026
 * @author zhugengyu
 */

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

#include "log.h"
#include "minilibc.h"
#include "gicv3.h"
#include "board_conf.h"
#include "cache.h"

#include "usbh_core.h"
#include "usb_board.h"
#include "usb_domain.h"
#include "usb_dwc3.h"
#if CONFIG_USBHOST_EHCI
#include "usb_hc_ehci.h"
#endif

/* the console formatter the shadow usb_config.h points CONFIG_USB_PRINTF at:
 * one line buffer formatted with minilibc's vsnprintf, then handed to the
 * stamped line assembler (board_log). Message text ends in '\n'; the sink
 * owns the CRLF and the timestamp (print discipline, S1 §3.4.2). Reached
 * from the hub threads and from error paths - USB_DBG_LEVEL is ERROR only,
 * the per-frame chatter never gets here. */
void usbh_console_printf(const char *fmt, ...)
{
	static char buf[192];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	board_log("%s", buf);
}

#ifdef CONFIG_USBHOST_EHCI
static const uint32_t s_ehci_irq[USBH_EHCI_NUM] = {
	USBH_EHCI0_IRQ,
	USBH_EHCI1_IRQ,
};
#endif

static const uint32_t s_xhci_irq[USBH_XHCI_NUM] = {
	USBH_XHCI0_IRQ,
	USBH_XHCI1_IRQ,
};

/* The completion path mutates the same shared state (async ring, pool
 * freelists, urb fields) the submit path guards with the osal critical
 * section, and the submit side may run on a thread the IRQ preempts.
 * Serializing the dispatch closes that window (the prior repo measured
 * the tear as double-granted pool slots under full-rate bulk). Hold time
 * is the scan itself; nothing in the held region sleeps. */
static void s_hcd_isr(uint8_t busid)
{
	size_t flags = usb_osal_enter_critical_section();

	USBH_IRQHandler(busid);

	usb_osal_leave_critical_section(flags);
}

/* The gicv3 registration carries the busid in the handler argument - the
 * one-arg slot the prior repo's CMSIS irq_ctrl shape had to close over
 * with one trampoline per bus. */
static void usbh_bus_isr(void *arg)
{
	s_hcd_isr((uint8_t)(uintptr_t)arg);
}

/* Arm one xHCI line: register the handler, level mode, the board's default
 * SPI priority class (same policy as the console line), then enable. The
 * distributor init leaves every SPI level-triggered already; the explicit
 * set keeps the invariant local to the arming site. */
static void usbh_xhci_irq_arm(uint8_t busid)
{
	uint32_t irq_num = s_xhci_irq[USBH_XHCI_INST(busid)];

	(void)irq_register(irq_num, usbh_bus_isr, (void *)(uintptr_t)busid);
	(void)irq_set_mode_level(irq_num);
	(void)irq_set_priority(irq_num, BOARD_SPI_DEFAULT_PRIORITY_RAW);
	(void)irq_enable(irq_num);
}

/* Called from the port's usb_hc_init() with the registers still reset:
 * bring up the shared PHY domain and arm the interrupt line. The register
 * base itself was set by usbh_initialize() before this runs. The xHCI
 * busids run the USB3 socket-group domain + the DWC3 core reconfiguration
 * (both once-guarded - usbh_platform_start() has already run them in task
 * context, before any hub thread existed, because the con9/con14 SRST
 * pulses reset whole controllers). */
void usb_hc_low_level_init(struct usbh_bus *bus)
{
	if (USBH_BUS_IS_XHCI(bus->busid)) {
		if (USBH_XHCI_INST(bus->busid) >= USBH_XHCI_NUM) {
			return;
		}

		usb_usb3_domain_init();
		usb_xhci_dwc3_host_init(bus->hcd.reg_base);

		/* The Phase-A checkpoint anchor (SPEC S5 §4/P5-a 判据 4): the
		 * core is out of soft reset and its ID register reads - this
		 * part reads 0 when the CORESOFTRESET closing clear was
		 * skipped. (0x5533300a on this silicon.) */
		board_log("usbh: bus%u gsnpsid=%08x\n", (uint32_t)bus->busid,
			  *(volatile uint32_t *)((uintptr_t)bus->hcd.reg_base +
						 DWC3_GSNPSID));

		usbh_xhci_irq_arm(bus->busid);
		return;
	}

#ifdef CONFIG_USBHOST_EHCI
	if ((uint32_t)bus->busid < USBH_EHCI_NUM) {
		uint32_t irq_num = s_ehci_irq[bus->busid];

		usb_usb2phy1_domain_init();

		(void)irq_register(irq_num, usbh_bus_isr,
				   (void *)(uintptr_t)bus->busid);
		(void)irq_set_mode_level(irq_num);
		(void)irq_set_priority(irq_num, BOARD_SPI_DEFAULT_PRIORITY_RAW);
		(void)irq_enable(irq_num);
	}
#endif
}

#ifdef CONFIG_USBHOST_EHCI
/* Called after HCRESET, before the schedules start: publish the real root
 * port count (from HCSPARAMS) into the roothub, which usbh_hub_initialize
 * preset to CONFIG_USBHOST_MAX_RHPORTS. The hub thread enumerates exactly
 * the ports this reports. */
void usb_hc_low_level2_init(struct usbh_bus *bus)
{
	bus->hcd.roothub.nports = g_ehci_hcd[bus->hcd.hcd_id].n_ports;
}

/* Root port speed decode from PORTSC line state; required by the EHCI
 * roothub control path (the xHCI port decodes PORTSC itself). */
uint8_t usbh_get_port_speed(struct usbh_bus *bus, const uint8_t port)
{
	uint32_t regval = EHCI_HCOR->portsc[port - 1U];

	if ((regval & EHCI_PORTSC_LSTATUS_MASK) == EHCI_PORTSC_LSTATUS_KSTATE) {
		return USB_SPEED_LOW;
	}
	if (regval & EHCI_PORTSC_PE) {
		return USB_SPEED_HIGH;
	}
	return USB_SPEED_FULL;
}
#endif /* CONFIG_USBHOST_EHCI */

/* --- dcache hooks (compiled in by CONFIG_USB_DCACHE_ENABLE) ---------------- */

void usb_dcache_clean(uintptr_t addr, size_t size)
{
	cache_dcache_flush(addr, size);
}

void usb_dcache_invalidate(uintptr_t addr, size_t size)
{
	cache_dcache_invalidate(addr, size);
}

void usb_dcache_flush(uintptr_t addr, size_t size)
{
	cache_dcache_flush(addr, size);
}
