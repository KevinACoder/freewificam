/*
 * @file
 * @brief The wpa_vendor_port registration face ([S7a] FR-8).
 *
 * Deliberately its own translation unit: the port layer (supp_main's
 * ensure_up routing) links against it in every CONFIG_WPA_SUPPLICANT
 * image, while the ops skeleton (driver_vendor.c) is backend-specific
 * and swaps with CONFIG_WPA_BACKEND.
 */

#include "wpa_vendor_port.h"

static const struct wpa_vendor_port *wpa_vendor_ops;

int wpa_vendor_port_register(const struct wpa_vendor_port *ops) {

	if (ops == NULL) {
		return -1;
	}
	wpa_vendor_ops = ops;
	return 0;
}

const struct wpa_vendor_port *wpa_vendor_backend(void) {

	return wpa_vendor_ops;
}
