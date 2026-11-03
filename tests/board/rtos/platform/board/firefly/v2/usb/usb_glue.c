/*
 * @file   usb_glue.c
 * @brief  Board glue the imported USB host controller drivers call, for the
 *         firefly v2 board (two xHCI instances, no domain bring-up).
 *
 * The responsibilities are the RK3568 board glue's, minus everything that
 * board's USB domain had to do (clock/PHY/soft-reset sequences, DWC3 core
 * configuration, EHCI port power): on this board the firmware leaves the
 * two xHCI instances running and the Linux tree drives them through the
 * generic xhci-plat driver, so what remains is
 *
 *  - usb_board_init(): the OSAL services (timer thread, semaphore pool),
 *    the xHCI driver registration for the bus and the GIC interrupt
 *    binding;
 *  - usb_hc_low_level_init(): the one board check the xHCI port cannot do
 *    itself - the aperture-alive anchor (a nonzero HCIVERSION), so a dead
 *    or unmapped controller fails at bring-up with a named line instead of
 *    a silent zero-filled capability space;
 *  - the DMA-cache primitives on the arch cache API;
 *  - the interrupt wrapper and the enumeration-event sink, including the
 *    disconnect hook that forgets a device's xHCI slot - the AIC8800
 *    ZeroCD flip (1111:1111 mass storage -> a69c:8d80 BootROM -> a69c:8d81
 *    runtime) re-enumerates three different devices on one port.
 *
 * @author zhugengyu
 * @date   03.11.2026
 * @design adapter + callback registration - the imported stack defines the
 *         hook slots and this file fills them for this board (the seam the
 *         controller drivers call out of their init/teardown,
 *         third-party/cherryusb/port/xhci/usb_hc_xhci.c:80 declares and
 *         :1195 calls; the driver table the stack dispatches through by
 *         busid, third-party/cherryusb/core/usbh_core.c:1264; and the
 *         board port's interrupt registration face,
 *         platform/arch/aarch64/gicv3.h:29). The register bases and
 *         interrupt numbers are read from the board layer
 *         (platform/board/firefly/v2/usb/usb_domain.h), so no hardware
 *         fact is duplicated here.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "cache.h"
#include "board_conf.h"
#include "gicv3.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

#include "usb_domain.h"

#include "usbh_core.h"
#include "usbh_hub.h"
#include "usb_hc_xhci.h"

#include "usb_glue.h"

/* The controller interrupt's raw GIC priority. The ThreadX backend's
 * API-call threshold is 0xb0 and the FreeRTOS one 0xd0; the board default
 * sits below both, so one value serves either kernel (the same note the
 * RK3568 glue carries). */
#ifndef USB_BOARD_IRQ_PRIORITY_RAW
#define USB_BOARD_IRQ_PRIORITY_RAW BOARD_SPI_DEFAULT_PRIORITY_RAW
#endif

/* --- symbols owned outside this file ------------------------------------- */

/* The xHCI driver table, defined in the imported port with
 * multi-controller dispatch on. */
extern const struct usbh_hc_driver xhci_hc_driver;

/* The stack's per-bus interrupt entry: it dispatches to the registered
 * driver's handler for that busid. */
extern void USBH_IRQHandler(uint8_t busid);

/* The xHCI port's slot cleanup for a port whose child left - declared by
 * the port, called from the disconnect event below. */
extern void usbh_xhci_forget_hport(uint8_t busid, uint8_t hub_index,
				   uint8_t hub_port);

/* --- console -------------------------------------------------------------- */

/* The stack's console sink: one line formatted locally, terminated, then
 * handed to the board log so every USB line carries the image's stamp. */
void usbh_console_printf(const char *fmt, ...)
{
	static char line[192];
	va_list ap;
	size_t n;

	va_start(ap, fmt);
	(void)vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);

	n = strlen(line);
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		line[--n] = '\0';
	LOG_INFO("%s\n", line);
}

/* --- interrupts ----------------------------------------------------------- */

/* One counter per bus: the diagnostic that separates "the transfer is
 * slow" from "the completion interrupt stopped arriving". */
static volatile unsigned int usb_board_irqs[USB_BOARD_BUS_COUNT];

unsigned int
usb_board_irq_count(unsigned int busid)
{
	return busid < USB_BOARD_BUS_COUNT ? usb_board_irqs[busid] : 0U;
}

static void usb_board_isr(void *arg)
{
	size_t flags;
	uint8_t busid = (uint8_t)(uintptr_t)arg;

	if (busid < USB_BOARD_BUS_COUNT)
		usb_board_irqs[busid]++;

	flags = usb_osal_enter_critical_section();
	USBH_IRQHandler(busid);
	usb_osal_leave_critical_section(flags);
}

static void usb_board_irq_arm(unsigned busid)
{
	uint32_t intid = usb_board_bus_intid(busid);

	(void)irq_register(intid, usb_board_isr, (void *)(uintptr_t)busid);
	(void)irq_set_mode_level(intid);
	(void)irq_set_priority(intid, USB_BOARD_IRQ_PRIORITY_RAW);
	(void)irq_enable(intid);
}

/* --- board entry points --------------------------------------------------- */

int usb_board_init(unsigned busid)
{
	if (busid >= USB_BOARD_BUS_COUNT) {
		return -1;
	}

	/* The stack's timers need their service thread before the first hub
	 * timer arms, and the semaphores the stack creates from a critical
	 * section are reserved while object creation is still allowed - the
	 * same order the RK3568 glue uses, and idempotent. */
	usb_osal_timer_service_start();
	usb_osal_sem_pool_init();

	usbh_register_hc_driver(busid, &xhci_hc_driver);

	usb_board_irq_arm(busid);

	return 0;
}

uintptr_t usb_board_reg_base(unsigned busid)
{
	return usb_board_bus_reg_base(busid);
}

void usb_board_after_start(unsigned busid)
{
	/* Nothing to raise on an xHCI root (no port-power registers); the
	 * hook exists so the case code keeps one shape across boards. */
	(void)busid;
}

/* --- driver hooks --------------------------------------------------------- */

void usb_hc_low_level_init(struct usbh_bus *bus)
{
	uint32_t cap = usb_xhci_cap_id(bus->hcd.reg_base);

	/* The aperture-alive anchor: the capability register's first dword
	 * carries HCIVERSION in its upper half; a clock-gated or unmapped
	 * window reads zero and every later step would silently operate on
	 * garbage. The RK3568 glue takes the same anchor from the DWC3 core
	 * ID. */
	LOG_DEBUG("usbh: bus%u hciversion=%04x caplength=%u\n",
		  (unsigned)bus->busid, (unsigned)(cap >> 16),
		  (unsigned)(cap & 0xffu));
	if ((cap >> 16) == 0U) {
		LOG_ERROR("usbh: bus%u capability space reads zero - the "
			"controller aperture is not live\n",
			(unsigned)bus->busid);
	}
}

void usb_hc_low_level_deinit(struct usbh_bus *bus)
{
	if (bus->busid >= USB_BOARD_BUS_COUNT) {
		return;
	}

	/* Mask the line; nothing else is torn down on purpose. */
	(void)irq_disable(usb_board_bus_intid(bus->busid));
}

/* The stack reports every enumeration milestone through this handler; the
 * lines are the run's anchors. On a disconnect the hub layer re-initializes
 * the child in place, so a device that left and came back (a modeswitch
 * flip) re-enumerates on the same hubport struct - and the controller's
 * slot for the old identity must go away now, or every transfer of the
 * next enumeration rings a doorbell into nothing and times out. */
static const char *usbh_event_name(uint8_t event)
{
	switch (event) {
	case USBH_EVENT_INIT:
		return "init";
	case USBH_EVENT_DEINIT:
		return "deinit";
	case USBH_EVENT_DEVICE_CONFIGURED:
		return "device-configured";
	case USBH_EVENT_DEVICE_DISCONNECTED:
		return "device-disconnected";
	case USBH_EVENT_INTERFACE_START:
		return "interface-start";
	case USBH_EVENT_INTERFACE_STOP:
		return "interface-stop";
	case USBH_EVENT_INTERFACE_UNSUPPORTED:
		return "interface-unsupported";
	default:
		return "other";
	}
}

void usb_board_event_handler(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
			     uint8_t intf, uint8_t event)
{
	LOG_DEBUG("usbh: bus%u event %s hub=%u port=%u intf=%u\n", busid,
		  usbh_event_name(event), hub_index, hub_port, intf);

	if (event == USBH_EVENT_DEVICE_DISCONNECTED) {
		usbh_xhci_forget_hport(busid, hub_index, hub_port);
	}
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
