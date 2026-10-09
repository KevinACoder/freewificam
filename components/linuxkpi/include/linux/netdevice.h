/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/netdevice.h
 * @brief   The network-device surface a driver compiles against: the device record
 *          and its operation table, the transmit queue and its flow-control helpers,
 *          the receive entry points and the NAPI (receive-polling) context.
 *
 * @design declaration-only compatibility face, grown per consumer. The interface's
 *         header (include/linux/netdevice.h) is one of the largest in the tree; this
 *         face carries the part the compiled set names, transcribed from the
 *         interface declaration for declaration and value for value, and marks every
 *         cut below. What is carried, in the interface's own grouping:
 *
 *           - the device record (`struct net_device`) with the members the compiled
 *             units read or write: the name and address block, the feature and flag
 *             words, the operation table pointer, the transmit-queue array, the
 *             wireless and receive-hook pointers, the statistics and the two structural
 *             members the interface's macros reach into (`dev`, for SET_NETDEV_DEV;
 *             `priv[]`, for netdev_priv). Members no compiled unit names (the remaining
 *             feature words, xdp, netns, sysfs/rtnl blocks, the per-queue receive array)
 *             are not carried: the record is not an ABI claim here, it is the shape its
 *             users name.
 *           - the operation table (`struct net_device_ops`) with the members the
 *             compiled units assign or read, at the interface's exact signatures.
 *           - the transmit-queue record (`struct netdev_queue`), the statistics record,
 *             the link/queue state enumerations and the device feature flags, all with
 *             the interface's values.
 *           - the device lifecycle, registration, notifier and receive entry points,
 *             declared as the interface declares them; their definitions belong to the
 *             port's net-device seam, which is where "register an interface", "hand a
 *             frame to the stack" and "the link came up" map onto the stack that
 *             actually exists on the image.
 *           - the NAPI context and its operations.
 *
 *         The inline bodies are the interface's own, with the one part that has no
 *         counterpart on this port (the queueing-discipline wake behind
 *         netif_tx_wake_queue) stated as such at its site. Nothing here is shared with a
 *         real Linux kernel at run time, so the record's layout is coherent for its
 *         users rather than fixed at the interface's offsets.
 *
 *         One include is carried here that the interface does not carry from this
 *         header: <linux/ethtool.h>. On the interface the driver's ethtool surface -
 *         the driver-information record a get_drvinfo callback fills and the operation
 *         table a net-device registration installs - reaches the driver through
 *         net/cfg80211.h, which includes that header (include/net/cfg80211.h:13); this
 *         face's net/cfg80211.h is grown separately and does not carry the include yet,
 *         and linux/ethtool.h is this face's own header, so the net-device face reaches
 *         it directly instead. One ethtool surface either way: the same header, spelled
 *         on a second path, not a second definition. Reference for the face
 *         shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_NETDEVICE_H_
#define _LINUXKPI_LINUX_NETDEVICE_H_

#include "../../linuxkpi_core.h"

#include <linux/ethtool.h>
#include <linux/if.h>
#include <linux/notifier.h>

struct net_device;
struct iw_handler_def;
struct wireless_dev;
struct ethtool_ops;

typedef u64 netdev_features_t;

/*
 * [LNX] include/linux/netdevice.h:130-138: the driver transmit return codes. The value
 * contract (0x00 took the packet, 0x10 busy) is the hardware-facing part; the
 * `__NETDEV_TX_MIN` member is the interface's own device for making the enumeration
 * signed, spelled as the twin of the core header's INT_MAX so this freestanding face
 * needs no host limits header. The value is the interface's INT_MIN.
 */
#define	NETDEV_TX_MASK	0xf0

enum netdev_tx {
	__NETDEV_TX_MIN	= (-INT_MAX - 1),	/* make sure enum is signed */
	NETDEV_TX_OK	= 0x00,			/* driver took care of packet */
	NETDEV_TX_BUSY	= 0x10,			/* driver tx path was busy */
};
typedef enum netdev_tx netdev_tx_t;

/* [LNX] include/linux/netdevice.h:96-97: the receive-path results the interface's
 * entry points report. */
#define	NET_RX_SUCCESS		0	/* keep 'em coming, baby */
#define	NET_RX_DROP		1	/* packet dropped */

/* [LNX] include/linux/netdevice.h:332-339 (enum netdev_state_t): the link state bits
 * behind netif_running() and netif_carrier_ok(). */
enum netdev_state_t {
	__LINK_STATE_START,
	__LINK_STATE_PRESENT,
	__LINK_STATE_NOCARRIER,
	__LINK_STATE_LINKWATCH_PENDING,
	__LINK_STATE_DORMANT,
	__LINK_STATE_TESTING,
};

/* [LNX] include/linux/netdevice.h:652-660 (enum netdev_queue_state_t): the per-queue
 * state bits behind the flow-control helpers below. */
enum netdev_queue_state_t {
	__QUEUE_STATE_DRV_XOFF,
	__QUEUE_STATE_STACK_XOFF,
	__QUEUE_STATE_FROZEN,
};

#define	QUEUE_STATE_DRV_XOFF	(1 << __QUEUE_STATE_DRV_XOFF)
#define	QUEUE_STATE_STACK_XOFF	(1 << __QUEUE_STATE_STACK_XOFF)
#define	QUEUE_STATE_FROZEN	(1 << __QUEUE_STATE_FROZEN)

/* [LNX] include/linux/netdevice.h:456-463: the generic-receive-offload result a
 * receive path reports for a buffer (the enumeration's order is the contract with the
 * callers that test it). */
enum gro_result {
	GRO_MERGED,
	GRO_MERGED_FREE,
	GRO_HELD,
	GRO_NORMAL,
	GRO_CONSUMED,
};
typedef enum gro_result gro_result_t;

/* [LNX] include/linux/netdevice.h:3670-3704 (enum netdev_priv_flags): the device's
 * private flags. The driver sets and tests IFF_DONT_BRIDGE; the rest of the interface's
 * enumeration is carried with it because the values are one contiguous flag space and a
 * partial copy of such a space is an invitation to a wrong bit later. */
enum netdev_priv_flags {
	IFF_802_1Q_VLAN			= 1 << 0,
	IFF_EBRIDGE			= 1 << 1,
	IFF_BONDING			= 1 << 2,
	IFF_ISATAP			= 1 << 3,
	IFF_WAN_HDLC			= 1 << 4,
	IFF_XMIT_DST_RELEASE		= 1 << 5,
	IFF_DONT_BRIDGE			= 1 << 6,
	IFF_DISABLE_NETPOLL		= 1 << 7,
	IFF_MACVLAN_PORT		= 1 << 8,
	IFF_BRIDGE_PORT			= 1 << 9,
	IFF_OVS_DATAPATH		= 1 << 10,
	IFF_TX_SKB_SHARING		= 1 << 11,
	IFF_UNICAST_FLT			= 1 << 12,
	IFF_TEAM_PORT			= 1 << 13,
	IFF_SUPP_NOFCS			= 1 << 14,
	IFF_LIVE_ADDR_CHANGE		= 1 << 15,
	IFF_MACVLAN			= 1 << 16,
	IFF_XMIT_DST_RELEASE_PERM	= 1 << 17,
	IFF_L3MDEV_MASTER		= 1 << 18,
	IFF_NO_QUEUE			= 1 << 19,
	IFF_OPENVSWITCH			= 1 << 20,
	IFF_L3MDEV_SLAVE		= 1 << 21,
	IFF_TEAM			= 1 << 22,
	IFF_PHONY_HEADROOM		= 1 << 24,
	IFF_MACSEC			= 1 << 25,
	IFF_NO_RX_HANDLER		= 1 << 26,
	IFF_FAILOVER			= 1 << 27,
	IFF_FAILOVER_SLAVE		= 1 << 28,
	IFF_L3MDEV_RX_HANDLER		= 1 << 29,
	IFF_NO_ADDRCONF			= BIT_ULL(30),
	IFF_TX_SKB_NO_LINEAR		= BIT_ULL(31),
};

/*
 * [LNX] include/linux/netdevice.h:3221-3261 (enum netdev_cmd): the device-event numbers
 * a registered notifier is called with. Carried in the interface's order so a notifier
 * that switches on the sentinel events it knows keeps the interface's numbering; the
 * enumeration continues past NETDEV_JOIN in the interface with the events of layers
 * this port does not have (bridging, tunnel offload), and they are not carried.
 */
enum netdev_cmd {
	NETDEV_UP	= 1,	/* For now you can't veto a device up/down */
	NETDEV_DOWN,
	NETDEV_REBOOT,		/* Tell a protocol stack a network interface
				 * detected a hardware crash and restarted
				 */
	NETDEV_CHANGE,		/* Notify device state change */
	NETDEV_REGISTER,
	NETDEV_UNREGISTER,
	NETDEV_CHANGEMTU,	/* notify after mtu change happened */
	NETDEV_CHANGEADDR,	/* notify after the address change */
	NETDEV_PRE_CHANGEADDR,	/* notify before the address change */
	NETDEV_GOING_DOWN,
	NETDEV_CHANGENAME,
	NETDEV_FEAT_CHANGE,
	NETDEV_BONDING_FAILOVER,
	NETDEV_PRE_UP,
	NETDEV_PRE_TYPE_CHANGE,
	NETDEV_POST_TYPE_CHANGE,
	NETDEV_POST_INIT,
	NETDEV_PRE_UNINIT,
	NETDEV_RELEASE,
	NETDEV_NOTIFY_PEERS,
	NETDEV_JOIN,
};

/*
 * [LNX] include/linux/netdevice.h:187-191: the interface declares each statistics field
 * as a union of a plain word and an atomic view so a reader on one CPU cannot tear a
 * writer on another. Only the plain word is carried: this port's atomic arithmetic has
 * no 64-bit word form in the contract header, and no compiled unit names the
 * `__`-prefixed member.
 */
#define	NET_DEV_STAT(FIELD)	unsigned long FIELD

/* [LNX] include/linux/netdevice.h:193-216 */
struct net_device_stats {
	NET_DEV_STAT(rx_packets);
	NET_DEV_STAT(tx_packets);
	NET_DEV_STAT(rx_bytes);
	NET_DEV_STAT(tx_bytes);
	NET_DEV_STAT(rx_errors);
	NET_DEV_STAT(tx_errors);
	NET_DEV_STAT(rx_dropped);
	NET_DEV_STAT(tx_dropped);
	NET_DEV_STAT(multicast);
	NET_DEV_STAT(collisions);
	NET_DEV_STAT(rx_length_errors);
	NET_DEV_STAT(rx_over_errors);
	NET_DEV_STAT(rx_crc_errors);
	NET_DEV_STAT(rx_frame_errors);
	NET_DEV_STAT(rx_fifo_errors);
	NET_DEV_STAT(rx_missed_errors);
	NET_DEV_STAT(tx_aborted_errors);
	NET_DEV_STAT(tx_carrier_errors);
	NET_DEV_STAT(tx_fifo_errors);
	NET_DEV_STAT(tx_heartbeat_errors);
	NET_DEV_STAT(tx_window_errors);
	NET_DEV_STAT(rx_compressed);
	NET_DEV_STAT(tx_compressed);
};

/*
 * [LNX] include/linux/netdevice.h:2710-2760 (struct netdev_queue): the per-queue
 * record the flow-control helpers below reach. The interface's record also carries the
 * queueing-discipline pair, the queue's sysfs kobject, the byte-queue-limit state, the
 * per-queue NAPI binding and the queue's transmit lock; none of those exist on this
 * port (there is no queueing discipline on the image) and no compiled unit names them,
 * so what is carried is the owning device pointer and the state word the helpers use.
 */
struct netdev_queue {
	struct net_device	*dev;
	unsigned long		state;
} __aligned(8);

/*
 * [LNX] include/linux/netdevice.h:344-433 (struct napi_struct): the receive-polling
 * context a driver embeds and hands to the stack's poller. The interface's record also
 * carries the generic-receive-offload node, the deferred- and threaded-instance
 * fields, the hash and device list nodes and the per-instance configuration; no
 * compiled unit names those, and who calls the poll callback (and when) is the port's
 * net-device seam rather than this record. What is carried is the state word, the
 * poll/weight pair and the owning device, which is what the interface's inline helpers
 * below touch. The weight is a plain field as in the interface.
 */
struct napi_struct {
	unsigned long		state;
	struct list_head	poll_list;
	int			weight;
	int			(*poll)(struct napi_struct *, int);
	struct net_device	*dev;
};

/* [LNX] include/linux/netdevice.h:2882 */
#define	NAPI_POLL_WEIGHT	64

/* [LNX] include/linux/netdevice.h:551-568: the NAPI state bit positions and their mask
 * forms. The interface declares the mask forms as an enumeration; they are carried here
 * with their exact values in the same two-part shape. */
enum {
	NAPI_STATE_SCHED,		/* Poll is scheduled */
	NAPI_STATE_MISSED,		/* reschedule a napi */
	NAPI_STATE_DISABLE,		/* Disable pending */
	NAPI_STATE_NPSVC,		/* Netpoll - don't dequeue from poll_list */
	NAPI_STATE_LISTED,		/* NAPI added to system lists */
	NAPI_STATE_NO_BUSY_POLL,	/* Do not add in napi_hash, no busy polling */
	NAPI_STATE_IN_BUSY_POLL,	/* Do not rearm NAPI interrupt */
	NAPI_STATE_PREFER_BUSY_POLL,	/* prefer busy-polling over softirq processing */
	NAPI_STATE_THREADED,		/* The poll is performed inside its own thread */
	NAPI_STATE_SCHED_THREADED,	/* Napi is currently scheduled in threaded mode */
	NAPI_STATE_HAS_NOTIFIER,	/* Napi has an IRQ notifier */
	NAPI_STATE_THREADED_BUSY_POLL,	/* The threaded NAPI poller will busy poll */
};

enum {
	NAPIF_STATE_SCHED		= BIT(NAPI_STATE_SCHED),
	NAPIF_STATE_MISSED		= BIT(NAPI_STATE_MISSED),
	NAPIF_STATE_DISABLE		= BIT(NAPI_STATE_DISABLE),
	NAPIF_STATE_NPSVC		= BIT(NAPI_STATE_NPSVC),
	NAPIF_STATE_LISTED		= BIT(NAPI_STATE_LISTED),
	NAPIF_STATE_NO_BUSY_POLL	= BIT(NAPI_STATE_NO_BUSY_POLL),
	NAPIF_STATE_IN_BUSY_POLL	= BIT(NAPI_STATE_IN_BUSY_POLL),
	NAPIF_STATE_PREFER_BUSY_POLL	= BIT(NAPI_STATE_PREFER_BUSY_POLL),
	NAPIF_STATE_THREADED		= BIT(NAPI_STATE_THREADED),
	NAPIF_STATE_SCHED_THREADED	= BIT(NAPI_STATE_SCHED_THREADED),
	NAPIF_STATE_HAS_NOTIFIER	= BIT(NAPI_STATE_HAS_NOTIFIER),
	NAPIF_STATE_THREADED_BUSY_POLL	= BIT(NAPI_STATE_THREADED_BUSY_POLL),
};

/* [LNX] include/linux/netdevice.h:1838-1867 (struct net_device_ops): the callbacks a
 * driver installs. The interface's table has some sixty members; the ones carried here
 * are the ones a compiled unit assigns or reads by name, at the interface's exact
 * signatures. The table is not a size claim, and an incomplete table cannot be assigned
 * by accident - a member that is not declared cannot be named. */
struct net_device_ops {
	int			(*ndo_init)(struct net_device *dev);
	void			(*ndo_uninit)(struct net_device *dev);
	int			(*ndo_open)(struct net_device *dev);
	int			(*ndo_stop)(struct net_device *dev);
	netdev_tx_t		(*ndo_start_xmit)(struct sk_buff *skb,
						  struct net_device *dev);
	u16			(*ndo_select_queue)(struct net_device *dev,
						    struct sk_buff *skb,
						    struct net_device *sb_dev);
	int			(*ndo_set_mac_address)(struct net_device *dev,
						       void *addr);
	int			(*ndo_do_ioctl)(struct net_device *dev,
						struct ifreq *ifr, int cmd);
	int			(*ndo_siocdevprivate)(struct net_device *dev,
						      struct ifreq *ifr,
						      void __user *data, int cmd);
	struct net_device_stats *(*ndo_get_stats)(struct net_device *dev);
};

/*
 * [LNX] include/linux/netdevice.h:2158-2650 (struct net_device).
 *
 * The two structural members are load-bearing and are transcribed: `dev`, because the
 * interface's SET_NETDEV_DEV() writes through it, and `priv[]`, the flexible tail the
 * interface's netdev_priv() returns; the cacheline alignment on the tail is the
 * interface's, and it is what keeps the driver's private area (allocated with the
 * device) aligned. Everything else is the named subset above.
 */
struct net_device {
	/* TX read-mostly hotpath */
	unsigned long		priv_flags;
	const struct net_device_ops *netdev_ops;
	struct netdev_queue	*_tx;
	unsigned int		real_num_tx_queues;
	unsigned short		needed_headroom;

	/* TXRX read-mostly hotpath */
	unsigned long		state;
	unsigned int		flags;
	netdev_features_t	features;
	void __rcu		*rx_handler_data;

	/* RX read-mostly hotpath */
	unsigned short		type;

	/* Control path */
	char			name[IFNAMSIZ];
	netdev_features_t	hw_features;
	const struct iw_handler_def *wireless_handlers;
	struct net_device_stats	stats;
	unsigned char		addr_len;
	struct wireless_dev	*ieee80211_ptr;
	const unsigned char	*dev_addr;
	unsigned int		num_tx_queues;
	int			watchdog_timeo;
	void			(*priv_destructor)(struct net_device *dev);
	unsigned long		gro_flush_timeout;

	struct device		dev;

	u8			priv[] __aligned(64);
} __aligned(64);

/* [LNX] include/linux/netdevice.h:2844-2853 */
#define	SET_NETDEV_DEV(net, pdev)	((net)->dev.parent = (pdev))
#define	SET_NETDEV_DEVTYPE(net, devtype)	((net)->dev.type = (devtype))

/* [LNX] include/linux/netdevice.h:2651-2652 */
#define	to_net_dev(d)	container_of(d, struct net_device, dev)

/*
 * [LNX] include/linux/netdevice.h:2836-2841 (netdev_priv): the interface returns the
 * record's flexible `priv[]` tail, which is where the private area allocated together
 * with the device lives. The offset assertion the interface keeps around this helper
 * exists to pin the tail for out-of-tree users; this port has no such ABI, so the
 * alignment on the member is the whole statement.
 */
static __always_inline __unused void *
netdev_priv(const struct net_device *dev)
{
	return (void *)dev->priv;
}

/* [LNX] include/linux/netdevice.h:2726-2731 */
static __always_inline __unused struct netdev_queue *
netdev_get_tx_queue(const struct net_device *dev, unsigned int index)
{
	return &dev->_tx[index];
}

/* [LNX] include/linux/netdevice.h:3710-3726 (the per-queue flow-control primitives).
 * The queue-state word is the contract with the stack: the device is asked to send
 * while the driver's "transmit stopped" bit is clear. */
static __always_inline __unused void
netif_tx_start_queue(struct netdev_queue *dev_queue)
{
	clear_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state);
}

static __always_inline __unused void
netif_tx_stop_queue(struct netdev_queue *dev_queue)
{
	set_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state);
}

/*
 * [LNX] include/linux/netdevice.h:3746-3752: the interface's own body clears the stop
 * bit and then schedules the queue's queueing discipline, because *that* is what makes
 * the stack resume sending - the queues are drained by a softirq the discipline raises.
 * This port has no queueing discipline (buffers reach the device through its own
 * transmit path), so there is nothing after the clear; the clear is the whole effect
 * here and is stated rather than hidden. The resumption of a port with a real transmit
 * stack belongs to that port's net-device seam, next to the definitions of the entry
 * points declared below.
 */
static __always_inline __unused void
netif_tx_wake_queue(struct netdev_queue *dev_queue)
{
	if (test_and_clear_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state)) {
		/* [LNX] the interface schedules dev_queue->qdisc here. */
	}
}

/* [LNX] include/linux/netdevice.h:3739-3766 (the device-wide wrappers) */
static __always_inline __unused void
netif_start_queue(struct net_device *dev)
{
	netif_tx_start_queue(netdev_get_tx_queue(dev, 0));
}

static __always_inline __unused void
netif_stop_queue(struct net_device *dev)
{
	netif_tx_stop_queue(netdev_get_tx_queue(dev, 0));
}

static __always_inline __unused void
netif_wake_queue(struct net_device *dev)
{
	netif_tx_wake_queue(netdev_get_tx_queue(dev, 0));
}

/* [LNX] include/linux/netdevice.h:3744-3756 and :3768-3780: the all-queue forms loop
 * over the transmit queues the device has. */
static __always_inline __unused void
netif_tx_start_all_queues(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++)
		netif_tx_start_queue(netdev_get_tx_queue(dev, i));
}

static __always_inline __unused void
netif_tx_wake_all_queues(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++)
		netif_tx_wake_queue(netdev_get_tx_queue(dev, i));
}

/* [LNX] include/linux/netdevice.h:3802: the stop form is an exported entry point in the
 * interface (`void netif_tx_stop_all_queues(struct net_device *dev)`), so it is one
 * here too, and its definition belongs with the other transmit entry points of the
 * port's net-device seam. */
void netif_tx_stop_all_queues(struct net_device *dev);

/* [LNX] include/linux/netdevice.h:3804-3818 */
static __always_inline __unused bool
netif_tx_queue_stopped(const struct netdev_queue *dev_queue)
{
	return test_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state);
}

static __always_inline __unused bool
netif_queue_stopped(const struct net_device *dev)
{
	return netif_tx_queue_stopped(netdev_get_tx_queue(dev, 0));
}

/*
 * [LNX] include/linux/netdevice.h:4115-4161: the per-subqueue forms. They are the
 * interface's own inline wrappers over the queue primitives above, with the queue index
 * named by the caller instead of being the first queue, so they keep the interface's
 * shape and bodies exactly; `netif_subqueue_stopped` is left out because no compiled
 * unit names it (it is the skb-mapping form of the same test).
 */
static __always_inline __unused void
netif_stop_subqueue(struct net_device *dev, u16 queue_index)
{
	struct netdev_queue *txq = netdev_get_tx_queue(dev, queue_index);

	netif_tx_stop_queue(txq);
}

static __always_inline __unused bool
__netif_subqueue_stopped(const struct net_device *dev, u16 queue_index)
{
	struct netdev_queue *txq = netdev_get_tx_queue(dev, queue_index);

	return netif_tx_queue_stopped(txq);
}

static __always_inline __unused void
netif_wake_subqueue(struct net_device *dev, u16 queue_index)
{
	struct netdev_queue *txq = netdev_get_tx_queue(dev, queue_index);

	netif_tx_wake_queue(txq);
}

/* [LNX] include/linux/netdevice.h:4082-4085 */
static __always_inline __unused bool
netif_running(const struct net_device *dev)
{
	return test_bit(__LINK_STATE_START, &dev->state);
}

/* [LNX] include/linux/netdevice.h:4637-4640 */
static __always_inline __unused bool
netif_carrier_ok(const struct net_device *dev)
{
	return !test_bit(__LINK_STATE_NOCARRIER, &dev->state);
}

/*
 * [LNX] include/linux/netdevice.h:4646-4647 (netif_carrier_on/off), :4757-4759
 * (netif_device_detach/attach), :3346-3365 (the device-event chain and its lookup
 * helper), :3444-3445 (dev_alloc_name), :3480-3485 and :3510 (register/unregister and
 * the final free), :5031 (ether_setup), :5048-5049 (register_netdev/unregister_netdev)
 * and :5091-5092 (dev_addr_mod). These are declared rather than defined: whether a
 * device can be registered at all, what bringing it up or down means, and how an
 * address change propagates are the answers of the port's net-device seam - an
 * unresolved symbol reports a missing answer, where a stub would hide one.
 */
void netif_carrier_on(struct net_device *dev);
void netif_carrier_off(struct net_device *dev);
void netif_device_detach(struct net_device *dev);
void netif_device_attach(struct net_device *dev);
int register_netdev(struct net_device *dev);
void unregister_netdev(struct net_device *dev);
int register_netdevice(struct net_device *dev);
void unregister_netdevice(struct net_device *dev);
void free_netdev(struct net_device *dev);
void ether_setup(struct net_device *dev);
int dev_alloc_name(struct net_device *dev, const char *name);
void dev_addr_mod(struct net_device *dev, unsigned int offset,
		  const void *addr, size_t len);
void netdev_set_default_ethtool_ops(struct net_device *dev,
				    const struct ethtool_ops *ops);

/*
 * [LNX] include/linux/etherdevice.h:57-60: the ethernet device allocators. The
 * interface states one declaration plus two macros over a queue count, so the
 * queue-count form is the same entry point; the private area is the record's `priv[]`
 * tail, per netdev_priv() above.
 */
struct net_device *alloc_etherdev_mqs(int sizeof_priv, unsigned int txqs,
				      unsigned int rxqs);
#define	alloc_etherdev_mq(sizeof_priv, count) \
	alloc_etherdev_mqs((sizeof_priv), (count), (count))
#define	alloc_etherdev(sizeof_priv)	alloc_etherdev_mq((sizeof_priv), 1)

/* [LNX] include/linux/netdevice.h:3268-3269: the device-event notifier chain. */
int register_netdevice_notifier(struct notifier_block *nb);
int unregister_netdevice_notifier(struct notifier_block *nb);

/* [LNX] include/linux/netdevice.h:1849-1856: the notification record and the lookup
 * helper every device notifier starts with. */
struct netdev_notifier_info {
	struct net_device	*dev;
};

static __always_inline __unused struct net_device *
netdev_notifier_info_to_dev(const struct netdev_notifier_info *info)
{
	return info->dev;
}

/*
 * [LNX] include/linux/netdevice.h:4334-4345: the receive entry points. netif_rx hands
 * a buffer to the stack from an interrupt-like context, netif_receive_skb from process
 * context, and both report one of the NET_RX_* results; napi_gro_receive feeds a buffer
 * into the receive-offload path and reports what happened to it (the gro_result
 * enumeration above). These are the port's receive seam and are declared for the same
 * reason the device entry points are.
 */
int netif_rx(struct sk_buff *skb);
int __netif_rx(struct sk_buff *skb);
int netif_receive_skb(struct sk_buff *skb);
gro_result_t napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb);

/*
 * [LNX] include/linux/netdevice.h:2884-3005 (the NAPI lifecycle) and :512-568 (the
 * scheduling pair). netif_napi_add() takes no weight in the interface any more - the
 * weight is fixed at NAPI_POLL_WEIGHT and the weighted forms are separate entry points;
 * napi_schedule() is the interface's own inline over the preparation test and the queue
 * call, so it keeps that shape here.
 */
void netif_napi_add(struct net_device *dev, struct napi_struct *napi,
		    int (*poll)(struct napi_struct *, int));
void netif_napi_del(struct napi_struct *napi);
void napi_enable(struct napi_struct *n);
void napi_disable(struct napi_struct *n);
bool napi_complete_done(struct napi_struct *n, int work_done);
bool napi_schedule_prep(struct napi_struct *n);
void __napi_schedule(struct napi_struct *n);

/* [LNX] include/linux/netdevice.h:594-597 */
static __always_inline __unused bool
napi_complete(struct napi_struct *n)
{
	return napi_complete_done(n, 0);
}

/* [LNX] include/linux/netdevice.h:560-568 */
static __always_inline __unused bool
napi_schedule(struct napi_struct *n)
{
	if (napi_schedule_prep(n)) {
		__napi_schedule(n);
		return true;
	}

	return false;
}

/* [LNX] include/linux/netdevice.h:515-518 */
static __always_inline __unused bool
napi_disable_pending(struct napi_struct *n)
{
	return test_bit(NAPI_STATE_DISABLE, &n->state);
}

/* [LNX] include/linux/netdevice.h:541-545 */
static __always_inline __unused bool
napi_is_scheduled(struct napi_struct *n)
{
	return test_bit(NAPI_STATE_SCHED, &n->state);
}

/*
 * [LNX] include/linux/netdev_features.h:11 (the bit enumeration `enum NETIF_F_*_BIT`)
 * and :103-132 (the `__NETIF_F_BIT`/`__NETIF_F` value forms). Only the flags the
 * compiled set names are carried, each with the interface's bit position: the feature
 * word is a 64-bit offload mask, and a wrong position would claim the wrong offload.
 * The positions below are the enumeration's: SG 0, IP_CSUM 1, IPV6_CSUM 4, HIGHDMA 5,
 * GSO 11, and the GSO block entered at NETIF_F_GSO_SHIFT 16 with TSO first and TSO6
 * fifth.
 */
#define	__NETIF_F_BIT(bit)	((netdev_features_t)1 << (bit))
#define	__NETIF_F(name)		__NETIF_F_BIT(NETIF_F_##name##_BIT)

#define	NETIF_F_SG_BIT			0	/* Scatter/gather IO. */
#define	NETIF_F_IP_CSUM_BIT		1	/* Can checksum TCP/UDP over IPv4. */
#define	NETIF_F_IPV6_CSUM_BIT		4	/* Can checksum TCP/UDP over IPV6. */
#define	NETIF_F_HIGHDMA_BIT		5	/* Can DMA to high memory. */
#define	NETIF_F_GSO_BIT			11	/* Enable software GSO. */
#define	NETIF_F_GSO_SHIFT		16	/* keep the order of SKB_GSO_* bits */
#define	NETIF_F_TSO_BIT			16	/* ... TCPv4 segmentation */
#define	NETIF_F_TSO6_BIT		20	/* ... TCPv6 segmentation */

#define	NETIF_F_SG		__NETIF_F(SG)
#define	NETIF_F_IP_CSUM		__NETIF_F(IP_CSUM)
#define	NETIF_F_IPV6_CSUM	__NETIF_F(IPV6_CSUM)
#define	NETIF_F_HIGHDMA		__NETIF_F(HIGHDMA)
#define	NETIF_F_GSO		__NETIF_F(GSO)
#define	NETIF_F_TSO		__NETIF_F(TSO)
#define	NETIF_F_TSO6		__NETIF_F(TSO6)

#endif /* _LINUXKPI_LINUX_NETDEVICE_H_ */
