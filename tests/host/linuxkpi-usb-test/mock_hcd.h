/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * mock_hcd.h - the synthetic host controller and the scripted device model.
 *
 * The pair behind the engine's backend seam (components/linuxkpi/usb/
 * lkpi_usb_backend.h): the controller half moves transfers between the engine
 * and the device half; the device half answers the enumeration requests and
 * the bulk-only-transport commands from its own constant tables.  Nothing in
 * either half reads the engine's internals: the truth of every assertion
 * lives in the tables and in the captured request bytes.
 *
 * @design test double + scripted responder - ref linux:drivers/usb/gadget/udc/
 *         dummy_hcd.c:1429 (one transfer function moving bytes across a
 *         synthetic link) and :392 (link state advanced by injected edges,
 *         not by a register model);
 *         completion delivery from a worker context per
 *         tests/host/net80211-frame-test/vwifi.c (the scripted counterpart
 *         pattern of the sibling mock suite).
 */

#ifndef MOCK_HCD_H
#define MOCK_HCD_H

#include "lkpi_usb_backend.h"

/* link state: the minimal set the suite asserts on */
enum mock_link_state {
	MOCK_LINK_DETACHED = 0,
	MOCK_LINK_ATTACHED,	/* connected, not yet reset */
	MOCK_LINK_RESET,	/* reset seen, default address */
	MOCK_LINK_ADDRESSED,	/* SET_ADDRESS done */
	MOCK_LINK_CONFIGURED,	/* SET_CONFIGURATION done */
	MOCK_LINK_SUSPENDED,	/* observable only */
	MOCK_LINK_ERROR,
};

/* the scripted faults (one-shot) */
enum mock_fault {
	MOCK_FAULT_NONE = 0,
	MOCK_FAULT_STALL,	/* the next matching transfer stalls */
};

/* capture limits */
#define MOCK_SETUP_LOG	16
#define MOCK_CBW_LOG	4

struct mock_capture {
	/* ep0 request lines, in order */
	u8	setup[MOCK_SETUP_LOG][8];
	int	setup_ret[MOCK_SETUP_LOG];
	unsigned int setup_n;
	/* the last device-descriptor response (the enumeration read) */
	u8	desc_dev[18];
	unsigned int desc_dev_len;
	/* the last bulk-OUT payload (a CBW when a BOT command ran) */
	u8	cbw[MOCK_CBW_LOG][31];
	unsigned int cbw_n;
	/* the last CSW the device sent */
	u8	csw[13];
	u8	csw_valid;
	/* the device counter snapshot */
	unsigned int n_control;
	unsigned int n_bulk_in;
	unsigned int n_bulk_out;
};

struct mock_hcd;

/* the backend instance: one root port, two bulk endpoints, one ep0 */
struct mock_backend {
	struct mock_hcd		*hcd;
	struct lkpi_usb_be_hubport port;
	struct lkpi_usb_be_ep	ep_in;
	struct lkpi_usb_be_ep	ep_out;
	struct mock_capture	cap;
};

/* lifecycle: create starts the model's worker thread and registers nothing;
 * the harness registers the returned ops with the seam itself */
struct mock_backend *mock_backend_create(void);
const struct lkpi_usb_be_ops *mock_backend_ops(void);
void mock_backend_free(struct mock_backend *b);

/* link edges */
void mock_hcd_connect(struct mock_backend *b);
void mock_hcd_disconnect(struct mock_backend *b);
enum mock_link_state mock_hcd_link(const struct mock_backend *b);

/* one-shot fault arming */
void mock_hcd_fault_set(struct mock_backend *b, enum mock_fault f);
void mock_hcd_hold_set(struct mock_backend *b, int on);

/* scripted responder switches (the usbtest battery shapes) */
void mock_hcd_script(struct mock_backend *b, int key, int value);
#define MOCK_SCRIPT_STALL_GET_INTERFACE	1	/* GET_INTERFACE always stalls */
#define MOCK_SCRIPT_STALL_IFACE_DESC	2	/* GET_DESCRIPTOR(INTERFACE) */
#define MOCK_SCRIPT_STALL_EP_DESC	3	/* GET_DESCRIPTOR(ENDPOINT) */
#define MOCK_SCRIPT_SHORT_DEVICE	4	/* DEVICE read answers short */
#define MOCK_SCRIPT_NO_STRING		5	/* string index 0 has no data */
#define MOCK_SCRIPT_MAX_LUN_STALL	6	/* Get Max LUN stalls */
#define MOCK_SCRIPT_ANSWER_ALL		7	/* answer every request, however
						 * short the device's own data
						 * (the lying-device tamper that
						 * must turn the negative
						 * battery red) */

/* the captured transfer counters (the device's own tally) */
void mock_hcd_counters(const struct mock_backend *b,
    unsigned int *n_control, unsigned int *n_bulk_in, unsigned int *n_bulk_out);

/* the worker's own pump: run one pending transfer (used by the harness when
 * it needs the completion to happen while it is not blocked) */
void mock_backend_pump(struct mock_backend *b);

#endif /* MOCK_HCD_H */
