/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    net/iw_handler.h
 * @brief   The wireless-extension handler side: the handler type a driver exports
 *          and the descriptor it hangs off the net device.
 *
 * @design declaration-only compatibility face, transcribed declaration for declaration
 *         from the interface's header (include/net/iw_handler.h:295-351): the request
 *         metadata record, the handler function type and the driver handler descriptor.
 *         The records the handlers exchange (union iwreq_data and its members) and the
 *         private-argument descriptor belong to the wireless-extension uapi surface and
 *         are already stated in this face's <linux/wireless.h>; this header includes that
 *         one instead of repeating them. The descriptor is transcribed with the
 *         interface's own CONFIG_WEXT_PRIV gate, so the private-handler members exist
 *         exactly when the interface's do - which is the state the compiled set is in
 *         (that configuration is not part of it), and nothing here invents a private
 *         surface the interface would not have.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_NET_IW_HANDLER_H_
#define _LINUXKPI_NET_IW_HANDLER_H_

#include "../../linuxkpi_core.h"

#include <linux/wireless.h>

/* [LNX] include/net/iw_handler.h:295-313: the request metadata and the handler type.
 * Handlers are declared as a plain function pointer so `&handler` and `handler` are
 * both accepted, which is how the interface's handler tables are written. */
struct iw_request_info {
	__u16		cmd;	/* Wireless Extension command */
	__u16		flags;	/* More to come ;-) */
};

struct net_device;

typedef int (*iw_handler)(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);

/* [LNX] include/net/iw_handler.h:322-351 */
struct iw_handler_def {
	const iw_handler	*standard;
	__u16			num_standard;
#ifdef CONFIG_WEXT_PRIV
	__u16			num_private;
	__u16			num_private_args;
	const iw_handler	*private;
	const struct iw_priv_args *private_args;
#endif
	struct iw_statistics	*(*get_wireless_stats)(struct net_device *dev);
};

/* [LNX] include/net/iw_handler.h:463-483: the stream builders a get-handler calls to
 * append one event to the reply buffer. Declared, not defined: the layout arithmetic
 * is the port's when it has a wireless-extension stream to build. */
char *iwe_stream_add_event(struct iw_request_info *info, char *stream,
			   char *ends, struct iw_event *iwe, int event_len);
char *iwe_stream_add_point(struct iw_request_info *info, char *stream,
			   char *ends, struct iw_event *iwe, char *extra);

#endif /* _LINUXKPI_NET_IW_HANDLER_H_ */
