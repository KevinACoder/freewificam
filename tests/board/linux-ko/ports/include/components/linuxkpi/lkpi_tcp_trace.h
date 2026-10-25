/*
 * @file
 * @brief Empty shadow of the linuxkpi TCP-trace header for the Linux ko.
 *
 * wlan_bridge.c includes components/linuxkpi/lkpi_tcp_trace.h unconditionally;
 * the calls inside it are compiled only with CONFIG_WLAN_TCP_TRACE, which the
 * kernelmod line does not define.  The Linux ko compiles the net80211 world against
 * kernel headers, so the first-party linuxkpi header tree is replaced by this
 * empty face rather than pulled in.
 */

enum lkpi_tcp_tx_boundary {
	LKPI_TCP_TX_LINKOUT = 0,
	LKPI_TCP_TX_BRIDGE,
	LKPI_TCP_TX_ENCAP_IN,
	LKPI_TCP_TX_ENCAP_DONE,
	LKPI_TCP_TX_CRYPTO_DONE,
	LKPI_TCP_TX_SEAM,
	LKPI_TCP_TX_N,
};

#ifndef CONFIG_WLAN_TCP_TRACE
#define lkpi_tcp_trace_tx_mark(boundary)	((void)(boundary))
#define lkpi_tcp_trace_tx_frame(frame, len)	((void)(frame), (void)(len))
#define lkpi_tcp_trace_tx_submit(t_ns)		((void)(t_ns))
#define lkpi_tcp_trace_classify(frame, len)	((void)(frame), (void)(len))
#define lkpi_tcp_trace_frame_begin(a, b)	((void)(a), (void)(b))
#define lkpi_tcp_trace_frame_end()		((void)0)
#define lkpi_tcp_trace_reset()			((void)0)
#define lkpi_tcp_trace_print()			((void)0)
#endif

#ifndef _WLAN_LINUX_LKPI_TCP_TRACE_H_
#define _WLAN_LINUX_LKPI_TCP_TRACE_H_

#endif /* _WLAN_LINUX_LKPI_TCP_TRACE_H_ */
