/*
 * @file   iperf3_cmd.c
 * @brief  iperf3 command: thin cherrysh front end for the vendored
 *         iperf3_embedded client (third-party/iperf3_embedded).
 *
 * The library spawns its own worker thread and reports intervals through
 * the diag console, so the command only translates arguments and returns -
 * a long run must not hold the shell. The peer is a plain PC iperf3
 * server ("iperf3 -s", default port 5201).
 *
 * @author zhugengyu
 * @date   22.09.2026
 */

#include <stdlib.h>
#include <string.h>

#include "lwip/ip4_addr.h"

#include "iperf3_embedded.h"

#include "cherrysh_adapter.h"
#include "csh.h"

static int cmd_iperf3(int argc, char **argv)
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	iperf3_client_cfg_t cfg;
	ip4_addr_t parsed;
	int i;
	int have_ip = 0;

	if (argc >= 2 && strcmp(argv[1], "stop") == 0) {
		if (!iperf3_client_is_running()) {
			csh_printf(csh, "iperf3: not running\n");
			return 0;
		}
		iperf3_client_stop();
		csh_printf(csh, "iperf3: stop requested\n");
		return 0;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.port = 5201;
	cfg.time_sec = 10;
	cfg.interval_sec = 1;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-u") == 0) {
			cfg.udp = 1;
		} else if (strcmp(argv[i], "-r") == 0) {
			cfg.reverse = 1;
		} else if (argv[i][0] == '-') {
			csh_printf(csh, "iperf3: unknown option %s\n", argv[i]);
			return 0;
		} else if (!have_ip) {
			if (!ip4addr_aton(argv[i], &parsed)) {
				csh_printf(csh, "iperf3: bad address %s\n", argv[i]);
				return 0;
			}
			cfg.dest_ip = parsed.addr;	/* network byte order */
			have_ip = 1;
		} else {
			uint32_t sec = (uint32_t) atoi(argv[i]);

			cfg.time_sec = (sec != 0U) ? sec : 10U;
		}
	}

	if (!have_ip) {
		csh_printf(csh, "usage: iperf3 <ip> [sec] [-u] [-r] | stop"
			   "   (-u UDP, -r reverse = server sends)\n");
		return 0;
	}
	if (iperf3_client_is_running()) {
		csh_printf(csh, "iperf3: test already running\n");
		return 0;
	}
	if (iperf3_client_start(&cfg) != 0) {
		csh_printf(csh, "iperf3: failed to start\n");
		return 0;
	}
	csh_printf(csh, "iperf3: %s-%s test vs %s started, reports on console\n",
		   cfg.udp ? "udp" : "tcp", cfg.reverse ? "reverse" : "forward",
		   argv[1]);
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_iperf3, iperf3, "iperf3 <ip> [sec] [-u] [-r]",
			  "iperf3 client - vendored iperf3_embedded vs PC iperf3 -s");
