/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/netlink.h
 * @brief   The netlink control-message records and helpers the wireless stack
 *          compiles against; the transport itself is not carried.
 *
 * @design declaration-only compatibility face over include/uapi/linux/netlink.h.
 *         The interface's header is two things: the wire vocabulary (the
 *         uapi file, carried verbatim) and the kernel-side helpers -
 *         the skb control-block accessors, the extended-ACK record and its
 *         NL_SET_ERR_MSG* macros, the dump/callback records, and the
 *         kernel-socket entry points.  This port has no netlink socket and no
 *         receive side (docs/decisions/2026-11-10-nl80211-scope.md: the
 *         netlink layer is a decode-only consumer), so the kernel-socket
 *         entry points stay declarations - the bodies either never run (the
 *         multicast/reply paths are discarded by the component) or belong to
 *         S1's link.  Two inline bodies of the interface are deliberately not
 *         carried: netlink_skb_clone() reads sk_buff fields this port's
 *         buffer does not have (destructor/is_vmalloc_addr), and the
 *         kernel-socket constructors name THIS_MODULE; neither is reached by
 *         the compiled stack.  Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_NETLINK_H_
#define _LINUXKPI_LINUX_NETLINK_H_

#include "../../linuxkpi_core.h"

#include <linux/skbuff.h>
#include <uapi/linux/netlink.h>
#include <net/scm.h>

struct net;
struct sock;
struct module;
struct user_namespace;
struct nla_policy;

/* [LNX] include/linux/netlink.h:14-17 */
static __always_inline __unused struct nlmsghdr *
nlmsg_hdr(const struct sk_buff *skb)
{
	return (struct nlmsghdr *)skb->data;
}

/* [LNX] include/linux/netlink.h:19-26 */
enum netlink_skb_flags {
	NETLINK_SKB_DST		= 0x8,	/* Dst set in sendto or sendmsg */
};

/* [LNX] include/linux/netlink.h:28-41: the control block netlink puts in
 * skb->cb.  The credential record is the face's (net/scm.h); the descriptor is
 * carried by value so a receiver that reads it lays out the interface's
 * bytes. */
struct netlink_skb_parms {
	struct scm_creds	creds;
	__u32			portid;
	__u32			dst_group;
	__u32			flags;
	struct sock		*sk;
	bool			nsid_is_set;
	int			nsid;
};

#define	NETLINK_CB(skb)		(*(struct netlink_skb_parms *)(void *)((skb)->cb))
#define	NETLINK_CREDS(skb)	(&NETLINK_CB((skb)).creds)
#define	NETLINK_CTX_SIZE	48

/* [LNX] include/linux/netlink.h:42-51 */
void	netlink_table_grab(void);
void	netlink_table_ungrab(void);

#define	NL_CFG_F_NONROOT_RECV	(1 << 0)
#define	NL_CFG_F_NONROOT_SEND	(1 << 1)

/* [LNX] include/linux/netlink.h:54-73: the kernel-socket configuration record.
 * The constructor is not carried (it names THIS_MODULE and a module
 * reference the port does not have); the record is, because a caller that
 * declares one must lay out the interface's fields. */
struct netlink_kernel_cfg {
	unsigned int	groups;
	unsigned int	flags;
	void		(*input)(struct sk_buff *skb);
	int		(*bind)(struct net *net, int group);
	void		(*unbind)(struct net *net, int group);
	void		(*release)(struct sock *sk, unsigned long *groups);
};

/* [LNX] include/linux/netlink.h:80-98: the extended-ACK record and the
 * message setters.  The port has no tracepoint (do_trace_netlink_extack is a
 * no-op declaration) and no userspace receiver, so a setter writes the
 * record and nothing reads it - but the store is what keeps a caller's control
 * flow identical, and the macros are the interface's spelling. */
#define	NETLINK_MAX_COOKIE_LEN	8
#define	NETLINK_MAX_FMTMSG_LEN	80

struct netlink_ext_ack {
	const char		*_msg;
	const struct nlattr	*bad_attr;
	const struct nla_policy	*policy;
	const struct nlattr	*miss_nest;
	u16			miss_type;
	u8			cookie[NETLINK_MAX_COOKIE_LEN];
	u8			cookie_len;
	char			_msg_buf[NETLINK_MAX_FMTMSG_LEN];
};

void	do_trace_netlink_extack(const char *msg);

/* the interface's rate-limited diagnostic (include/linux/netdevice.h: net_warn_ratelimited);
 * this port routes it to the component's line sink. */
#define	net_warn_ratelimited(fmt, args...)				\
	lkpi_printf(ULOG_WARNING_LEVEL, fmt, ##args)

#define	NL_SET_ERR_MSG(extack, msg) do {				\
	static const char __msg[] = msg;				\
	struct netlink_ext_ack *__extack = (extack);			\
									\
	do_trace_netlink_extack(__msg);					\
	if (__extack)							\
		__extack->_msg = __msg;					\
} while (0)

#define	NL_SET_ERR_MSG_FMT(extack, fmt, args...) do {			\
	struct netlink_ext_ack *__extack = (extack);			\
									\
	if (!__extack)							\
		break;							\
	if (snprintf(__extack->_msg_buf, NETLINK_MAX_FMTMSG_LEN,	\
		     "%s" fmt "%s", "", ##args, "") >=			\
	    NETLINK_MAX_FMTMSG_LEN)					\
		net_warn_ratelimited("%s" fmt "%s", "truncated extack: ", \
				     ##args, "\n");			\
	do_trace_netlink_extack(__extack->_msg_buf);			\
	__extack->_msg = __extack->_msg_buf;				\
} while (0)

#define	NL_SET_ERR_MSG_MOD(extack, msg)					\
	NL_SET_ERR_MSG((extack), KBUILD_MODNAME ": " msg)
#define	NL_SET_ERR_MSG_FMT_MOD(extack, fmt, args...)			\
	NL_SET_ERR_MSG_FMT((extack), KBUILD_MODNAME ": " fmt, ##args)

#define	NL_SET_ERR_MSG_WEAK(extack, msg) do {				\
	if ((extack) && !(extack)->_msg)				\
		NL_SET_ERR_MSG((extack), msg);				\
} while (0)
#define	NL_SET_ERR_MSG_WEAK_MOD(extack, msg) do {			\
	if ((extack) && !(extack)->_msg)				\
		NL_SET_ERR_MSG_MOD((extack), msg);			\
} while (0)

#define	NL_SET_BAD_ATTR_POLICY(extack, attr, pol) do {			\
	if ((extack)) {							\
		(extack)->bad_attr = (attr);				\
		(extack)->policy = (pol);				\
	}								\
} while (0)
#define	NL_SET_BAD_ATTR(extack, attr)					\
	NL_SET_BAD_ATTR_POLICY(extack, attr, NULL)

#define	NL_SET_ERR_MSG_ATTR_POL(extack, attr, pol, msg) do {		\
	static const char __msg[] = msg;				\
	struct netlink_ext_ack *__extack = (extack);			\
									\
	do_trace_netlink_extack(__msg);					\
	if (__extack) {							\
		__extack->_msg = __msg;					\
		__extack->bad_attr = (attr);				\
		__extack->policy = (pol);				\
	}								\
} while (0)

#define	NL_SET_ERR_MSG_ATTR_POL_FMT(extack, attr, pol, fmt, args...) do { \
	struct netlink_ext_ack *__extack = (extack);			\
									\
	if (!__extack)							\
		break;							\
	if (snprintf(__extack->_msg_buf, NETLINK_MAX_FMTMSG_LEN,	\
		     "%s" fmt "%s", "", ##args, "") >=			\
	    NETLINK_MAX_FMTMSG_LEN)					\
		net_warn_ratelimited("%s" fmt "%s", "truncated extack: ", \
				     ##args, "\n");			\
	do_trace_netlink_extack(__extack->_msg_buf);			\
	__extack->_msg = __extack->_msg_buf;				\
	__extack->bad_attr = (attr);					\
	__extack->policy = (pol);					\
} while (0)

#define	NL_SET_ERR_MSG_ATTR(extack, attr, msg)				\
	NL_SET_ERR_MSG_ATTR_POL(extack, attr, NULL, msg)
#define	NL_SET_ERR_MSG_ATTR_FMT(extack, attr, msg, args...)		\
	NL_SET_ERR_MSG_ATTR_POL_FMT(extack, attr, NULL, msg, ##args)

#define	NL_SET_ERR_ATTR_MISS(extack, nest, type) do {			\
	struct netlink_ext_ack *__extack = (extack);			\
									\
	if (__extack) {							\
		__extack->miss_nest = (nest);				\
		__extack->miss_type = (type);				\
	}								\
} while (0)

#define	NL_REQ_ATTR_CHECK(extack, nest, tb, type) ({			\
	struct nlattr **__tb = (tb);					\
	u32 __attr = (type);						\
	int __retval;							\
									\
	__retval = !__tb[__attr];					\
	if (__retval)							\
		NL_SET_ERR_ATTR_MISS((extack), (nest), __attr);		\
	__retval;							\
})

static __always_inline __unused void
nl_set_extack_cookie_u64(struct netlink_ext_ack *extack, u64 cookie)
{
	if (!extack)
		return;
	BUILD_BUG_ON(sizeof(extack->cookie) < sizeof(cookie));
	memcpy(extack->cookie, &cookie, sizeof(cookie));
	extack->cookie_len = sizeof(cookie);
}

/* [LNX] include/linux/netlink.h:241-290: the kernel-side entry points.  The
 * port discards replies/multicasts at the component seam
 * (docs/HANDOFF-S0-HOST.md §2.3, decision A); these are declarations so the
 * cfg80211 sources keep their call shape, and S1 supplies the bodies the link
 * actually needs. */
void	netlink_kernel_release(struct sock *sk);
int	__netlink_change_ngroups(struct sock *sk, unsigned int groups);
int	netlink_change_ngroups(struct sock *sk, unsigned int groups);
void	__netlink_clear_multicast_users(struct sock *sk, unsigned int group);
void	netlink_ack(struct sk_buff *in_skb, struct nlmsghdr *nlh, int err,
		    const struct netlink_ext_ack *extack);
int	netlink_has_listeners(struct sock *sk, unsigned int group);
bool	netlink_strict_get_check(struct sk_buff *skb);
int	netlink_unicast(struct sock *ssk, struct sk_buff *skb, __u32 portid,
			int nonblock);
int	netlink_broadcast(struct sock *ssk, struct sk_buff *skb, __u32 portid,
			  __u32 group, gfp_t allocation);
typedef int (*netlink_filter_fn)(struct sock *dsk, struct sk_buff *skb,
				 void *data);
int	netlink_broadcast_filtered(struct sock *ssk, struct sk_buff *skb,
				   __u32 portid, __u32 group, gfp_t allocation,
				   netlink_filter_fn filter, void *filter_data);
int	netlink_set_err(struct sock *ssk, __u32 portid, __u32 group, int code);
int	netlink_register_notifier(struct notifier_block *nb);
int	netlink_unregister_notifier(struct notifier_block *nb);
struct sock *netlink_getsockbyfd(int fd);
int	netlink_attachskb(struct sock *sk, struct sk_buff *skb, long *timeo,
			  struct sock *ssk);
void	netlink_detachskb(struct sock *sk, struct sk_buff *skb);
int	netlink_sendskb(struct sock *sk, struct sk_buff *skb);

/* [LNX] include/linux/netlink.h:293-306: the message-size cap.  The interface
 * derives it from PAGE_SIZE and the shared-info tail; the port has no page
 * allocator in this path and its netlink layer is a decoder, so the cap is
 * stated as the one-page number the interface lands on for a 4 KiB page. */
#define	NLMSG_GOODSIZE		4096UL
#define	NLMSG_DEFAULT_SIZE	(NLMSG_GOODSIZE - NLMSG_HDRLEN)

/* [LNX] include/linux/netlink.h:309-334 */
struct netlink_callback {
	struct sk_buff		*skb;
	const struct nlmsghdr	*nlh;
	int			(*dump)(struct sk_buff *skb,
					struct netlink_callback *cb);
	int			(*done)(struct netlink_callback *cb);
	void			*data;
	struct module		*module;
	struct netlink_ext_ack	*extack;
	u16			family;
	u16			answer_flags;
	u32			min_dump_alloc;
	unsigned int		prev_seq, seq;
	int			flags;
	bool			strict_check;
	union {
		u8		ctx[NETLINK_CTX_SIZE];
		long		args[6];
	};
};

#define	NL_ASSERT_CTX_FITS(type_name)					\
	BUILD_BUG_ON(sizeof(type_name) > sizeof_field(struct netlink_callback, ctx))

struct netlink_notify {
	struct net	*net;
	u32		portid;
	int		protocol;
};

struct nlmsghdr *
__nlmsg_put(struct sk_buff *skb, u32 portid, u32 seq, int type, int len,
	    int flags);

struct netlink_dump_control {
	int	(*start)(struct netlink_callback *);
	int	(*dump)(struct sk_buff *skb, struct netlink_callback *);
	int	(*done)(struct netlink_callback *);
	struct netlink_ext_ack *extack;
	void	*data;
	struct module *module;
	u32	min_dump_alloc;
	int	flags;
};

int	__netlink_dump_start(struct sock *ssk, struct sk_buff *skb,
			     const struct nlmsghdr *nlh,
			     struct netlink_dump_control *control);

/* the interface fills in control->module; the port has no module reference */
static __always_inline __unused int
netlink_dump_start(struct sock *ssk, struct sk_buff *skb,
		   const struct nlmsghdr *nlh,
		   struct netlink_dump_control *control)
{
	return __netlink_dump_start(ssk, skb, nlh, control);
}

struct netlink_tap {
	struct net_device	*dev;
	struct module		*module;
	struct list_head	list;
};
int	netlink_add_tap(struct netlink_tap *nt);
int	netlink_remove_tap(struct netlink_tap *nt);

bool	__netlink_ns_capable(const struct netlink_skb_parms *nsp,
			     struct user_namespace *ns, int cap);
bool	netlink_ns_capable(const struct sk_buff *skb,
			   struct user_namespace *ns, int cap);
bool	netlink_capable(const struct sk_buff *skb, int cap);
bool	netlink_net_capable(const struct sk_buff *skb, int cap);
struct sk_buff *netlink_alloc_large_skb(unsigned int size, int broadcast);

#endif /* _LINUXKPI_LINUX_NETLINK_H_ */
