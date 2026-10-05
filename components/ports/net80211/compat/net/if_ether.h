/*
 * @file
 * @brief Ethernet shell: header layout and broadcast address.
 */

#ifndef _COMPAT_NET_IF_ETHER_H_
#define _COMPAT_NET_IF_ETHER_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <net/if.h>

#define ETHER_ADDR_LEN 6
#define ETHERTYPE_LEN 2
#define ETHER_HDR_LEN (ETHER_ADDR_LEN * 2 + ETHERTYPE_LEN)
#define ETHER_CRC_LEN 4
#define ETHER_MAX_LEN 1518
#define ETHERMIN (ETHER_MAX_LEN - ETHER_CRC_LEN - ETHER_HDR_LEN - 1)

struct ether_addr {
	uint8_t ether_addr_octet[ETHER_ADDR_LEN];
};

struct ether_header {
	uint8_t ether_dhost[ETHER_ADDR_LEN];
	uint8_t ether_shost[ETHER_ADDR_LEN];
	uint16_t ether_type;
};

#define ETHERTYPE_IP 0x0800
#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_VLAN 0x8100
#define ETHERTYPE_IPV6 0x86dd
#define ETHERTYPE_PAE 0x888e

#define ETHER_IS_MULTICAST(addr) ((addr)[0] & 0x01)

extern const uint8_t etherbroadcastaddr[ETHER_ADDR_LEN];
extern const uint8_t etheripaddr[ETHER_ADDR_LEN];

struct ethercom {
	struct ifnet ec_if;
	unsigned int ec_multicnt;
};


#define EVL_PRIOFTAG(tag) (((tag) >> 13) & 0x7)
#define EVL_VLANOFTAG(tag) ((tag) & 0xfff)
#define EVL_MAKETAG(pri, id) ((((pri) & 0x7) << 13) | ((id) & 0xfff))

const char *ether_sprintf(const uint8_t *mac);


/* the multicast filters are a no-op on the ports (the firmware
 * accepts group frames anyway) */
static inline int ether_addmulti(const struct sockaddr *sa,
	struct ethercom *ec) {
	(void) sa; (void) ec;
	return 0;
}
static inline int ether_delmulti(const struct sockaddr *sa,
	struct ethercom *ec) {
	(void) sa; (void) ec;
	return 0;
}

#endif /* _COMPAT_NET_IF_ETHER_H_ */

char *ether_snprintf(char *buf, size_t len, const uint8_t *mac);


