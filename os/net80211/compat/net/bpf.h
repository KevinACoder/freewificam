/*
 * @file
 * @brief bpf(4) stub: radiotap taps are not attached on the ports.
 */

#ifndef _COMPAT_NET_BPF_H_
#define _COMPAT_NET_BPF_H_

#include <sys/cdefs.h>
#include <sys/types.h>

#define DLT_NULL 0
#define DLT_EN10MB 1
#define DLT_IEEE802_11 105
#define DLT_IEEE802_11_RADIO 127

struct bpf_if;
struct mbuf;

/* The stubs swallow the calls; the driver handle stays NULL. */
#define bpf_attach2(ifp, dlt, hdrlen, filterp) ((void) (filterp))
#define bpf_detach(ifp) ((void) (ifp))
#define bpf_mtap2(ifbp, data, dlen, m, ...) ((void) (m))
#define bpf_mtap(ifbp, m, ...) ((void) (m))
#define bpf_mtap3(ifbp, m, d) ((void) (m))
#define BPF_D_IN 0
#define BPF_D_OUT 1

#endif /* _COMPAT_NET_BPF_H_ */
