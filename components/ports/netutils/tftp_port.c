/*
 * @file   tftp_port.c
 * @brief  netutils tftp port file for this trunk: RAM file hooks + CherrySH CLI.
 *
 * Upstream's tftp_port.c (not vendored) binds the tftp core to dfs_posix and
 * msh. This trunk has no filesystem, so the four tftp_file_* hooks bind to
 * RAM slots instead: a pulled file lands in a growable heap block, a pushed
 * file comes from one. That keeps the TFTP client/server usable for the
 * comprehensive-network-test suite (round-trip integrity against the
 * oh-my-oslab TFTP server) without waiting on the fatfs/sdmmc line. The CLI
 * prints size + CRC32 per transfer; the host side cross-checks with
 * python's zlib.crc32 over the same bytes.
 *
 * Slot lifecycle: open() claims a slot (name recorded, content reset for
 * writes), close() leaves the content in place so a pulled file can be
 * pushed back out or listed afterwards; a subsequent open() recycles the
 * oldest slot when both are taken.
 */

#include <rtthread.h>
#include <cmsis_os2.h>
#include <string.h>
#include <stdio.h>
#include "csh.h"
#include "cherrysh_adapter.h"
#include "tftp.h"

/* --- RAM slots ------------------------------------------------------------- */

#define TFTP_SLOT_MAX		2
#define TFTP_SLOT_CAP		(128u * 1024u)

struct tftp_slot {
	uint8_t in_use;
	int is_write;
	char name[48];
	uint8_t *buf;
	uint32_t cap;
	uint32_t len;
};

/* Static storage, not the TLSF heap: the wlan/wpa world already leans hard
 * on the 1 MB heap, and a capture slot growing through realloc was the
 * first thing to die under that pressure (board-proven 2026-09-25: the
 * transfer stopped dead at the initial 16 KB floor). 96 MB of RAM budget
 * vs a ~1 MB image makes .bss the honest home. */
static uint8_t slot_storage[TFTP_SLOT_MAX][TFTP_SLOT_CAP];
static struct tftp_slot slots[TFTP_SLOT_MAX];

/* last slot touched by the hooks - what a finished transfer prints */
static struct tftp_slot *last_slot;

static uint32_t crc32_bytes(const uint8_t *p, uint32_t len)
{
	uint32_t crc = 0xFFFFFFFFu;
	uint32_t i;
	int b;

	for (i = 0; i < len; i++) {
		crc ^= p[i];
		for (b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(crc & 1u)));
		}
	}
	return crc ^ 0xFFFFFFFFu;
}

/* --- the four hooks the tftp core links against ----------------------------- */

void *tftp_file_open(const char *fname, const char *mode, int is_write)
{
	struct tftp_slot *s = NULL;
	uint32_t i;
	uint32_t victim = 0;

	if (strcmp(mode, "octet") != 0) {
		rt_kprintf("tftp: no support for mode(%s)\n", mode);
		return NULL;
	}

	/* free slot first; otherwise recycle the oldest */
	for (i = 0; i < TFTP_SLOT_MAX; i++) {
		if (!slots[i].in_use) {
			s = &slots[i];
			break;
		}
	}
	if (s == NULL) {
		i = victim;
		s = &slots[victim];
		rt_kprintf("tftp: recycling RAM slot '%s'\n", s->name);
	}

	s->in_use = 1;
	s->is_write = is_write;
	s->buf = slot_storage[i];	/* the storage is the slot's home */
	s->cap = TFTP_SLOT_CAP;
	strncpy(s->name, fname, sizeof(s->name) - 1u);
	s->name[sizeof(s->name) - 1u] = '\0';
	if (is_write) {
		s->len = 0;	/* fresh target */
	}
	/* reads keep existing content: a push serves a previously pulled file */
	last_slot = s;
	return s;
}

int tftp_file_write(void *handle, int pos, void *buff, int len)
{
	struct tftp_slot *s = (struct tftp_slot *)handle;

	if (s == NULL || !s->in_use || len < 0) {
		return -1;
	}
	if ((uint32_t)pos + (uint32_t)len > s->cap) {
		return -1;	/* past the static slot: file too large */
	}
	memcpy(s->buf + pos, buff, (uint32_t)len);
	if ((uint32_t)pos + (uint32_t)len > s->len) {
		s->len = (uint32_t)pos + (uint32_t)len;
	}
	return len;
}

int tftp_file_read(void *handle, int pos, void *buff, int len)
{
	struct tftp_slot *s = (struct tftp_slot *)handle;
	uint32_t n;

	if (s == NULL || !s->in_use || len < 0 || pos < 0) {
		return -1;
	}
	if ((uint32_t)pos >= s->len) {
		return 0;	/* EOF */
	}
	n = s->len - (uint32_t)pos;
	if (n > (uint32_t)len) {
		n = (uint32_t)len;
	}
	memcpy(buff, s->buf + pos, n);
	return (int)n;
}

void tftp_file_close(void *handle)
{
	struct tftp_slot *s = (struct tftp_slot *)handle;

	if (s != NULL) {
		s->in_use = 0;	/* content stays for -w/-l reuse */
	}
}

/* --- thread hook (tftp server) ---------------------------------------------- */

int tftp_thread_create(void **task, void (*entry)(void *param), void *param)
{
	osThreadAttr_t attr;
	osThreadId_t tid;

	attr.name = "tftps";
	attr.attr_bits = 0;
	attr.cb_mem = NULL;
	attr.cb_size = 0;
	attr.stack_mem = NULL;
	attr.stack_size = 2048;
	attr.priority = osPriorityNormal;
	attr.tz_module = 0;

	tid = osThreadNew((osThreadFunc_t)entry, param, &attr);
	*task = (void *)tid;
	return (tid != NULL) ? 0 : -1;
}

/* --- CherrySH CLI ------------------------------------------------------------ */

static struct tftp_server *g_server;
static void *g_server_task;

static void tftp_session_help(chry_shell_t *csh)
{
	csh_printf(csh, "Usage:\n");
	csh_printf(csh, "  tftp -r <ip> <remote> [local]  pull file into RAM\n");
	csh_printf(csh, "  tftp -w <ip> <remote> [local]  push RAM slot to server\n");
	csh_printf(csh, "  tftp -s [port]                 start RAM-backed server\n");
	csh_printf(csh, "  tftp --stop                    stop the server\n");
	csh_printf(csh, "  tftp -l                        list RAM slots\n");
}

static void tftp_slot_print(chry_shell_t *csh, const struct tftp_slot *s)
{
	csh_printf(csh, "  %-40s %10u bytes  crc32 %08x\n",
		   s->name, s->len, crc32_bytes(s->buf, s->len));
}

static int cmd_tftp(int argc, char *argv[])
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	const char *ip = NULL;
	const char *path[2] = {NULL, NULL};
	int port = 69;
	int mode = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "-w") == 0) {
			mode = argv[i][1];
			if (i + 1 < argc) {
				ip = argv[++i];
			}
		} else if (strcmp(argv[i], "-s") == 0) {
			mode = 's';
			if (i + 1 < argc && argv[i + 1][0] >= '0' &&
			    argv[i + 1][0] <= '9') {
				port = atoi(argv[++i]);
			}
		} else if (strcmp(argv[i], "--stop") == 0) {
			mode = 'x';
		} else if (strcmp(argv[i], "-l") == 0) {
			mode = 'l';
		} else if (strcmp(argv[i], "-h") == 0) {
			mode = 'h';
		} else if (path[0] == NULL) {
			path[0] = argv[i];
		} else if (path[1] == NULL) {
			path[1] = argv[i];
		} else {
			tftp_session_help(csh);
			return -1;
		}
	}

	switch (mode) {
	case 'h':
	case 0:
		tftp_session_help(csh);
		return (mode == 'h') ? 0 : -1;

	case 'l': {
		uint32_t j;

		for (j = 0; j < TFTP_SLOT_MAX; j++) {
			if (slots[j].buf != NULL) {
				tftp_slot_print(csh, &slots[j]);
			}
		}
		return 0;
	}

	case 'x':
		if (g_server != NULL) {
			tftp_server_destroy(g_server);
			g_server = NULL;
			g_server_task = NULL;
			csh_printf(csh, "tftp server stopped\n");
		}
		return 0;

	case 's': {
		if (g_server != NULL) {
			csh_printf(csh, "tftp server already running\n");
			return -1;
		}
		g_server = tftp_server_create(path[0] ? path[0] : "/", port);
		if (g_server == NULL) {
			csh_printf(csh, "tftp server create failed\n");
			return -1;
		}
		tftp_server_write_set(g_server, 1);
		if (tftp_thread_create((void **)&g_server_task,
				       (void (*)(void *))tftp_server_run,
				       g_server) != 0) {
			tftp_server_destroy(g_server);
			g_server = NULL;
			csh_printf(csh, "tftp server thread failed\n");
			return -1;
		}
		return 0;
	}

	case 'r':
	case 'w': {
		struct tftp_client *client;
		int bytes;

		if (ip == NULL || path[0] == NULL) {
			tftp_session_help(csh);
			return -1;
		}
		client = tftp_client_create(ip, port);
		if (client == NULL) {
			csh_printf(csh, "tftp client create failed\n");
			return -1;
		}
		if (mode == 'r') {
			bytes = tftp_client_pull(client, path[0],
						 path[1] ? path[1] : path[0]);
		} else {
			bytes = tftp_client_push(client, path[0],
						 path[1] ? path[1] : path[0]);
		}
		tftp_client_destroy(client);
		if (bytes < 0) {
			csh_printf(csh, "tftp transfer failed (%d)\n", bytes);
			return -1;
		}
		csh_printf(csh, "file size: %d bytes\n", bytes);
		if (last_slot != NULL) {
			tftp_slot_print(csh, last_slot);
		}
		return 0;
	}

	default:
		tftp_session_help(csh);
		return -1;
	}
}
CSH_CMD_EXPORT_ALIAS_FULL(cmd_tftp, tftp, "tftp [-r|-w|-s|-l|--stop] ...",
			  "tftp client/server over RAM slots");
