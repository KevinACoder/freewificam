/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/sections.h>
#include <zephyr/toolchain.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_pkt.h>

#include "includes.h"
#include "utils/common.h"
#include "eloop.h"
#include "l2_packet.h"
#include "common/eapol_common.h"
struct l2_packet_data
{
    char ifname[17];
    u8 own_addr[ETH_ALEN];
    int ifindex;
    struct net_if *iface;
    void (*rx_callback)(void *ctx, const u8 *src_addr, const u8 *buf, size_t len);
    void *rx_callback_ctx;
    int l2_hdr; /* whether to include layer 2 (Ethernet) header data
                 * buffers */
    int fd;
    unsigned short protocol;
};

int l2_packet_get_own_addr(struct l2_packet_data *l2, u8 *addr)
{
    os_memcpy(addr, l2->own_addr, ETH_ALEN);
    return 0;
}

int l2_packet_send(struct l2_packet_data *l2, const u8 *dst_addr, u16 proto, const u8 *buf, size_t len)
{
    int ret;

    if (l2 == NULL)
    {
        return -1;
    }

    if (l2->l2_hdr)
    {
        ret = send(l2->fd, buf, len, 0);
        if (ret < 0)
        {
            wpa_printf(MSG_ERROR, "l2_packet_send - send: %s", strerror(errno));
        }
    }
    else
    {
        struct sockaddr_ll ll;

        os_memset(&ll, 0, sizeof(ll));
        ll.sll_family   = AF_PACKET;
        ll.sll_ifindex  = l2->ifindex;
        ll.sll_protocol = htons(proto);
        ll.sll_halen    = ETH_ALEN;
        os_memcpy(ll.sll_addr, dst_addr, ETH_ALEN);
        // TODO: This takes up too much stack, call wifi driver TX directly?
        ret = sendto(l2->fd, buf, len, 0, (struct sockaddr *)&ll, sizeof(ll));
        if (ret < 0)
        {
            wpa_printf(MSG_ERROR, "l2_packet_send - sendto: %s", strerror(errno));
        }
    }
    return ret;
}

static void l2_packet_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
    struct l2_packet_data *l2 = eloop_ctx;
    u8 buf[2300];
    int res;
    struct sockaddr_ll ll;
    socklen_t fromlen;
    const struct ieee802_1x_hdr *hdr;

    os_memset(&ll, 0, sizeof(ll));
    fromlen = sizeof(ll);
    res     = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&ll, &fromlen);
    if (res < 0)
    {
        wpa_printf(MSG_ERROR, "RAW : failed to recv error %d", errno);
        return;
    }

    // FIXME: sll_addr is not being filled as L2 header is not removed
    wpa_printf(MSG_DEBUG, "%s: src=" MACSTR " proto=0x%x len=%d", __func__, MAC2STR(ll.sll_addr), ll.sll_protocol,
               (int)res);

    hdr = (const struct ieee802_1x_hdr *)buf;

    // FIXME: L2 header is now removed but sll_protocol is not set.
    // So, as a workaround add this check to drop packets.
    if (hdr->type != IEEE802_1X_TYPE_EAPOL_KEY)
        return;

    // FIXME: We should only get registered protcols from networking stack
    // but for some reason ETH_P_ALL is being set (bind_default)
    l2->rx_callback(l2->rx_callback_ctx, ll.sll_addr, buf, res);
}

struct l2_packet_data *l2_packet_init(const char *ifname,
                                      const u8 *own_addr,
                                      unsigned short protocol,
                                      void (*rx_callback)(void *ctx, const u8 *src_addr, const u8 *buf, size_t len),
                                      void *rx_callback_ctx,
                                      int l2_hdr)
{
    struct l2_packet_data *l2;
    struct sockaddr_ll ll;
    int ret                        = 0;
    struct net_linkaddr *link_addr = NULL;
    struct net_if *iface           = NULL;
    const struct device *device    = device_get_binding(ifname);

    if (!device)
    {
        wpa_printf(MSG_ERROR, "Cannot get device for: %s\n", ifname);
        return NULL;
    }

    iface = net_if_lookup_by_dev(device);

    if (!iface)
    {
        wpa_printf(MSG_ERROR, "Cannot get device for: %s\n", ifname);
        return NULL;
    }

    l2 = os_zalloc(sizeof(struct l2_packet_data));
    if (l2 == NULL)
    {
        return NULL;
    }
    os_strlcpy(l2->ifname, ifname, sizeof(l2->ifname));
    l2->rx_callback     = rx_callback;
    l2->rx_callback_ctx = rx_callback_ctx;
    l2->l2_hdr          = l2_hdr;
    l2->protocol        = protocol;
    l2->iface           = iface;

    l2->ifindex = net_if_get_by_iface(l2->iface);

    if (!l2->ifindex)
    {
        wpa_printf(MSG_ERROR, "Cannot get  interface index for: %s\n", l2->ifname);
        os_free(l2);
        return NULL;
    }

    link_addr = &iface->if_dev->link_addr;
    os_memcpy(l2->own_addr, link_addr->addr, link_addr->len);

    l2->fd = socket(AF_PACKET, l2_hdr ? SOCK_RAW : SOCK_DGRAM, htons(protocol));
    if (l2->fd < 0)
    {
        wpa_printf(MSG_ERROR, "Failed to open socket: %s, proto: %d, af: %d", strerror(errno), htons(protocol),
                   AF_PACKET);
        goto fail;
    }

    os_memset(&ll, 0, sizeof(ll));
    ll.sll_family = AF_PACKET;

    ll.sll_ifindex  = l2->ifindex;
    ll.sll_protocol = htons(protocol);

    // FIXME: This should skip bind_default to ETH_P_ALL, but its not.
    memcpy(ll.sll_addr, l2->own_addr, ETH_ALEN);

    ret = bind(l2->fd, (const struct sockaddr *)&ll, sizeof(ll));
    if (ret < 0)
    {
        goto fail;
    }

    if (rx_callback)
    {
        eloop_register_read_sock(l2->fd, l2_packet_receive, l2, NULL);
    }

    wpa_printf(MSG_INFO, "l2_packet_init: iface %s ifindex %d", l2->ifname, l2->ifindex);

    return l2;
fail:
    if (l2->fd >= 0)
        close(l2->fd);
    os_free(l2);
    wpa_printf(MSG_ERROR, "Failed to create l2_packet: %d\n", ret);
    return NULL;
}

struct l2_packet_data *l2_packet_init_bridge(
    const char *br_ifname,
    const char *ifname,
    const u8 *own_addr,
    unsigned short protocol,
    void (*rx_callback)(void *ctx, const u8 *src_addr, const u8 *buf, size_t len),
    void *rx_callback_ctx,
    int l2_hdr)
{
    return l2_packet_init(br_ifname, own_addr, protocol, rx_callback, rx_callback_ctx, l2_hdr);
}

void l2_packet_deinit(struct l2_packet_data *l2)
{
    if (l2 == NULL)
    {
        return;
    }

    if (l2->fd >= 0)
    {
        eloop_unregister_read_sock(l2->fd);
        /* TODO: close connection */
    }

    os_free(l2);
}

int l2_packet_get_ip_addr(struct l2_packet_data *l2, char *buf, size_t len)
{
#ifdef CONFIG_NET_IPV4
    char addr_buf[NET_IPV4_ADDR_LEN];
    os_strlcpy(buf,
               net_addr_ntop(AF_INET, &l2->iface->config.ip.ipv4->unicast[0].address.in_addr.s_addr, addr_buf,
                             sizeof(addr_buf)),
               len);
    return 0;
#else
    return -1;
#endif
}

void l2_packet_notify_auth_start(struct l2_packet_data *l2)
{
    /* This function can be left empty */
}

int l2_packet_set_packet_filter(struct l2_packet_data *l2, enum l2_packet_filter_type type)
{
    /* TODO: Set filter */
    return 0;
}
