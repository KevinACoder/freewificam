/*
 * @file   gdb_packet.c
 * @brief  RSP packet framing, checksum and hex conversion.
 *
 * Wire behaviour from the prior image's stub (itself from FreeBSD
 * sys/gdb/gdb_packet.c, BSD-2, 2004 Marcel Moolenaar), reduced to the
 * synchronous polled-transport shape: frame = $<payload>#<2 checksum hex>,
 * ACK '+' / NAK '-' with one retransmit. No run-length, no escape
 * handling - not advertised in qSupported, so the host will not send
 * either.
 */

#include <stdint.h>
#include <stddef.h>

#include "minilibc.h"
#include "gdb.h"
#include "gdb_int.h"

static const struct gdb_dbgport *dp;

void gdb_dbgport_register(const struct gdb_dbgport *ops)
{
	dp = ops;
}

int gdb_dbgport_ready(void)
{
	return dp != 0;
}

static int gdb_getc(void)
{
	if (dp == 0 || dp->getc == 0) {
		return -1;
	}
	return dp->getc();
}

static void gdb_putc(char c)
{
	if (dp != 0 && dp->putc != 0) {
		dp->putc(c);
	}
}

void gdb_dbgport_intr_ctrl(int on)
{
	if (dp != 0 && dp->intr_ctrl != 0) {
		dp->intr_ctrl(on);
	}
}

/* Send one fully framed packet, fire-and-forget. The FreeBSD original
 * waits for the host's '+' and resends on '-'; with a polled UART and no
 * host attached that would spin at wire speed forever. GDB re-requests
 * anything it finds corrupted, so a dropped packet costs one retry, not
 * correctness. */
void gdb_send_packet(const char *data, unsigned int len)
{
	unsigned char checksum = 0U;
	unsigned int i;

	gdb_putc('$');
	for (i = 0U; i < len; i++) {
		gdb_putc(data[i]);
		checksum = (unsigned char)(checksum + (unsigned char)data[i]);
	}
	gdb_putc('#');
	gdb_putc("0123456789abcdef"[checksum >> 4]);
	gdb_putc("0123456789abcdef"[checksum & 0xfU]);
}

/* Reply helpers used by the command layer. */
void gdb_reply_str(const char *s)
{
	gdb_send_packet(s, (unsigned int)strlen(s));
}

void gdb_reply_ok(void)
{
	gdb_reply_str("OK");
}

void gdb_reply_err(unsigned char err)
{
	char b[4];

	b[0] = 'E';
	b[1] = "0123456789abcdef"[err >> 4];
	b[2] = "0123456789abcdef"[err & 0xfU];
	b[3] = '\0';
	gdb_reply_str(b);
}

/* Receive one packet into buf. Returns payload length, or -1 on a malformed
 * frame (caller just loops; the ACK already told the host to resend), or
 * GDB_PKT_INTR when the host break-in byte 0x03 shows up between packets.
 * Leading '+', '-' and XOFF/XOUT chatter before the '$' is skipped. */
int gdb_get_packet(char *buf, unsigned int bufsz)
{
	unsigned char checksum = 0U;
	unsigned int count = 0U;
	int c;
	int hex[2];

	do {
		c = gdb_getc();
		if (c == 0x03) {
			return GDB_PKT_INTR;
		}
	} while (c != '$');

	while ((c = gdb_getc()) != '#') {
		if (c < 0) {
			continue;
		}
		if (c == '$') {
			/* Restarted frame mid-read: start over. */
			checksum = 0U;
			count = 0U;
			continue;
		}
		if (count >= bufsz - 1U) {
			/* Oversized: drain to '#', then NAK. */
			while ((c = gdb_getc()) != '#') {
				if (c < 0) {
					continue;
				}
			}
			gdb_putc('-');
			return -1;
		}
		buf[count++] = (char)c;
		checksum = (unsigned char)(checksum + (unsigned char)c);
	}

	hex[0] = gdb_getc();
	hex[1] = gdb_getc();
	if (hex[0] < 0 || hex[1] < 0) {
		gdb_putc('-');
		return -1;
	}

	if ((unsigned char)((hexval(hex[0]) << 4) | hexval(hex[1])) == checksum) {
		gdb_putc('+');
		buf[count] = '\0';
		return (int)count;
	}

	gdb_putc('-');
	return -1;
}

/* --- hex helpers ---------------------------------------------------------- */

int hexval(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

unsigned int hex2mem(const char *hex, unsigned char *mem, unsigned int nbytes)
{
	unsigned int i;

	for (i = 0U; i < nbytes; i++) {
		int hi = hexval(hex[2U * i]);
		int lo = hexval(hex[2U * i + 1U]);

		if (hi < 0 || lo < 0) {
			break;
		}
		mem[i] = (unsigned char)((hi << 4) | lo);
	}
	return i;
}

char *mem2hex(const unsigned char *mem, char *hex, unsigned int nbytes)
{
	static const char digits[] = "0123456789abcdef";
	unsigned int i;

	for (i = 0U; i < nbytes; i++) {
		hex[2U * i] = digits[mem[i] >> 4];
		hex[2U * i + 1U] = digits[mem[i] & 0xfU];
	}
	hex[2U * nbytes] = '\0';
	return hex;
}
