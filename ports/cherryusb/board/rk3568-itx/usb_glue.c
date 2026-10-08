/*
 * @file   usb_glue.c
 * @brief  Board glue the imported USB host controller drivers call.
 *
 * Responsibilities, one per driver-facing contract:
 *
 *  - usb_board_init(): the board layer's domain bring-up (idempotent), the
 *    controller driver registration for the bus, and the interrupt binding.
 *    The stack dispatches through the registered driver table, so the driver
 *    has to be in place BEFORE usbh_initialize() - the hub thread that call
 *    creates runs the controller init through that table the moment it is
 *    scheduled.
 *
 *  - usb_hc_low_level_init()/usb_hc_low_level_deinit(): the hooks the
 *    controller drivers call from their own init/teardown, both required
 *    (not weak) at this revision. Init runs the per-controller part of the
 *    bring-up: for the USB3 instances the DWC3 core configuration, for the
 *    USB2 roots nothing beyond the shared PHY domain, which is already up.
 *    Deinit masks the bus's interrupt line; the domain stays powered because
 *    it is shared with the other controllers.
 *
 *  - usb_hc_low_level2_init(): overrides the weak default to publish the
 *    controller's real root port count, so the hub thread enumerates exactly
 *    the ports that exist.
 *
 *  - usbh_get_port_speed(): the USB2 root port speed decode the driver's
 *    roothub control path requires. The register window is read through the
 *    board layer - this file contains no register access of its own.
 *
 *  - usb_dcache_clean()/usb_dcache_invalidate()/usb_dcache_flush(): the
 *    stack's DMA-cache primitives, implemented on the arch cache API. The
 *    invalidate is a bare invalidate on purpose: it runs on buffers the
 *    device wrote and the CPU is about to read, and cleaning first would
 *    write the CPU's stale lines back over the device's data.
 *
 *  - the interrupt wrapper: the GIC handler passes the busid through the
 *    registered argument, the completion path is dispatched through the
 *    stack's per-bus entry, the whole dispatch is serialised against the
 *    submitting threads' critical sections, and ending the interrupt stays
 *    with the exception entry (no EOI work here).
 *
 * @author zhugengyu
 * @date   07.10.2026
 * @design adapter + callback registration - the imported stack defines the
 *         hook slots and this file fills them for this board: the low-level
 *         seam the controller drivers call out of their init/teardown
 *         (third-party/cherryusb/port/ehci/usb_hc_ehci.c:790-794 declares
 *         the hooks and calls them), the driver table the stack dispatches
 *         through by busid (third-party/cherryusb/core/usbh_core.c:1264
 *         registers, :1326 is the per-bus interrupt entry), and the board
 *         port's interrupt registration face (arch/aarch64/gicv3.h:21-38).
 *         The register bases and interrupt numbers are read from the board
 *         layer (board/rk3568-itx/usb/usb_domain.h), so no hardware fact is
 *         duplicated here.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "cache.h"
#include "board_conf.h"
#include "gicv3.h"
#include "log.h"
#include "minilibc.h"

#include "usb_domain.h"
#include "usb_dwc3.h"

#include "usbh_core.h"
/* The EHCI port's register header, for the root port status bits the speed
 * decode below needs; it also declares g_ehci_hcd. */
#include "usb_hc_ehci.h"
#include "usb_hc_xhci.h"

#include "usb_glue.h"

/* --- symbols owned outside this file ------------------------------------- */

/* The two controller driver tables. The USB2 table is a plain struct, the
 * USB3 one is const; both are defined in their own port with multi-controller
 * dispatch on. */
extern struct usbh_hc_driver ehci_hc_driver;
extern const struct usbh_hc_driver xhci_hc_driver;

/* The stack's per-bus interrupt entry: it dispatches to the registered
 * driver's handler for that busid. Declared here because the imported core
 * only publishes it in the multi-controller build. */
extern void USBH_IRQHandler(uint8_t busid);

/* --- console -------------------------------------------------------------- */

/* The stack's console sink (CONFIG_USB_PRINTF in usb_config.h): one line
 * formatted into a local buffer, then handed to the board log so the output
 * carries the image's stamp and line discipline. Reached from hub and class
 * threads and from error paths, never from an interrupt. */
void usbh_console_printf(const char *fmt, ...)
{
	static char line[192];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);

	board_log("%s", line);
}

/* The xHCI port prints its own bring-up anchors through the same sink; it
 * declares the symbol itself, so only the definition lives here. */

/* --- interrupts ----------------------------------------------------------- */

/* The completion path mutates the same shared state (async rings, pool
 * freelists, URB fields) that the submitting threads guard with the stack's
 * critical section, and a controller interrupt can preempt a submit in
 * progress - so the dispatch runs inside the same critical section. Nothing
 * in the held region sleeps. */
static void usb_board_isr(void *arg)
{
	size_t flags = usb_osal_enter_critical_section();

	USBH_IRQHandler((uint8_t)(uintptr_t)arg);

	usb_osal_leave_critical_section(flags);
}

/* Bind one bus's interrupt line: handler, level trigger, the board's default
 * device-interrupt priority, then enable. The line is enabled before the
 * controller runs, which is harmless - it stays deasserted until the
 * controller's interrupt enable bit is set during its own init. */
static void usb_board_irq_arm(unsigned busid)
{
	uint32_t intid = usb_board_bus_intid(busid);

	(void)irq_register(intid, usb_board_isr, (void *)(uintptr_t)busid);
	(void)irq_set_mode_level(intid);
	(void)irq_set_priority(intid, BOARD_SPI_DEFAULT_PRIORITY_RAW);
	(void)irq_enable(intid);
}

/* --- board entry points --------------------------------------------------- */

int usb_board_init(unsigned busid)
{
	if (busid >= USB_BOARD_BUS_COUNT) {
		return -1;
	}

	/* The whole domain, once, before any controller driver initialises:
	 * the shared soft-reset pulses inside it reset every controller in
	 * the USB block, so they must not land between two inits. The domain
	 * entry point is idempotent, so a per-bus bring-up order is safe. */
	usb_domain_up();

	if (usb_board_bus_is_xhci(busid)) {
		usbh_register_hc_driver(busid, &xhci_hc_driver);
	} else {
		usbh_register_hc_driver(busid, &ehci_hc_driver);
	}

	usb_board_irq_arm(busid);

	return 0;
}

uintptr_t usb_board_reg_base(unsigned busid)
{
	return usb_board_bus_reg_base(busid);
}

/* --- driver hooks --------------------------------------------------------- */

void usb_hc_low_level_init(struct usbh_bus *bus)
{
	if (usb_board_bus_is_xhci(bus->busid)) {
		/* The USB3 domain first (idempotent), then this core: close
		 * the soft reset, open the PHYs, force the host role. The core
		 * ID read back through the board layer is the anchor that says
		 * the aperture is alive - a core still in soft reset reads
		 * zero there. */
		usb_usb3_domain_init();

		usb_xhci_dwc3_host_init(bus->hcd.reg_base);

		board_log("usbh: bus%u gsnpsid=%08x\n", (unsigned)bus->busid,
			  usb_xhci_core_id(bus->hcd.reg_base));
		return;
	}

	/* USB2 root: the PHY domain is the only per-controller step; the
	 * rest of the root's bring-up is the generic EHCI reset the driver
	 * performs itself. */
	usb_usb2phy1_domain_init();
}

void usb_hc_low_level_deinit(struct usbh_bus *bus)
{
	if (bus->busid >= USB_BOARD_BUS_COUNT) {
		return;
	}

	/* Mask the line. The domain stays powered on purpose: it is shared
	 * with the other controllers and with the board's VBUS rails, so
	 * tearing it down here would take unrelated ports down with it. */
	(void)irq_disable(usb_board_bus_intid(bus->busid));
}

/* Weak in the imported driver, overridden here: the roothub is preset to the
 * configured maximum port count, and the hub thread enumerates exactly the
 * ports the controller reports. Publishing the real count keeps the thread
 * from probing ports that do not exist. */
void usb_hc_low_level2_init(struct usbh_bus *bus)
{
	bus->hcd.roothub.nports = g_ehci_hcd[bus->hcd.hcd_id].n_ports;
}

/* Root port speed decode, required by the driver's roothub control path
 * (the USB3 port decodes its own port register): a low-speed device holds
 * the port in the K state while it is being reset, and an enabled port with
 * neither indication is a high-speed device. */
uint8_t usbh_get_port_speed(struct usbh_bus *bus, const uint8_t port)
{
	uintptr_t opbase = (uintptr_t)bus->hcd.reg_base +
			   (uintptr_t)CONFIG_USB_EHCI_HCCR_OFFSET +
			   (uintptr_t)g_ehci_hcd[bus->hcd.hcd_id].hcor_offset;
	uint32_t regval = usb_ehci_port_status(opbase, (unsigned)(port - 1U));

	if ((regval & EHCI_PORTSC_LSTATUS_MASK) == EHCI_PORTSC_LSTATUS_KSTATE) {
		return USB_SPEED_LOW;
	}
	if ((regval & EHCI_PORTSC_PE) != 0U) {
		return USB_SPEED_HIGH;
	}
	return USB_SPEED_FULL;
}

/* --- DMA cache primitives ------------------------------------------------- */

void usb_dcache_clean(uintptr_t addr, size_t size)
{
	cache_dcache_flush(addr, (unsigned long)size);
}

void usb_dcache_invalidate(uintptr_t addr, size_t size)
{
	cache_dcache_invalidate(addr, (unsigned long)size);
}

void usb_dcache_flush(uintptr_t addr, size_t size)
{
	cache_dcache_flush_invalidate(addr, (unsigned long)size);
}