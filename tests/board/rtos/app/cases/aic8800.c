/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   aic8800.c
 * @brief   Case aic8800: the AIC8800D80 USB dongle enumerates on a firefly
 *          v2 xHCI root, switches out of its ZeroCD mass-storage mode
 *          (1111:1111 -> a69c:8d80), downloads the five D80 U02 firmware
 *          images, comes back as the runtime composite (a69c:8d81), and the
 *          vendor FullMAC stack reaches READY, scans and (when the profile
 *          names an AP) joins with WPA2-PSK and DHCP.
 *
 * This is the CherryUSB-PR bring-up ladder - every stage is one of the
 * plan's B0..B4 milestones, and each has its own anchor so a failure says
 * which link broke:
 *
 *   B0  aic8800: usb bus=N started          (two xHCI roots registered)
 *   B1  the modeswitch line from usbh_msc   (1111:1111 CBW, no CSW)
 *   B2  the stack's own firmware lines      (five images, lengths checked)
 *   B3  aic8800: ready / scan N APs / rows
 *   B4  aic8800: connected / got ip / ping  (only when SSID is configured)
 *
 * The vendor class claims the devices itself (aic8800_class_info); the
 * case only orders the bring-up and polls the public wifi API. An empty
 * CONFIG_AIC8800_AP_SSID builds the bring-up-only image: the case stops
 * after the scan table and passes - joining is the B4 arm's to add.
 *
 * Anchors:
 *   aic8800: fw blobs registered (5 images, D80 U02)
 *   aic8800: usb buses=0,1
 *   aic8800: usb bus=0 hcd=xhci base=0x... started
 *   aic8800: FAIL <which link and why>
 *   aic8800: ready after N ms mac=..:..:..:..:..:..
 *   aic8800: scan N APs
 *   aic8800: scan[i] "ssid" ch=N rssi=-N
 *   aic8800: no AP configured - bring-up ladder ends here (B3)
 *   aic8800: connected "ssid" / got ip N.N.N.N
 *   aic8800: PASS
 */

#include <stdint.h>
#include <string.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "aic8800_fw.h"
#include "aic8800_usb.h"
#include "aic8800_wifi.h"
#include "usbh_aic8800.h"
#include "usb_glue.h"
#include "usb_domain.h"

/* The ladder's time budget, per stage. The modeswitch alone waits 350 ms
 * (the PR's own bring-up number), the firmware download moves ~410 KiB over
 * high-speed bulk, and each re-enumeration walks the hub thread once. */
#define AIC8800_READY_TIMEOUT_MS	90000u
#define AIC8800_SCAN_TIMEOUT_MS		20000u
#define AIC8800_JOIN_TIMEOUT_MS		30000u
#define AIC8800_POLL_MS			200u

/* The event callback runs in the FMAC worker; it only sets flags. */
static volatile unsigned int aic8800_events;
static volatile enum aic8800_wifi_state aic8800_last_state;

static void
aic8800_on_event(enum aic8800_wifi_event event, void *arg)
{
	(void)arg;
	aic8800_events |= 1u << (unsigned)event;
	aic8800_last_state = aic8800_wifi_get_state();
}

static int
aic8800_wait(unsigned int mask, unsigned int timeout_ms, unsigned int *waited)
{
	unsigned int elapsed = 0u;

	while (elapsed < timeout_ms) {
		if ((aic8800_events & mask) != 0u) {
			*waited = elapsed;
			return 0;
		}
		osDelay(AIC8800_POLL_MS);
		elapsed += AIC8800_POLL_MS;
	}
	*waited = elapsed;
	return -1;
}

static void
aic8800_dump_scan(void)
{
	unsigned int count = aic8800_wifi_get_scan_count();
	unsigned int rows = count;
	unsigned int i;

	if (rows > 8u) {
		rows = 8u;
	}
	CASE_ANCHOR("aic8800: scan %u APs\n", count);
	for (i = 0u; i < rows; i++) {
		struct aic8800_wifi_ap ap;

		if (aic8800_wifi_get_scan_ap(i, &ap) != 0) {
			break;
		}
		ap.ssid[sizeof(ap.ssid) - 1u] = '\0';
		/* freq_mhz is the vendor field as the scan report carries it
		 * (raw, printed unconverted): the demo's own unit for it does
		 * not divide out to a channel on this firmware's report
		 * format, and the join path keys on BSSID/RSN, not on it. */
		CASE_ANCHOR("aic8800: scan[%u] \"%s\" freq=%u band=%u "
		    "rssi=%d\n", i, ap.ssid, ap.freq_mhz,
		    (unsigned)ap.band, (int)ap.rssi);
	}
}

int
case_aic8800_run(void)
{
	enum usbh_aic8800_family family;
	const struct usbh_aic8800 *device;
	uint8_t mac[6];
	uint8_t ip[4], mask[4], gw[4];
	unsigned int waited = 0u;
	unsigned int bus;
	int started = 0;
	int result;

	/* B2 precondition: the five linked images with their lengths checked
	 * (the vendor loader refuses a wrong-length set before any USB
	 * transfer). */
	if (aic8800_fw_blobs_register() != 0) {
		CASE_ANCHOR("aic8800: FAIL firmware blob registration (wrong "
			    "lengths? regenerate with tools/rtos/aic8800-fw-blobs.py)\n");
		return 1;
	}
	CASE_ANCHOR("aic8800: fw blobs registered (5 images, D80 U02)\n");

	/* The vendor stack's own bring-up order: wifi layer + callback first,
	 * then the ZeroCD switch registered, then the controllers. */
	aic8800_wifi_init();
	if (AIC8800_WIFI_SSID[0] != '\0') {
		(void)aic8800_wifi_set_sta(AIC8800_WIFI_SSID,
		    AIC8800_WIFI_PASSWORD);
	}
	aic8800_wifi_set_event_callback(aic8800_on_event, NULL);
	usbh_aic8800_enable_zero_cd_modeswitch();

	/* B0: both xHCI roots, one usbh_initialize each. */
	CASE_ANCHOR("aic8800: usb buses=0,1\n");
	for (bus = 0u; bus < USB_BOARD_BUS_COUNT; bus++) {
		if (usb_board_init(bus) != 0) {
			CASE_INFO("aic8800: bus%u board layer failed\n", bus);
			continue;
		}
		if (usbh_initialize((uint8_t)bus, usb_board_reg_base(bus),
				    usb_board_event_handler) != 0) {
			CASE_INFO("aic8800: bus%u usbh_initialize failed\n",
			    bus);
			continue;
		}
		usb_board_after_start((uint8_t)bus);
		CASE_ANCHOR("aic8800: usb bus=%u hcd=xhci base=0x%lx started\n",
		    bus, (unsigned long)usb_board_reg_base(bus));
		started++;
	}
	if (started == 0) {
		CASE_ANCHOR("aic8800: FAIL no controller started\n");
		return 1;
	}

	/* B1+B2+B3's first half: the dongle walks ZeroCD -> BootROM ->
	 * firmware -> runtime -> READY. Everything between "started" and
	 * "ready" is the stack's own log (the modeswitch line, the firmware
	 * upload lines, the FMAC version line). */
	if (aic8800_wait(1u << AIC8800_WIFI_EVENT_READY,
			 AIC8800_READY_TIMEOUT_MS, &waited) != 0) {
		const struct usbh_aic8800 *device = usbh_aic8800_get_device();

		CASE_ANCHOR("aic8800: FAIL not ready after %u ms (stuck at "
			    "state=%d; see the modeswitch/firmware lines above)\n",
		    waited, (int)aic8800_wifi_get_state());
		if (device == NULL) {
			CASE_ANCHOR("aic8800: no device claimed by the vendor "
				    "class (enumeration stopped earlier)\n");
		}
		for (bus = 0u; bus < USB_BOARD_BUS_COUNT; bus++) {
			CASE_ANCHOR("aic8800: bus%u irqs=%u\n", bus,
			    usb_board_irq_count(bus));
		}
		return 1;
	}
	/* The class exposes the device, not a family getter: read the enum
	 * and name it here (the class's own name helper is file-static). */
	device = usbh_aic8800_get_device();
	family = (device != NULL) ? device->family
	    : USBH_AIC8800_FAMILY_UNKNOWN;
	(void)aic8800_wifi_get_mac(mac);
	CASE_ANCHOR("aic8800: ready after %u ms family=%u vid=%04x pid=%04x "
	    "mac=%02x:%02x:%02x:%02x:%02x:%02x\n", waited, (unsigned)family,
	    device ? device->vendor_id : 0u, device ? device->product_id : 0u,
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	for (bus = 0u; bus < USB_BOARD_BUS_COUNT; bus++) {
		CASE_ANCHOR("aic8800: bus%u irqs=%u\n", bus,
		    usb_board_irq_count(bus));
	}

	/* B3: the scan table. */
	result = aic8800_wifi_scan();
	if (result != 0) {
		CASE_ANCHOR("aic8800: FAIL scan refused (%d)\n", result);
		return 1;
	}
	if (aic8800_wait(1u << AIC8800_WIFI_EVENT_SCAN_DONE,
			 AIC8800_SCAN_TIMEOUT_MS, &waited) != 0) {
		CASE_ANCHOR("aic8800: FAIL scan timeout after %u ms\n", waited);
		/* W7-02: same wedge fingerprint as the join arm - ep0
		 * liveness, halt-cycle, then a message-pipe rescan. */
		if (device != NULL && device->hport != NULL) {
			struct usb_setup_packet probe;
			static struct usb_setup_packet probe_setup
			    __attribute__((aligned(64)));
			static uint8_t desc[64] __attribute__((aligned(64)));
			static const uint8_t eps[4] = {
			    0x01u, 0x81u, 0x02u, 0x82u
			};
			int pr;
			unsigned int e;

			memset(&probe, 0, sizeof(probe));
			probe.bmRequestType = 0x80u;
			probe.bRequest = 0x06u; /* GET_DESCRIPTOR */
			probe.wValue = 0x0100u;
			probe.wIndex = 0u;
			probe.wLength = 18u;
			probe_setup = probe;
			pr = usbh_control_transfer(device->hport,
			    &probe_setup, desc);
			CASE_ANCHOR("aic8800: post-wedge ep0 probe=%d "
				    "vid=%02x%02x\n", pr,
			    (pr >= 0) ? desc[9] : 0u,
			    (pr >= 0) ? desc[8] : 0u);
			for (e = 0u; e < 4u; e++) {
				static struct usb_setup_packet halt
				    __attribute__((aligned(64)));
				int hr;

				memset(&halt, 0, sizeof(halt));
				halt.bmRequestType = 0x02u; /* EP */
				halt.bRequest = 0x03u; /* SET_FEATURE */
				halt.wValue = 0u; /* ENDPOINT_HALT */
				halt.wIndex = eps[e];
				hr = usbh_control_transfer(
				    device->hport, &halt, NULL);
				halt.bRequest = 0x01u; /* CLEAR_FEATURE */
				hr = usbh_control_transfer(
				    device->hport, &halt, NULL);
				CASE_ANCHOR(
				    "aic8800: halt-cycle ep=%02x ret=%d\n",
				    eps[e], hr);
			}
			{
				int sr = aic8800_wifi_scan();
				int sd = -1;
				unsigned int sw = 0u;

				if (sr == 0) {
					sd = aic8800_wait(
					    1u << AIC8800_WIFI_EVENT_SCAN_DONE,
					    8000u, &sw);
				}
				CASE_ANCHOR("aic8800: post-wedge rescan=%d "
					    "scan_done=%d count=%u\n", sr, sd,
				    aic8800_wifi_get_scan_count());
			}
		}
		return 1;
	}
	aic8800_dump_scan();
	if (aic8800_wifi_get_scan_count() == 0u) {
		CASE_ANCHOR("aic8800: FAIL scan table is empty\n");
		return 1;
	}

	/* B4: join + DHCP + ping, only when the profile names an AP. */
	if (AIC8800_WIFI_SSID[0] == '\0') {
		CASE_ANCHOR("aic8800: no AP configured - bring-up ladder ends "
			    "here (B3)\n");
		CASE_ANCHOR("aic8800: PASS\n");
		return 0;
	}
	result = aic8800_wifi_connect();
	if (result != 0) {
		CASE_ANCHOR("aic8800: FAIL connect refused (%d)\n", result);
		return 1;
	}
	if (aic8800_wait(1u << AIC8800_WIFI_EVENT_CONNECTED,
			 AIC8800_JOIN_TIMEOUT_MS, &waited) != 0) {
		/* W7-02 diagnostic: is the device's USB stack still alive
		 * after the join stall? An ep0 GET_DESCRIPTOR answers
		 * whether only the bulk pipes wedged (app-level) or the
		 * whole device went silent. */
		if (device != NULL && device->hport != NULL) {
			struct usb_setup_packet probe;
			/* the xHCI port asserts 64-byte alignment on both */
			static struct usb_setup_packet probe_setup
			    __attribute__((aligned(64)));
			static uint8_t desc[64] __attribute__((aligned(64)));
			int pr;

			memset(&probe, 0, sizeof(probe));
			probe.bmRequestType = 0x80u;
			probe.bRequest = 0x06u; /* GET_DESCRIPTOR */
			probe.wValue = 0x0100u;
			probe.wIndex = 0u;
			probe.wLength = 18u;
			probe_setup = probe;
			pr = usbh_control_transfer(device->hport,
			    &probe_setup, desc);
			CASE_ANCHOR("aic8800: FAIL associate timeout after %u "
				    "ms (ep0 probe=%d vid=%02x%02x)\n", waited,
			    pr, (pr >= 0) ? desc[9] : 0u,
			    (pr >= 0) ? desc[8] : 0u);
			/* W7-02 diagnostic 2: USB-protocol endpoint
			 * recovery. The controller-side RESET_EP/SET_TR_DEQ
			 * route is refused (Context State Error); try the
			 * device-side halt cycle on all four bulk endpoints
			 * via the still-working ep0, then the rescan below
			 * reports whether any pipe came back. */
			{
				static const uint8_t eps[4] = {
				    0x01u, 0x81u, 0x02u, 0x82u
				};
				unsigned int e;

				for (e = 0u; e < 4u; e++) {
					static struct usb_setup_packet halt
					    __attribute__((aligned(64)));
					int hr;

					memset(&halt, 0, sizeof(halt));
					halt.bmRequestType = 0x02u; /* EP */
					halt.bRequest = 0x03u; /* SET_FEATURE */
					halt.wValue = 0u; /* ENDPOINT_HALT */
					halt.wIndex = eps[e];
					hr = usbh_control_transfer(
					    device->hport, &halt, NULL);
					halt.bRequest = 0x01u; /* CLEAR_FEATURE */
					hr = usbh_control_transfer(
					    device->hport, &halt, NULL);
					CASE_ANCHOR(
					    "aic8800: halt-cycle ep=%02x "
					    "ret=%d\n", eps[e], hr);
				}
			}
			/* W7-02 diagnostic 3: is the fw's application loop
			 * still servicing the MESSAGE pipe? A rescan has an
			 * unambiguous completion event (SCAN_DONE). */
			{
				int sr = aic8800_wifi_scan();
				int sd = -1;
				unsigned int sw = 0u;

				if (sr == 0) {
					sd = aic8800_wait(
					    1u << AIC8800_WIFI_EVENT_SCAN_DONE,
					    8000u, &sw);
				}
				CASE_ANCHOR("aic8800: post-wedge rescan=%d "
					    "scan_done=%d count=%u\n", sr, sd,
				    aic8800_wifi_get_scan_count());
			}
		} else {
			CASE_ANCHOR("aic8800: FAIL associate timeout after %u "
				    "ms (no hport for ep0 probe)\n", waited);
		}
		return 1;
	}
	CASE_ANCHOR("aic8800: connected \"%s\" after %u ms\n",
	    AIC8800_WIFI_SSID, waited);
	if (aic8800_wait(1u << AIC8800_WIFI_EVENT_GOT_IP,
			 AIC8800_JOIN_TIMEOUT_MS, &waited) != 0) {
		CASE_ANCHOR("aic8800: FAIL no IP after %u ms (DHCP)\n",
			    waited);
		return 1;
	}
	(void)aic8800_wifi_get_ip4(ip, mask, gw);
	CASE_ANCHOR("aic8800: got ip %u.%u.%u.%u/%u.%u.%u.%u gw %u.%u.%u.%u\n",
	    ip[0], ip[1], ip[2], ip[3], mask[0], mask[1], mask[2], mask[3],
	    gw[0], gw[1], gw[2], gw[3]);

	/* The vendor stack's own gateway ping (four ICMP echoes through the
	 * netif it just brought up) - its prints are the B4 evidence. */
	aic8800_lwip_start_gateway_ping();
	osDelay(8000u);

	CASE_ANCHOR("aic8800: PASS\n");
	return 0;
}
