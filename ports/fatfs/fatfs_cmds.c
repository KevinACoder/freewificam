/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    fatfs_cmds.c
 * @brief   The `stor` console command family - the operational view the storage
 *          acceptance runs are written against.
 *
 *   stor info              mounted volume, filesystem type, sector size, free/total
 *   stor mount             bind the device and mount volume 0
 *   stor umount            unmount volume 0
 *   stor format            format volume 0 as exFAT (destructive)
 *   stor ls                directory listing of the volume root
 *   stor wr <bytes>        write a deterministic pattern, read it back, verify
 *   stor rd <bytes>        read the file and print size + checksum
 *   stor bench <bytes>     sequential write then read, reporting bytes and ms
 *
 * This unit is compiled into the PCIe/storage image only; it is not part of any
 * other image and is not added to the shared board command table.
 *
 * The volume is fixed at 0 (the NVMe controller the seam registers as device 0).
 * The command family is a console face over ports/fatfs/fs.h; it holds no storage
 * state of its own.
 */

#include "os/cmsis2/cmsis2.h"

#include <stdint.h>
#include <string.h>

#include "csh.h"

#include "ff.h"
#include "fs.h"

/* The one volume this image's storage stack exposes. */
#define STOR_VOLUME	0u

/* The file the write/read/bench commands act on, under the volume root. */
#define STOR_FILE	"0:/stor.bin"

/* One shared I/O buffer. 64 KiB keeps a request inside a single driver chunk for
 * the common device geometries. */
#define STOR_BUF_SIZE	(64u * 1024u)
static uint8_t stor_buf[STOR_BUF_SIZE] __attribute__((aligned(64)));

static void stor_usage(chry_shell_t *csh)
{
	csh_printf(csh,
		   "usage: stor <info|mount|umount|format|ls|wr|rd|bench> [bytes]\r\n");
	csh_printf(csh, "       stor info           volume, type, sector size, space\r\n");
	csh_printf(csh, "       stor mount|umount   volume %u\r\n", (unsigned)STOR_VOLUME);
	csh_printf(csh, "       stor format         exFAT, 4096-byte sector (destructive)\r\n");
	csh_printf(csh, "       stor ls             root directory listing\r\n");
	csh_printf(csh, "       stor wr <bytes>     write, read back, verify\r\n");
	csh_printf(csh, "       stor rd <bytes>     read + checksum\r\n");
	csh_printf(csh, "       stor bench <bytes>  sequential write then read, timed\r\n");
}

/* Decimal only, and small: the byte counts are file sizes a person types. */
static int parse_u32(const char *arg, uint32_t *value)
{
	uint32_t v = 0;

	if (arg == NULL || arg[0] == '\0') {
		return -1;
	}

	while (*arg != '\0') {
		if (*arg < '0' || *arg > '9') {
			return -1;
		}
		v = v * 10u + (uint32_t)(*arg - '0');
		arg++;
	}
	*value = v;

	return 0;
}

static uint32_t fnv1a(const uint8_t *data, uint32_t len, uint32_t hash)
{
	uint32_t i;

	for (i = 0; i < len; i++) {
		hash ^= data[i];
		hash *= 16777619u;
	}

	return hash;
}

/* The deterministic byte pattern the write/verify command uses: a plain hash of
 * the offset, so the reader can regenerate it without storing it anywhere. */
static uint8_t pattern_byte(uint32_t index)
{
	uint32_t seed = index * 2654435761u + 12345u;

	return (uint8_t)(seed >> 16);
}

/* --- info ----------------------------------------------------------------- */

static void cmd_info(chry_shell_t *csh)
{
	uint64_t total = 0;
	uint64_t freeb = 0;
	const char *type;

	csh_printf(csh, "stor: volume %u, device %s, sector %lu\r\n",
		   (unsigned)STOR_VOLUME, "nvme0",
		   (unsigned long)fs_sector_size());

	if (!fs_is_mounted(STOR_VOLUME)) {
		csh_printf(csh, "stor: not mounted\r\n");
		return;
	}

	type = fs_type_name(STOR_VOLUME);
	if (fs_usage(STOR_VOLUME, &total, &freeb) == 0) {
		csh_printf(csh,
			   "stor: mounted, %s, %llu MB total, %llu MB free\r\n",
			   (type != NULL) ? type : "?",
			   (unsigned long long)(total >> 20),
			   (unsigned long long)(freeb >> 20));
	} else {
		csh_printf(csh, "stor: mounted, %s (usage unavailable)\r\n",
			   (type != NULL) ? type : "?");
	}
}

/* --- ls ------------------------------------------------------------------- */

static void cmd_ls(chry_shell_t *csh)
{
	DIR dj;
	FILINFO info;
	FRESULT fr;
	uint32_t entries = 0;

	fr = f_opendir(&dj, "0:/");
	if (fr != FR_OK) {
		csh_printf(csh, "stor: cannot open root (FR_%d)\r\n", (int)fr);
		return;
	}

	for (;;) {
		fr = f_readdir(&dj, &info);
		if (fr != FR_OK || info.fname[0] == '\0') {
			break;
		}
		if (info.fattrib & AM_DIR) {
			csh_printf(csh, "  <dir>          %s\r\n", info.fname);
		} else {
			csh_printf(csh, "  %10llu     %s\r\n",
				   (unsigned long long)info.fsize, info.fname);
		}
		entries++;
	}
	(void)f_closedir(&dj);

	csh_printf(csh, "stor: %u entr%s, FR_%d\r\n", (unsigned)entries,
		   (entries == 1) ? "y" : "ies", (int)fr);
}

/* --- wr (write, read back, verify) ---------------------------------------- */

static void cmd_wr(chry_shell_t *csh, uint32_t bytes)
{
	FIL fp;
	FRESULT fr;
	uint32_t done = 0;
	uint32_t start_ms;
	uint32_t elapsed_ms;
	int mismatch = -1;

	fr = f_open(&fp, STOR_FILE, FA_CREATE_ALWAYS | FA_WRITE);
	if (fr != FR_OK) {
		csh_printf(csh, "stor: cannot create %s (FR_%d)\r\n", STOR_FILE,
			   (int)fr);
		return;
	}

	start_ms = osKernelGetTickCount();
	while (done < bytes) {
		uint32_t chunk = bytes - done;
		UINT wrote = 0;
		uint32_t i;

		if (chunk > STOR_BUF_SIZE) {
			chunk = STOR_BUF_SIZE;
		}
		for (i = 0; i < chunk; i++) {
			stor_buf[i] = pattern_byte(done + i);
		}

		fr = f_write(&fp, stor_buf, chunk, &wrote);
		if (fr != FR_OK || wrote != chunk) {
			break;
		}
		done += chunk;
	}
	fr = f_close(&fp);	/* close flushes; a failure here is a real error */
	if (fr != FR_OK || done != bytes) {
		csh_printf(csh, "stor: write failed after %lu bytes (FR_%d)\r\n",
			   (unsigned long)done, (int)fr);
		(void)f_unlink(STOR_FILE);
		return;
	}
	elapsed_ms = (osKernelGetTickCount() - start_ms) + 1u;

	/* Read it back through the path a reader would take and compare against
	 * the pattern - a write that "succeeded" but landed elsewhere passes a
	 * status check and fails this. */
	fr = f_open(&fp, STOR_FILE, FA_READ);
	if (fr != FR_OK) {
		csh_printf(csh, "stor: reopen failed (FR_%d)\r\n", (int)fr);
		return;
	}

	done = 0;
	fr = FR_OK;
	while (done < bytes) {
		uint32_t chunk = bytes - done;
		UINT got = 0;
		uint32_t i;

		if (chunk > STOR_BUF_SIZE) {
			chunk = STOR_BUF_SIZE;
		}
		fr = f_read(&fp, stor_buf, chunk, &got);
		if (fr != FR_OK || got != chunk) {
			break;
		}
		for (i = 0; i < chunk; i++) {
			if (stor_buf[i] != pattern_byte(done + i)) {
				mismatch = (int)(done + i);
				break;
			}
		}
		if (mismatch >= 0) {
			break;
		}
		done += chunk;
	}
	(void)f_close(&fp);

	if (fr != FR_OK) {
		csh_printf(csh, "stor: read-back failed at %lu bytes (FR_%d)\r\n",
			   (unsigned long)done, (int)fr);
	} else if (mismatch >= 0) {
		csh_printf(csh, "stor: MISMATCH at byte %d\r\n", mismatch);
	} else if (done != bytes) {
		csh_printf(csh, "stor: short read-back, %lu of %lu bytes\r\n",
			   (unsigned long)done, (unsigned long)bytes);
	} else {
		csh_printf(csh,
			   "stor: %lu bytes written and verified, %lu ms (%lu KiB/s)\r\n",
			   (unsigned long)bytes, (unsigned long)elapsed_ms,
			   (unsigned long)((bytes / 1024u) * 1000u / elapsed_ms));
	}
}

/* --- rd (read + checksum) -------------------------------------------------- */

static void cmd_rd(chry_shell_t *csh, uint32_t bytes)
{
	FIL fp;
	FRESULT fr;
	uint32_t total = 0;
	uint32_t hash = 2166136261u;
	uint32_t start_ms;
	uint32_t elapsed_ms;

	fr = f_open(&fp, STOR_FILE, FA_READ);
	if (fr != FR_OK) {
		csh_printf(csh, "stor: cannot open %s (FR_%d)\r\n", STOR_FILE,
			   (int)fr);
		return;
	}

	start_ms = osKernelGetTickCount();
	while (total < bytes) {
		uint32_t chunk = bytes - total;
		UINT got = 0;

		if (chunk > STOR_BUF_SIZE) {
			chunk = STOR_BUF_SIZE;
		}
		fr = f_read(&fp, stor_buf, chunk, &got);
		if (fr != FR_OK || got == 0) {
			break;
		}
		hash = fnv1a(stor_buf, got, hash);
		total += got;
	}
	elapsed_ms = (osKernelGetTickCount() - start_ms) + 1u;
	(void)f_close(&fp);

	csh_printf(csh,
		   "stor: %s %lu bytes, fnv1a %08lx, %lu ms (%lu KiB/s), FR_%d\r\n",
		   STOR_FILE, (unsigned long)total, (unsigned long)hash,
		   (unsigned long)elapsed_ms,
		   (unsigned long)((total / 1024u) * 1000u / elapsed_ms),
		   (int)fr);
}

/* --- bench (sequential write then read, timed) ----------------------------- */

static void cmd_bench(chry_shell_t *csh, uint32_t bytes)
{
	FIL fp;
	FRESULT fr;
	uint32_t done;
	uint32_t start_ms;
	uint32_t write_ms;
	uint32_t read_ms;

	fr = f_open(&fp, STOR_FILE, FA_CREATE_ALWAYS | FA_WRITE);
	if (fr != FR_OK) {
		csh_printf(csh, "stor: cannot create %s (FR_%d)\r\n", STOR_FILE,
			   (int)fr);
		return;
	}

	start_ms = osKernelGetTickCount();
	for (done = 0; done < bytes; ) {
		uint32_t chunk = bytes - done;
		UINT wrote = 0;
		uint32_t i;

		if (chunk > STOR_BUF_SIZE) {
			chunk = STOR_BUF_SIZE;
		}
		for (i = 0; i < chunk; i++) {
			stor_buf[i] = pattern_byte(done + i);
		}
		fr = f_write(&fp, stor_buf, chunk, &wrote);
		if (fr != FR_OK || wrote != chunk) {
			break;
		}
		done += chunk;
	}
	fr = f_close(&fp);
	write_ms = (osKernelGetTickCount() - start_ms) + 1u;
	if (fr != FR_OK || done != bytes) {
		csh_printf(csh, "stor: bench write failed after %lu bytes (FR_%d)\r\n",
			   (unsigned long)done, (int)fr);
		return;
	}

	fr = f_open(&fp, STOR_FILE, FA_READ);
	if (fr != FR_OK) {
		csh_printf(csh, "stor: bench reopen failed (FR_%d)\r\n", (int)fr);
		return;
	}
	start_ms = osKernelGetTickCount();
	for (done = 0; done < bytes; ) {
		uint32_t chunk = bytes - done;
		UINT got = 0;

		if (chunk > STOR_BUF_SIZE) {
			chunk = STOR_BUF_SIZE;
		}
		fr = f_read(&fp, stor_buf, chunk, &got);
		if (fr != FR_OK || got != chunk) {
			break;
		}
		done += chunk;
	}
	(void)f_close(&fp);
	read_ms = (osKernelGetTickCount() - start_ms) + 1u;

	if (fr != FR_OK || done != bytes) {
		csh_printf(csh, "stor: bench read failed after %lu bytes (FR_%d)\r\n",
			   (unsigned long)done, (int)fr);
		return;
	}

	csh_printf(csh, "stor: bench %lu bytes: write %lu ms (%lu KiB/s), "
		   "read %lu ms (%lu KiB/s)\r\n",
		   (unsigned long)bytes, (unsigned long)write_ms,
		   (unsigned long)((bytes / 1024u) * 1000u / write_ms),
		   (unsigned long)read_ms,
		   (unsigned long)((bytes / 1024u) * 1000u / read_ms));
}

/* --- entry ---------------------------------------------------------------- */

static int cmd_stor(int argc, char **argv)
{
	chry_shell_t *csh = (void *)argv[argc + 1];
	const char *op;

	if (argc < 2) {
		stor_usage(csh);
		return 0;
	}
	op = argv[1];

	if (strcmp(op, "info") == 0) {
		cmd_info(csh);
		return 0;
	}

	if (strcmp(op, "mount") == 0) {
		int fr;

		(void)fs_init();
		fr = fs_mount(STOR_VOLUME);
		csh_printf(csh, "stor: mount volume %u: %s (%d)\r\n",
			   (unsigned)STOR_VOLUME, (fr == 0) ? "ok" : "failed", fr);
		return (fr == 0) ? 0 : -1;
	}

	if (strcmp(op, "umount") == 0) {
		int fr = fs_unmount(STOR_VOLUME);

		csh_printf(csh, "stor: umount volume %u: %s (%d)\r\n",
			   (unsigned)STOR_VOLUME, (fr == 0) ? "ok" : "failed", fr);
		return (fr == 0) ? 0 : -1;
	}

	if (strcmp(op, "format") == 0) {
		int fr;

		(void)fs_init();
		fr = fs_format_exfat(STOR_VOLUME);
		csh_printf(csh, "stor: format volume %u as exFAT: %s (%d)\r\n",
			   (unsigned)STOR_VOLUME, (fr == 0) ? "ok" : "failed", fr);
		return (fr == 0) ? 0 : -1;
	}

	if (strcmp(op, "ls") == 0) {
		cmd_ls(csh);
		return 0;
	}

	if (strcmp(op, "wr") == 0 || strcmp(op, "rd") == 0 ||
	    strcmp(op, "bench") == 0) {
		uint32_t bytes;

		if (argc < 3 || parse_u32(argv[2], &bytes) != 0 || bytes == 0) {
			csh_printf(csh, "stor: %s needs a positive byte count\r\n",
				   op);
			return -1;
		}
		if (op[0] == 'w') {
			cmd_wr(csh, bytes);
		} else if (op[0] == 'r') {
			cmd_rd(csh, bytes);
		} else {
			cmd_bench(csh, bytes);
		}
		return 0;
	}

	stor_usage(csh);
	return -1;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_stor, stor,
			  "stor <info|mount|umount|format|ls|wr|rd|bench> [bytes]",
			  "storage: mount/format/list and the write/read/bench tests\n")
