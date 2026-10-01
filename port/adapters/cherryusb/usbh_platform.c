/*
 * @file   usbh_platform.c
 * @brief  CherryUSB backend platform bring-up: the USB3 socket group's xHCI
 *         roots now, the two panel EHCI roots behind CONFIG_USBHOST_EHCI.
 *
 * Ported from the prior repo (feat/cherryusb_xhci shape, board-verified
 * 2026-09-29) with three P5-a edits (SPEC S5/cherryusb-xhci-port §3.4):
 * the wlan bring-up segment (console gating, cmsis osal pre-init, rtw88
 * firmware registry) is deleted - this repo has no wlan line and the
 * console needs no readiness gating; the EHCI halves sit behind
 * CONFIG_USBHOST_EHCI (N4, the P5c flip); the xHCI start loop runs
 * CONFIG_USBHOST_XHCI_START_NUM instances from busid 2 (1 for P5-a's
 * single-instance bring-up, 2 restores the full serial pair for P5-b).
 *
 * The register side is not here: the PD_PIPE / PHY reference clock / VBUS
 * sequences live in port/adapters/usb/usb_domain.c, and the CRU/SRST
 * ordering discipline is fixed - every domain runs to completion before
 * any controller is initialized, because the con9/con14 SRST pulses reset
 * whole controllers (the USB3 socket group AND both EHCI roots).
 *
 * What this file owns is the CherryUSB half:
 *  - the OSAL byte pool + reaper (they must exist before the first
 *    usb_osal_* allocation, i.e. before the first hub thread is created);
 *  - one bus registration per root through upstream's multi-HC
 *    dispatcher (usbh_register_hc_driver) - registered BEFORE
 *    usbh_initialize(), because the hub thread that usbh_initialize()
 *    creates runs usb_hc_init() through that dispatcher the moment it is
 *    scheduled;
 *  - the wait for usb_hc_init() to actually finish (it runs on the hub
 *    thread, not on the caller);
 *  - KI-006: the USB3 dongle / the panel hubs sit plugged in before the
 *    image boots, so after reset the ports read CCS=1 without ever
 *    raising a connect-change edge and the hub thread would sleep forever
 *    waiting for one.  Ports with a device get a power-cycle kick to
 *    manufacture that edge, the roothub change bits are seeded, and the
 *    hub thread is woken (per-backend post_init).
 *
 * @date   01.10.2026
 * @author zhugengyu
 */

#include <stdbool.h>
#include <stdint.h>

#include "usbh_core.h"
#include "usbh_hub.h"
#ifdef CONFIG_USBHOST_EHCI
#include "usb_hc_ehci.h"
#endif
#if CONFIG_USBHOST_XHCI
#include "usb_hc_xhci.h"
#endif

#include "usb_board.h"
#include "usb_domain.h"
#include "usbh_platform.h"

#ifdef CONFIG_USBHOST_EHCI
/* upstream's per-controller driver table (defined in port/ehci/usb_hc_ehci.c
 * under CONFIG_USBHOST_MULT_HC); usbh_register_hc_driver() stores it in the
 * bus slot the dispatcher reads */
extern const struct usbh_hc_driver ehci_hc_driver;
#endif

/* How many xHCI instances this image starts, counting from busid 2
 * (USBH_XHCI0). 1 = P5-a's single-instance slice (0xFD000000 only);
 * 2 = the P5-b pair, started serially (busid 3 waits for busid 2 ready,
 * SPEC §3.2.2). Makefile-driven; usb_board.h stays the busid truth. */
#ifndef CONFIG_USBHOST_XHCI_START_NUM
#define CONFIG_USBHOST_XHCI_START_NUM	1U
#endif

static bool s_usbh_platform_started;
static bool s_usbh_platform_ready;

/* OSAL init is the adapter's side of the contract (upstream has no
 * prototype for it: the integrator owns the byte pool). */
extern void usb_osal_init(uint8_t *mem, uint32_t mem_size);

/* --- enumeration reporting ------------------------------------------------ */

static const char *usbh_speed_str(uint8_t speed)
{
	switch (speed) {
	case USB_SPEED_LOW:
		return "low";
	case USB_SPEED_FULL:
		return "full";
	case USB_SPEED_HIGH:
		return "high";
	case USB_SPEED_SUPER:
		return "super";
	default:
		return "?";
	}
}

/* Safe port lookup by (hub index, port): the vendored usbh_find_hubport()
 * asserts hub_index <= roothub->index, so it cannot address a device behind
 * the panel's external hub. Walk the tree ourselves and return NULL
 * instead of parking the system when the lookup has no answer. */
static struct usbh_hubport *usbh_find_port_safe(struct usbh_bus *bus,
						uint8_t hub_index,
						uint8_t hub_port)
{
	struct usbh_hub *stack[CONFIG_USBHOST_MAX_EXTHUBS + 1U];
	int top = 0;

	stack[top++] = &bus->hcd.roothub;

	while (top > 0) {
		struct usbh_hub *hub = stack[--top];
		uint8_t port;

		if (hub->index == hub_index) {
			if ((hub_port < 1U) || (hub_port > hub->nports)) {
				return NULL;
			}
			return hub->child[hub_port - 1U].connected ?
			       &hub->child[hub_port - 1U] : NULL;
		}

		for (port = 0U; port < hub->nports; port++) {
			if (hub->child[port].connected &&
			    (hub->child[port].self != NULL) &&
			    (top < (int)(sizeof(stack) / sizeof(stack[0])))) {
				stack[top++] = hub->child[port].self;
			}
		}
	}

	return NULL;
}

static void usbh_report_port(struct usbh_bus *bus, uint8_t hub_index,
			     uint8_t hub_port, const char *what)
{
	struct usbh_hubport *hport = usbh_find_port_safe(bus, hub_index,
							  hub_port);

	if (hport != NULL) {
		usbh_console_printf("usbh: bus%u hub%u port%u %s %04x:%04x %s "
				    "(%s)\n",
				    bus->busid, hub_index, hub_port, what,
				    hport->device_desc.idVendor,
				    hport->device_desc.idProduct,
				    hport->device_desc.bDeviceClass == 0x09U ?
				    "hub" : "device",
				    usbh_speed_str(hport->speed));
	} else {
		usbh_console_printf("usbh: bus%u hub%u port%u %s\n",
				    bus->busid, hub_index, hub_port, what);
	}
}

static void usbh_bus_event(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
			   uint8_t intf, uint8_t event)
{
	struct usbh_bus *bus = &g_usbhost_bus[busid];

	(void)intf;

	switch (event) {
	case USBH_EVENT_INIT:
		usbh_console_printf("usbh: bus%u hc init\n", busid);
		break;
	case USBH_EVENT_INTERFACE_START:
		usbh_report_port(bus, hub_index, hub_port, "claimed");
		break;
	case USBH_EVENT_INTERFACE_UNSUPPORTED:
		/* A device with no matching class driver: enumerated and
		 * reported, which is exactly what the enumeration milestone
		 * needs to see. */
		usbh_report_port(bus, hub_index, hub_port, "no-driver");
		break;
	case USBH_EVENT_DEVICE_CONNECTED:
		usbh_report_port(bus, hub_index, hub_port, "connected");
		break;
	case USBH_EVENT_DEVICE_DISCONNECTED:
		usbh_report_port(bus, hub_index, hub_port, "gone");
		/* The hub layer reuses the child struct in place, so the xHCI
		 * slot must be torn down NOW (before the next enumerate): a
		 * stale slot is unreachable-yet-matched and its HALTED ep0
		 * eats the re-enumeration (RTL8821CU modeswitch re-enum,
		 * fd000000 2026-09-29).  EHCI keeps no per-device hardware
		 * state, so this is a no-op there. */
#if CONFIG_USBHOST_XHCI
		if (USBH_BUS_IS_XHCI(busid)) {
			usbh_xhci_forget_hport(busid, hub_index, hub_port);
		}
#endif
		break;
	default:
		break;
	}
}

/* --- start path ------------------------------------------------------------ */

#ifdef CONFIG_USBHOST_EHCI
/* KI-006: manufacture the connect-change edge for pre-plugged root ports,
 * seed the roothub change bits, and wake the hub thread (which is blocked
 * on its queue with nothing pending right after usb_hc_init). */
static void usbh_ehci_post_init(struct usbh_bus *bus)
{
	struct ehci_hcd *hcd = &g_ehci_hcd[bus->hcd.hcd_id];
	uint8_t port;

	for (port = 1U; port <= hcd->n_ports; port++) {
		uint32_t regval = EHCI_HCOR->portsc[port - 1U];

		usbh_console_printf("usbh: bus%u port%u portsc=%08x ppc=%u\n",
				    bus->busid, port, regval, hcd->ppc);

		if (hcd->ppc && (regval & EHCI_PORTSC_CCS) != 0U) {
			regval &= ~EHCI_PORTSC_PP;
			EHCI_HCOR->portsc[port - 1U] = regval;
			usb_osal_msleep(30);
			regval |= EHCI_PORTSC_PP;
			EHCI_HCOR->portsc[port - 1U] = regval;
			usb_osal_msleep(30);
			usbh_console_printf(
				"usbh: bus%u port%u kicked, now %08x\n",
				bus->busid, port,
				EHCI_HCOR->portsc[port - 1U]);
		}

		bus->hcd.roothub.int_buffer[port / 8U] |=
			(uint8_t)(1U << (port % 8U));
	}

	usbh_console_printf("usbh: bus%u wake hub thread (intbuf=%02x%02x)\n",
			    bus->busid, bus->hcd.roothub.int_buffer[1],
			    bus->hcd.roothub.int_buffer[0]);
	usbh_hub_thread_wakeup(&bus->hcd.roothub);
}

/* One root: register the HCD, start the stack, wait for the controller to
 * come live, then run the pre-plugged-device seed. */
static int usbh_bus_start(uint8_t busid)
{
	struct usbh_bus *bus = &g_usbhost_bus[busid];
	struct ehci_hcd *hcd = &g_ehci_hcd[busid];
	uint32_t i;

	if ((uint32_t)busid >= USBH_EHCI_NUM) {
		return -1;
	}

	/* Register BEFORE usbh_initialize: the hub thread created inside
	 * calls usb_hc_init() through the dispatcher as soon as it runs, and
	 * usbh_register_hc_driver() only writes the static bus slot. */
	usbh_register_hc_driver(busid, &ehci_hc_driver);
	if (usbh_initialize(busid, USBH_EHCI_BASE(busid), usbh_bus_event) != 0) {
		return -1;
	}

	/* hcor_offset is read from the capability block mid-init; only then
	 * does an HCOR access (usbintr below) hit the operational frame. */
	for (i = 0U; i < 300U; i++) {
		if (hcd->hcor_offset != 0U) {
			break;
		}
		usb_osal_msleep(10);
	}
	for (i = 0U; i < 300U; i++) {
		if (EHCI_HCOR->usbintr != 0U) {
			break;
		}
		usb_osal_msleep(10);
	}
	if (EHCI_HCOR->usbintr == 0U) {
		usbh_console_printf("usbh: bus%u hc init timeout\n", busid);
		return -1;
	}

	usbh_ehci_post_init(bus);
	return 0;
}
#endif /* CONFIG_USBHOST_EHCI */

#if CONFIG_USBHOST_XHCI
/* The xHCI root (feat/cherryusb_xhci): same start shape, the readiness
 * probe is the driver's running flag, and the post-init pass is the
 * driver's (post-RUN port power + the KI-006 kick for the pre-plugged
 * dongle). */
static int usbh_xhci_bus_start(uint8_t busid)
{
	struct usbh_bus *bus = &g_usbhost_bus[busid];
	uint32_t i;

	if (!USBH_BUS_IS_XHCI(busid) ||
	    (USBH_XHCI_INST(busid) >= USBH_XHCI_NUM)) {
		return -1;
	}

	usbh_register_hc_driver(busid, &xhci_hc_driver);
	if (usbh_initialize(busid, USBH_XHCI_BASE(busid), usbh_bus_event) != 0) {
		return -1;
	}

	/* usb_hc_init runs on the hub thread this just created: wait for the
	 * controller to come live before the port seed. */
	for (i = 0U; i < 300U; i++) {
		if (usbh_xhci_bus_ready(busid)) {
			break;
		}
		usb_osal_msleep(10);
	}
	if (!usbh_xhci_bus_ready(busid)) {
		usbh_console_printf("usbh: bus%u xhci init timeout\n", busid);
		return -1;
	}

	usbh_xhci_post_init(bus);
	return 0;
}
#endif

int usbh_platform_start(void)
{
	uint8_t busid;
	int fails = 0;

	if (s_usbh_platform_started) {
		return s_usbh_platform_ready ? 0 : -1;
	}
	s_usbh_platform_started = true;

	usb_osal_init(NULL, 0U);

	/* Every shared-domain register write happens HERE, in task context,
	 * before any hub thread exists: the con9/con14 SRST pulses reset
	 * whole controllers (the USB3 socket group AND both EHCI roots), so
	 * they must not land between their inits.  The order is the
	 * NetBSD-side usb_platform_init order: USB3 socket group first,
	 * then the usb2phy1 domain. */
	usb_usb3_domain_init();
	usb_usb2phy1_domain_init();

#ifdef CONFIG_USBHOST_EHCI
	for (busid = 0U; busid < USBH_EHCI_NUM; busid++) {
		if (usbh_bus_start(busid) != 0) {
			usbh_console_printf("usbh: bus%u start FAIL\n", busid);
			fails++;
		}
	}
#endif

#if CONFIG_USBHOST_XHCI
	/* CONFIG_USBHOST_XHCI_START_NUM instances from busid 2, strictly
	 * serially (each start waits for its bus ready inside): P5-a runs
	 * 1 = 0xFD000000 (usbhost30, the lower socket's dedicated root);
	 * P5-b sets 2 and busid 3 (usbdrd30 @0xFCC00000, OTG forced host,
	 * the GL hub carrier) joins behind it, per the U-Boot `usb tree`
	 * of 2026-09-29.  The domain bring-up above already powers both
	 * usb2phy0 lanes (D-C8). */
	for (busid = USBH_XHCI0_BUSID;
	     busid < (USBH_XHCI0_BUSID + CONFIG_USBHOST_XHCI_START_NUM);
	     busid++) {
		if (usbh_xhci_bus_start(busid) != 0) {
			usbh_console_printf("usbh: bus%u xhci start FAIL\n",
					    (uint32_t)busid);
			fails++;
		}
	}
#endif

	s_usbh_platform_ready = (fails == 0);
	return s_usbh_platform_ready ? 0 : -1;
}

bool usbh_platform_ready(void)
{
	return s_usbh_platform_ready;
}
