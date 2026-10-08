/*
 * @file    wpa_vendor_port.c
 * @brief   The wpa_vendor_port registration face.
 *
 * @design factory + registry (DESIGN.md section 1, "backend registration"
 *         row): a one-time registration of a full vtable, read back by the
 *         skeleton and by the port layer's ensure_up routing. Deliberately
 *         its own translation unit so a backend can be swapped by link input
 *         without touching the skeleton.
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
