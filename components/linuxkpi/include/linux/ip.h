#ifndef _LINUXKPI_LINUX_IP_H_
#define _LINUXKPI_LINUX_IP_H_
/* [S6] B8: the IP header shape the RX path reads (iph->protocol etc.) */
struct iphdr {
	unsigned char	ihl:4,
			version:4;
	unsigned char	tos;
	unsigned short	tot_len;
	unsigned short	id;
	unsigned short	frag_off;
	unsigned char	ttl;
	unsigned char	protocol;
	unsigned short	check;
	unsigned int	saddr;
	unsigned int	daddr;
} __packed;
#endif
