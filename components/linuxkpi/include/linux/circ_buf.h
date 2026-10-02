#ifndef _LINUXKPI_LINUX_CIRC_BUF_H_
#define _LINUXKPI_LINUX_CIRC_BUF_H_
/* [S6] B8: include-only stub (osdep funnel; nothing consumes the shape) */
struct circ_buf {
	char	*buf;
	int	head;
	int	tail;
};
#endif
