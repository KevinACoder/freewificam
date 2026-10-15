/**
 * @file    lwip_smoke.c
 * @brief   Case lwip-loopback: the stack comes alive and loops a socket.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case row of the regression table (tests/board/common/board_suite.h);
 *          the roundtrip shape is the stack's own documented loopback path -
 *          the upstream loop interface (third-party/lwip/src/core/netif.c:155-177,
 *          added by netif_init when LWIP_HAVE_LOOPIF is on) carries
 *          socket -> connect -> accept -> send -> recv without any board
 *          network hardware, which is what makes it the right anchor for the
 *          "is the stack alive" question (real link work is the gmac data
 *          plane's case).
 *
 * Alive means three things here: the TCP/IP thread was created and reached
 * its init callback (net: READY), the loopback netif is up at 127.0.0.1, and
 * a byte pattern crosses the socket API, the loopback path, the TCP state
 * machine and comes back identical in both directions.
 */

#include <string.h>

#include "board_suite.h"

#include "os/cmsis2/cmsis2.h"

#include "lwip/sockets.h"

#include "log.h"

#include "lwip_stack.h"

#define ECHO_PORT 7777
#define ECHO_LEN  32

/* Receive exactly len bytes, honouring the socket's receive timeout. */
static int recv_all(int s, unsigned char *buf, int len)
{
	int got = 0;

	while (got < len) {
		int n = lwip_recv(s, buf + got, (size_t)(len - got), 0);

		if (n <= 0) {
			return (n < 0) ? -1 : got;
		}
		got += n;
	}
	return got;
}

static void close_if_open(int s)
{
	if (s >= 0) {
		(void)lwip_close(s);
	}
}

static void lwip_loopback(void)
{
	struct sockaddr_in addr;
	struct timeval tv = { 2, 0 };
	unsigned char tx[ECHO_LEN];
	unsigned char rx[ECHO_LEN];
	int listener = -1;
	int client = -1;
	int server = -1;
	int i;

	/* The stack's entry point: initialises the stack, adds the loopback
	 * netif, creates the TCP/IP thread and runs the callback on it. */
	if (lwip_stack_up() != 0) {
		board_case_fail("tcpip_init callback never ran (5s)");
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = lwip_htons(ECHO_PORT);
	addr.sin_addr.s_addr = lwip_htonl(INADDR_LOOPBACK);

	listener = lwip_socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0) {
		board_case_fail("socket(listener) failed");
		return;
	}
	(void)lwip_setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (lwip_bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		board_case_fail("bind(127.0.0.1) failed");
		close_if_open(listener);
		return;
	}
	if (lwip_listen(listener, 1) != 0) {
		board_case_fail("listen failed");
		close_if_open(listener);
		return;
	}

	client = lwip_socket(AF_INET, SOCK_STREAM, 0);
	if (client < 0) {
		board_case_fail("socket(client) failed");
		close_if_open(listener);
		return;
	}
	(void)lwip_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (lwip_connect(client, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		board_case_fail("connect(127.0.0.1) failed");
		close_if_open(client);
		close_if_open(listener);
		return;
	}

	server = lwip_accept(listener, NULL, NULL);
	if (server < 0) {
		board_case_fail("accept timed out (loopback handshake)");
		close_if_open(client);
		close_if_open(listener);
		return;
	}

	for (i = 0; i < ECHO_LEN; i++) {
		tx[i] = (unsigned char)('a' + (i % 26));
	}

	if (lwip_send(client, tx, ECHO_LEN, 0) != ECHO_LEN) {
		board_case_fail("client send failed");
		goto out;
	}
	memset(rx, 0, sizeof(rx));
	if (recv_all(server, rx, ECHO_LEN) != ECHO_LEN) {
		board_case_fail("server recv timed out");
		goto out;
	}
	if (memcmp(tx, rx, ECHO_LEN) != 0) {
		board_case_fail("server side bytes differ");
		goto out;
	}

	if (lwip_send(server, rx, ECHO_LEN, 0) != ECHO_LEN) {
		board_case_fail("server echo send failed");
		goto out;
	}
	memset(rx, 0, sizeof(rx));
	if (recv_all(client, rx, ECHO_LEN) != ECHO_LEN) {
		board_case_fail("client recv timed out");
		goto out;
	}
	if (memcmp(tx, rx, ECHO_LEN) != 0) {
		board_case_fail("client side bytes differ");
		goto out;
	}

	LOG_INFO("net: loopback echo ok port=%u sent=%u got=%u\n",
		  (unsigned)ECHO_PORT, (unsigned)ECHO_LEN, (unsigned)ECHO_LEN);

out:
	close_if_open(server);
	close_if_open(client);
	close_if_open(listener);
}

BOARD_TEST("lwip-loopback", lwip_loopback);
