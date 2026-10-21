/*
 * @file
 * @brief The stable port interface of the net_80211 library.
 *
 * A port adapts the imported NetBSD net80211 stack and chip drivers to
 * one operating environment. The library never includes an OS header
 * directly: the NetBSD kernel API surface the imported code expects is
 * declared by the shadow headers in compat/netbsd/ and implemented by
 * the port, while the pieces that differ structurally between OSes go
 * through this header.
 *
 * Ports live in three categories under port/:
 *   osal/<os>/    the OS adaptation (locks, threads, timers, firmware
 *                 storage) behind the compat/netbsd/ declarations,
 *   bus/<bus>/    one directory per bus backend (usb/ with its
 *                 port_usb.h types, pcie/ and sd/ reserved), bringing
 *                 an attached device to the chip driver,
 *   net/<stack>/  the presentation layer (how the wlan interface
 *                 appears to the host stack: embox netdev + cfg80211,
 *                 lwip netif, ...).
 *
 * This header is the bus-agnostic contract: firmware lookup,
 * presentation hooks, the chip driver registry and the port lifecycle.
 * Bus-specific types sit next to their backend (port/bus/usb/port_usb.h
 * for USB) and are only visible as opaque structs here.
 *
 * The set of compiled-in chip drivers is discovered through
 * WLAN_CHIP_DRIVERS (a weak NULL-terminated array), so adding a driver
 * never touches a port.
 */

#ifndef NET80211_PORT_H_
#define NET80211_PORT_H_

#include <stdint.h>
#include <stddef.h>

/* Bus types; backends include the full definitions from
 * port/bus/<bus>/port_<bus>.h. */
struct wlan_usb_dev;
struct wlan_usb_id;
struct wlan_pcie_dev;
struct wlan_pcie_id;

/* ------------------------------------------------------------------
 * Firmware
 *
 * Ports resolve a driver firmware name to bytes; embedding or loading
 * from a file system is up to the port. The blob is not freed.
 */

struct wlan_firmware {
	const uint8_t *data;
	size_t size;
};

int wlan_port_firmware_get(const char *name, struct wlan_firmware *fw);

/* ------------------------------------------------------------------
 * Net attachment (presentation hooks)
 *
 * net80211 runs on a BSD ifnet shell that the port owns. The port
 * decides how that shell maps onto its own stack.
 */

struct wlan_port_ifops {
	/* The interface went up/down (net80211 INIT <-> RUN transitions). */
	int (*up)(void *if_priv);
	int (*down)(void *if_priv);
};

/* ------------------------------------------------------------------
 * Frame delivery (presentation hooks)
 *
 * Data frames that pass the net80211 input path leave the library
 * through these hooks. The presentation layer registers handlers for
 * the frames it wants; without a handler frames are dropped as before.
 * Both run in the USB worker context: the handlers must copy the frame
 * and return without blocking.
 */

/* EAPOL (ethertype 0x888e) frames; buf points at the payload behind
 * the 14-byte ethernet header, src is the ethernet source address. */
typedef void (*wlan_eapol_rx_fn)(const uint8_t src[6],
    const uint8_t *buf, size_t len, void *arg);

/* Every other delivered frame, as a full ethernet frame. */
typedef void (*wlan_data_rx_fn)(const uint8_t *frame, size_t len,
    void *arg);

void wlan_port_set_eapol_rx(wlan_eapol_rx_fn fn, void *arg);
void wlan_port_set_data_rx(wlan_data_rx_fn fn, void *arg);

enum wlan_port_event {
	WLAN_PORT_SCAN_DONE,
	WLAN_PORT_ASSOC,
	WLAN_PORT_DISASSOC,
};

/* Borrowed address, valid only during the callback. Consumers copy and
 * queue notifications; they must not re-enter the protocol state machine. */
typedef void (*wlan_event_fn)(enum wlan_port_event event,
    const uint8_t *addr, void *arg);
void wlan_port_set_event_handler(wlan_event_fn fn, void *arg);

/* Start one complete scan on the device worker, with no automatic join. */
int wlan_port_scan(const uint8_t *ssid, size_t len);

/* Send a full ethernet frame out of the wlan interface (queued to the
 * ifnet, encrypted/encapsulated by net80211). Returns len or -1. */
int wlan_port_xmit(const uint8_t *frame, size_t len);

/* The interface hardware address (after attach). */
int wlan_port_get_hwaddr(uint8_t addr[6]);

/* Bring the first attached interface up (firmware load + power on).
 * Provided by the port core on top of the driver up hooks. */
int wlan_port_up(void);

/* Diagnostics: the attached driver's softc/node dump and the net80211
 * scan table. Provided by the port core. */
void wlan_port_status_dump(void);
void wlan_port_scan_dump(void);

/* Focus the shell hooks on a named driver adapter. */
int wlan_port_select(const char *name);

/* The active adapter's ieee80211com (NULL before attach). */
void *wlan_port_get_ic(void);

/* ------------------------------------------------------------------
 * Driver registry
 */

enum wlan_bus_type {
	WLAN_BUS_USB = 1,
	WLAN_BUS_PCIE = 2,
};

/* Control & diagnostics hooks a driver adapter offers the port shell.
 * Every entry may be NULL; the port core dispatches onto the first
 * adapter that registered. */
struct wlan_port_adapter {
	const char *name;
	int (*up)(void);
	int (*scan)(const uint8_t *ssid, size_t len);
	int (*xmit)(const uint8_t *frame, size_t len);
	int (*get_hwaddr)(uint8_t addr[6]);
	void (*status_dump)(void);
	void (*scan_dump)(void);
	/* the adapter's ieee80211com, for the supplicant bridge; set at
	 * attach time (the softc does not exist when the table is
	 * declared) */
	void *ic;
};

/* Called once by the driver adapter before/at attach time. */
void wlan_port_adapter_register(const struct wlan_port_adapter *adapter);

struct wlan_chip_driver {
	const char *name;
	enum wlan_bus_type bus;
	/* USB match table (WLAN_BUS_USB), terminated by vid==0 && pid==0. */
	const struct wlan_usb_id *usb_ids;
	/* PCI match table (WLAN_BUS_PCIE), terminated by vendor==0. */
	const struct wlan_pcie_id *pcie_ids;

	/* Attach the device: bring the chip up, load the firmware,
	 * ieee80211_ifattach. bus_dev is the port device of the driver's
	 * bus (struct wlan_usb_dev / struct wlan_pcie_dev); if_priv is the
	 * port-owned ifnet shell. Returns 0 on success; on failure the
	 * port closes the device. */
	int (*attach)(void *bus_dev, void *if_priv);
	void (*detach)(void *if_priv);
	void (*stop)(void *if_priv);
};

/* Each driver translation unit exports one global
 * `const struct wlan_chip_driver <name>_driver`. The port defines its
 * own NULL-terminated `wlan_chip_drivers[]` listing the drivers it was
 * built with - explicit, so ports and drivers stay decoupled without
 * relying on linker sections or constructors. */

/* ------------------------------------------------------------------
 * Port lifecycle
 *
 * Called once by the environment before any use. The port scans the
 * registry, claims matching devices and presents one net interface
 * per attached chip (naming per presentation layer).
 */

/* wlan_port_init()/wlan_port_deinit() are provided per port; the
 * embox port wires wlan_port_init as its unit init. */
void wlan_port_deinit(void);

/* ------------------------------------------------------------------
 * Driver serialization
 *
 * The imported drivers rely on the splnet() discipline of their host
 * kernel to serialize the interrupt, state-machine and transmit
 * contexts. The port provides a reentrant lock instead; it is taken
 * around every driver entry and around the interface transmit path.
 */
void wlan_port_serializer_lock(void);
void wlan_port_serializer_unlock(void);

#endif /* NET80211_PORT_H_ */
