/*
 * @file   iperf2_cmd.c
 * @brief  iperf2 command: lwiperf TCP client (the Phytium iperf shape).
 *
 * lwiperf's traffic generation is the reference TX shape the 921 Mbit/s
 * Phytium result was measured with: it runs as a raw-API state machine
 * inside the tcpip thread and tcp_write()s from a static const pattern
 * buffer with no-copy flags - no worker thread, no app-level copy. The
 * vendored iperf3_embedded client (COPY semantics, band-2 worker) stays
 * the protocol-compatible surface; this command is its copy-count
 * attribution pair on the TX side (specs/netutils-adapt, N7 R3).
 *
 * The raw API must not race the tcpip thread, so start/abort run under the
 * core lock (LWIP_TCPIP_CORE_LOCKING=1). lwiperf programs amount=-1000
 * (iperf2 time mode, 10 s) into its header, so the session ends by itself
 * and the report arrives on the console from the callback below.
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

#include <string.h>

#include "lwip/ip4_addr.h"
#include "lwip/tcpip.h"
#include "lwip/apps/lwiperf.h"

#include "log.h"

#include "cherrysh_adapter.h"
#include "csh.h"

static void *s_session;

/* Runs in the tcpip thread: the console is the only output channel. */
static void iperf2_report(void *arg, enum lwiperf_report_type type,
			  const ip_addr_t *local_addr, u16_t local_port,
			  const ip_addr_t *remote_addr, u16_t remote_port,
			  u32_t bytes, u32_t ms, u32_t kbitpsec)
{
	(void)arg;
	(void)local_addr;
	(void)local_port;
	(void)remote_addr;
	(void)remote_port;
	if ((type == LWIPERF_TCP_DONE_CLIENT) || (type == LWIPERF_TCP_ABORTED_LOCAL) ||
	    (type == LWIPERF_TCP_ABORTED_LOCAL_TXERROR) ||
	    (type == LWIPERF_TCP_ABORTED_REMOTE)) {
		s_session = NULL;
	}
	board_log("lwiperf: client report type=%d bytes=%u ms=%u %ukbit/s\n",
		  (int)type, (unsigned)bytes, (unsigned)ms, (unsigned)kbitpsec);
}

/* Shared by the shell command and the boot autorun (unattended ladder
 * rounds): 0 started, -1 bad address or lwiperf refused. */
int iperf2_client_start(const char *ipstr)
{
	ip_addr_t remote;

	if ((ipstr == NULL) || !ip4addr_aton(ipstr, ip_2_ip4(&remote))) {
		return -1;
	}

	LOCK_TCPIP_CORE();
	s_session = lwiperf_start_tcp_client(&remote, LWIPERF_TCP_PORT_DEFAULT,
					     LWIPERF_CLIENT, iperf2_report, NULL);
	UNLOCK_TCPIP_CORE();
	if (s_session == NULL) {
		return -1;
	}
	board_log("iperf2: lwiperf client vs %s started (10s, report on console)\n",
		  ipstr);
	return 0;
}

static int cmd_iperf2(int argc, char **argv)
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);

	if ((argc >= 2) && (strcmp(argv[1], "stop") == 0)) {
		LOCK_TCPIP_CORE();
		if (s_session != NULL) {
			lwiperf_abort(s_session);
			s_session = NULL;
		}
		UNLOCK_TCPIP_CORE();
		csh_printf(csh, "iperf2: stop requested\n");
		return 0;
	}

	if (argc < 2) {
		csh_printf(csh, "usage: iperf2 <ip> | stop"
				"   (10s lwiperf client vs PC iperf2 -s; report on console)\n");
		return 0;
	}
	if (iperf2_client_start(argv[1]) != 0) {
		csh_printf(csh, "iperf2: start FAILED (bad ip or lwiperf refused)\n");
	}
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_iperf2, iperf2, "iperf2 <ip> | stop",
			  "lwiperf TCP client (no-copy TX shape) vs PC iperf2 -s");
