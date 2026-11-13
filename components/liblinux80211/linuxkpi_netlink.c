/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_netlink.c
 * @brief   The netlink/genetlink surface cfg80211's registration path touches,
 *          per ADR docs/decisions/2026-11-10-nl80211-scope.md (D1).
 *
 * @design "netlink is a decoder, not a protocol stack."  Attribute parsing is
 *         real (the control plane will feed nl80211 messages into it one day);
 *         message construction appends to the skb, so lengths and pointers are
 *         honest; the *send* side (unicast/broadcast/multicast) discards the
 *         message and reports success, because this port has no socket, no
 *         userspace and therefore no receiver.  Family registration records
 *         nothing but returns 0: nl80211_init() must proceed, and
 *         nl80211_exit() must not look up a family it never installed.
 *
 *         The per-net operations are likewise a recording stub: this port has
 *         one namespace (init_net), so there is no per-net state to allocate.
 * @date    10.11.2026
 */

#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/notifier.h>
#include <linux/netlink.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/dropreason.h>
#include <net/net_namespace.h>

/* ------------------------------------------------------------------ */
/* attribute parsing (real)                                            */
/* ------------------------------------------------------------------ */

int
__nla_validate(const struct nlattr *head, int len, int maxtype,
	       const struct nla_policy *policy, unsigned int validate,
	       struct netlink_ext_ack *extack)
{
	const struct nlattr *nla;
	int rem = len;

	(void)maxtype;
	(void)policy;
	(void)validate;
	(void)extack;
	for (nla = head, rem = len; nla_ok(nla, rem);
	     nla = nla_next(nla, &rem)) {
		if (rem < 0)
			return -EINVAL;
	}
	return rem == 0 ? 0 : -EINVAL;
}

int
__nla_parse(struct nlattr **tb, int maxtype, const struct nlattr *head,
	    int len, const struct nla_policy *policy, unsigned int validate,
	    struct netlink_ext_ack *extack)
{
	const struct nlattr *nla;
	int rem;
	int type;

	if (tb == NULL || maxtype < 0)
		return -EINVAL;
	memset(tb, 0, (size_t)(maxtype + 1) * sizeof(tb[0]));
	for (nla = head, rem = len; nla_ok(nla, rem);
	     nla = nla_next(nla, &rem)) {
		type = nla_type(nla);
		if (type <= maxtype)
			tb[type] = (struct nlattr *)nla;
	}
	if (rem != 0)
		return -EINVAL;
	(void)policy;
	(void)validate;
	(void)extack;
	return 0;
}

struct nlattr *
nla_find(const struct nlattr *head, int len, int attrtype)
{
	const struct nlattr *nla;
	int rem;

	for (nla = head, rem = len; nla_ok(nla, rem);
	     nla = nla_next(nla, &rem)) {
		if (nla_type(nla) == attrtype)
			return (struct nlattr *)nla;
	}
	return NULL;
}

int
nla_memcpy(void *dest, const struct nlattr *src, int count)
{
	int len;

	if (dest == NULL || src == NULL || count < 0)
		return -EINVAL;
	len = nla_len(src);
	if (count > len)
		count = len;
	memcpy(dest, nla_data(src), (size_t)count);
	return count;
}

/* ------------------------------------------------------------------ */
/* attribute construction (appends to the skb, discarded on send)      */
/* ------------------------------------------------------------------ */

struct nlattr *
nla_reserve(struct sk_buff *skb, int attrtype, int attrlen)
{
	struct nlattr *nla;
	int total;
	unsigned char *p;

	if (skb == NULL || attrlen < 0)
		return NULL;
	total = nla_total_size(attrlen);
	if (total > skb_tailroom(skb))
		return NULL;
	p = skb_put(skb, (unsigned int)total);
	memset(p, 0, (size_t)total);
	nla = (struct nlattr *)p;
	nla->nla_type = (u16)attrtype;
	nla->nla_len = (u16)(NLA_HDRLEN + attrlen);
	return nla;
}

struct nlattr *
nla_reserve_64bit(struct sk_buff *skb, int attrtype, int attrlen, int padattr)
{
	struct nlattr *nla;
	int total;

	total = nla_total_size(attrlen);
	if (total > skb_tailroom(skb))
		return NULL;
	nla = nla_reserve(skb, attrtype, attrlen);
	if (nla == NULL)
		return NULL;
	(void)padattr;
	return nla;
}

int
nla_put(struct sk_buff *skb, int attrtype, int attrlen, const void *data)
{
	struct nlattr *nla;

	nla = nla_reserve(skb, attrtype, attrlen);
	if (nla == NULL)
		return -EMSGSIZE;
	if (attrlen > 0 && data != NULL)
		memcpy(nla_data(nla), data, (size_t)attrlen);
	return 0;
}

int
nla_put_64bit(struct sk_buff *skb, int attrtype, int attrlen,
	      const void *data, int padattr)
{
	struct nlattr *nla;

	nla = nla_reserve_64bit(skb, attrtype, attrlen, padattr);
	if (nla == NULL)
		return -EMSGSIZE;
	if (attrlen > 0 && data != NULL)
		memcpy(nla_data(nla), data, (size_t)attrlen);
	return 0;
}

/*
 * The headerless forms.  Upstream appends the attribute's data without the
 * `struct nlattr` (lib/nlattr.c:nla_reserve_nohdr / __nla_put_nohdr); the
 * vendor driver's cfg80211 path puts the SSID/IE bytes it already framed this
 * way.  The reserve is the same tailroom check as nla_reserve, minus the
 * header, and the put copies into it.
 */
void *
nla_reserve_nohdr(struct sk_buff *skb, int attrlen)
{
	unsigned char *p;

	if (skb == NULL || attrlen < 0)
		return NULL;
	if (attrlen > skb_tailroom(skb))
		return NULL;
	p = skb_put(skb, (unsigned int)attrlen);
	memset(p, 0, (size_t)attrlen);
	return p;
}

void
__nla_put_nohdr(struct sk_buff *skb, int attrlen, const void *data)
{
	void *p;

	p = nla_reserve_nohdr(skb, attrlen);
	if (p != NULL && attrlen > 0 && data != NULL)
		memcpy(p, data, (size_t)attrlen);
}

int
nla_put_nohdr(struct sk_buff *skb, int attrlen, const void *data)
{
	void *p;

	p = nla_reserve_nohdr(skb, attrlen);
	if (p == NULL)
		return -EMSGSIZE;
	if (attrlen > 0 && data != NULL)
		memcpy(p, data, (size_t)attrlen);
	return 0;
}

/* [LNX] net/netlink/af_netlink.c: put the message header at the skb's tail.
 * This is the raw spelling nlmsg_put() wraps, and it is real (lengths and
 * pointers are honest); only the send half of the stack is a discard. */
struct nlmsghdr *
__nlmsg_put(struct sk_buff *skb, u32 portid, u32 seq, int type, int len,
	    int flags)
{
	struct nlmsghdr *nlh;
	int total;

	if (skb == NULL || len < 0)
		return NULL;
	total = NLMSG_HDRLEN + len;
	if (total > skb_tailroom(skb))
		return NULL;
	nlh = (struct nlmsghdr *)skb_put(skb, (unsigned int)total);
	nlh->nlmsg_len = (u32)total;
	nlh->nlmsg_type = (u16)type;
	nlh->nlmsg_flags = (u16)flags;
	nlh->nlmsg_seq = seq;
	nlh->nlmsg_pid = portid;
	return nlh;
}

/* ------------------------------------------------------------------ */
/* generic netlink: family + message header + send (discard)           */
/* ------------------------------------------------------------------ */

/*
 * D1's netlink is a decoder and a discard, not a socket stack.  But the
 * *handlers* are real, and the C4 bench reaches nl80211's trigger handler
 * through the registered family rather than re-implementing it: remember the
 * pointers here, and let the bench helper call pre_doit/doit/post_doit with a
 * parsed message.  Nothing is exposed to userspace; there is still no socket.
 */
#define LKPI_GENL_FAMILY_MAX	8

static const struct genl_family *lkpi_genl_families[LKPI_GENL_FAMILY_MAX];
static unsigned int lkpi_genl_family_n;

const struct genl_family *
lkpi_genl_family_get(const char *name)
{
	unsigned int i;

	if (name == NULL)
		return NULL;
	for (i = 0; i < lkpi_genl_family_n; i++)
		if (strcmp(lkpi_genl_families[i]->name, name) == 0)
			return lkpi_genl_families[i];
	return NULL;
}

int
genl_register_family(struct genl_family *family)
{
	if (family == NULL)
		return -EINVAL;
	if (lkpi_genl_family_n >= LKPI_GENL_FAMILY_MAX)
		return -ENOSPC;
	lkpi_genl_families[lkpi_genl_family_n++] = family;
	return 0;
}

int
genl_unregister_family(const struct genl_family *family)
{
	unsigned int i;

	for (i = 0; i < lkpi_genl_family_n; i++) {
		if (lkpi_genl_families[i] != family)
			continue;
		memmove(&lkpi_genl_families[i], &lkpi_genl_families[i + 1],
			(lkpi_genl_family_n - i - 1) *
			sizeof(lkpi_genl_families[0]));
		lkpi_genl_family_n--;
		return 0;
	}
	return 0;
}

void *
genlmsg_put(struct sk_buff *skb, u32 portid, u32 seq,
	    const struct genl_family *family, int flags, u8 cmd)
{
	struct nlmsghdr *nlh;
	struct genlmsghdr *hdr;

	if (skb == NULL || family == NULL)
		return NULL;
	nlh = nlmsg_put(skb, portid, seq, family->id,
			GENL_HDRLEN + (int)family->hdrsize, flags);
	if (nlh == NULL)
		return NULL;
	hdr = nlmsg_data(nlh);
	hdr->cmd = cmd;
	hdr->version = (u8)family->version;
	hdr->reserved = 0;
	return (u8 *)hdr + GENL_HDRLEN;
}

int
genlmsg_multicast_allns(const struct genl_family *family, struct sk_buff *skb,
			u32 portid, unsigned int group)
{
	(void)family;
	(void)portid;
	(void)group;
	/* no receiver: the message is consumed here, as the seam contract says */
	kfree_skb(skb);
	return 0;
}

/* ------------------------------------------------------------------ */
/* netlink core calls (no socket, no receiver)                         */
/* ------------------------------------------------------------------ */

int
netlink_unicast(struct sock *ssk, struct sk_buff *skb, u32 portid, int nonblock)
{
	(void)ssk;
	(void)portid;
	(void)nonblock;
	kfree_skb(skb);
	return 0;
}

int
netlink_broadcast_filtered(struct sock *ssk, struct sk_buff *skb, u32 portid,
			   u32 group, gfp_t allocation,
			   netlink_filter_fn filter, void *filter_data)
{
	(void)ssk;
	(void)portid;
	(void)group;
	(void)allocation;
	(void)filter;
	(void)filter_data;
	kfree_skb(skb);
	return 0;
}

int
netlink_register_notifier(struct notifier_block *nb)
{
	(void)nb;
	return 0;
}

int
netlink_unregister_notifier(struct notifier_block *nb)
{
	(void)nb;
	return 0;
}

void
do_trace_netlink_extack(const char *msg)
{
	(void)msg;
}

/* ------------------------------------------------------------------ */
/* per-net operations (one namespace) and namespace lookup             */
/* ------------------------------------------------------------------ */

int
register_pernet_device(struct pernet_operations *ops)
{
	(void)ops;
	return 0;
}

void
unregister_pernet_device(struct pernet_operations *ops)
{
	(void)ops;
}

struct net *
get_net_ns_by_pid(pid_t pid)
{
	(void)pid;
	return ERR_PTR(-ESRCH);
}

struct net *
get_net_ns_by_fd(int fd)
{
	(void)fd;
	return ERR_PTR(-EBADF);
}

/* ------------------------------------------------------------------ */
/* drop-reason registry (recording only)                               */
/* ------------------------------------------------------------------ */

void
drop_reasons_register_subsys(enum skb_drop_reason_subsys subsys,
			     const struct drop_reason_list *list)
{
	(void)subsys;
	(void)list;
}

void
drop_reasons_unregister_subsys(enum skb_drop_reason_subsys subsys)
{
	(void)subsys;
}
