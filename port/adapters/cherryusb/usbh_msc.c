/*
 * @file
 * @brief cherryusb class/msc adapter: instance registry + acceptance shell
 *        commands on top of the upstream BOT/SCSI API.
 *
 *        The BOT machinery (CBW/CSW, sync URBs, TUR/INQUIRY/CAPACITY,
 *        READ10/WRITE10) lives entirely in the vendored
 *        third-party/cherryusb/class/msc/usbh_msc.c - the mature,
 *        battle-tested implementation is used as-is (user ruling
 *        2026-10-01: adapters glue, they never re-implement the library).
 *        This TU is glue only:
 *          - usbh_msc_run/stop  : the library's weak hooks give the
 *                                 instance registry (claim does scsi_init)
 *          - `usbh_msc` command : the acceptance toolbox (P5-d criteria:
 *                                 pattern write/read verify, bw, soak)
 *
 *        One shared 64 KiB-aligned 128 KiB buffer serves all data stages:
 *        the xHCI port requires every byte it sees to be 64B aligned, and
 *        128 KiB per command amortizes the BOT turnaround (same stick, same
 *        hub port: linux dd = 30.7 MB/s read / 24.5 MB/s write with 120 KiB
 *        commands - runs/20261001-xhci-throughput/).  Commands serialize on
 *        one mutex (BOT allows a single command per LUN).
 *
 * Commands:
 *   usbh_msc [idx] list                      instances + capacity identity
 *   usbh_msc [idx] read <lba> <n>            read n blocks (chunked); verifies
 *                                            against the matching last write and
 *                                            hexdumps the first 32 bytes
 *   usbh_msc [idx] write <lba> <n> <p>       fill n blocks with pattern byte <p>
 *   usbh_msc [idx] bw [<MiB> [w]]            sequential bandwidth (default 32;
 *                                            w = write to the high-LBA zone)
 *   usbh_msc soak <sec> [<idx>]              write/read/verify loop - the 600s
 *                                            acceptance long-test, progress line
 *                                            every 30 s, summary at the end
 *
 * @date   01.10.2026
 * @author zhugengyu
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "cmsis_os2.h"

#include "usbh_core.h"
#include "usbh_hub.h"
#include "usbh_msc.h"
#include "usb_config.h"

#include "cherrysh_adapter.h"
#include "csh.h"

/* Data chunk: 64 KiB aligned so the 128 KiB buffer splits into two 64 KiB
 * chained TRBs that each stay inside one 64 KiB boundary (port contract). */
#define MSC_ADAPTER_CHUNK_BYTES		131072U
#define MSC_ADAPTER_CHUNK_ALIGN		65536U
#define MSC_ADAPTER_MAX			CONFIG_USBHOST_MAX_MSC_CLASS

static uint8_t g_msc_data[MSC_ADAPTER_CHUNK_BYTES]
	__attribute__((aligned(MSC_ADAPTER_CHUNK_ALIGN)));

static struct usbh_msc *g_msc[MSC_ADAPTER_MAX];
static usb_osal_mutex_t g_msc_lock;

/* last write per instance, for `read`'s verify pass */
static uint32_t g_w_lba[MSC_ADAPTER_MAX];
static uint32_t g_w_blocks[MSC_ADAPTER_MAX];
static uint8_t g_w_pattern[MSC_ADAPTER_MAX];

/* --- library weak hooks: instance registry --------------------------------- */

void usbh_msc_run(struct usbh_msc *msc_class)
{
	uint32_t idx = (uint32_t)(msc_class->sdchar - 'a');
	int ret;

	if (idx >= MSC_ADAPTER_MAX) {
		return;
	}
	if (g_msc_lock == NULL) {
		g_msc_lock = usb_osal_mutex_create();
	}

	/* the identity chain the library provides: ready -> inquiry -> capacity */
	ret = usbh_msc_scsi_init(msc_class);
	if (ret < 0) {
		board_log("usbh_msc: bus%u addr%u init fail (%d)",
			  msc_class->hport->bus->busid,
			  msc_class->hport->dev_addr, ret);
		return;
	}
	g_msc[idx] = msc_class;
	g_w_blocks[idx] = 0U;
	board_log("usbh_msc: bus%u addr%u blocks=%u bsize=%u (%u MiB)",
		  msc_class->hport->bus->busid, msc_class->hport->dev_addr,
		  msc_class->blocknum, msc_class->blocksize,
		  (uint32_t)(((uint64_t)msc_class->blocknum *
		    msc_class->blocksize) >> 20));
}

void usbh_msc_stop(struct usbh_msc *msc_class)
{
	uint32_t idx = (uint32_t)(msc_class->sdchar - 'a');

	if (idx < MSC_ADAPTER_MAX) {
		g_msc[idx] = NULL;
	}
}

/* --- acceptance toolbox ----------------------------------------------------- */

static struct usbh_msc *msc_lookup(uint32_t idx)
{
	if (idx >= MSC_ADAPTER_MAX) {
		return NULL;
	}
	return g_msc[idx];
}

static void msc_lock_take(void)
{
	if (g_msc_lock == NULL) {
		g_msc_lock = usb_osal_mutex_create();
	}
	if (g_msc_lock != NULL) {
		usb_osal_mutex_take(g_msc_lock);
	}
}

static void msc_lock_give(void)
{
	if (g_msc_lock != NULL) {
		usb_osal_mutex_give(g_msc_lock);
	}
}

static int msc_check_range(chry_shell_t *csh, struct usbh_msc *msc,
			   uint32_t lba, uint32_t blocks)
{
	if ((lba > msc->blocknum) || (blocks > (msc->blocknum - lba))) {
		csh_printf(csh, "msc: range %u+%u out of %u blocks\n",
			   lba, blocks, msc->blocknum);
		return -1;
	}
	if (msc->blocksize == 0U) {
		return -1;
	}
	return 0;
}

/* Fill/verify one chunk; `fill` selects write-vs-check. Returns 0 or -1. */
static int msc_chunk_op(chry_shell_t *csh, struct usbh_msc *msc,
			uint32_t lba, uint32_t blocks, uint8_t pattern,
			uint8_t fill)
{
	uint32_t off;
	int ret;

	if (fill != 0U) {
		memset(g_msc_data, pattern, blocks * msc->blocksize);
		ret = usbh_msc_scsi_write10(msc, lba, g_msc_data, blocks);
		if (ret != 0) {
			csh_printf(csh, "msc: write %u@%u fail (%d)\n",
				   blocks, lba, ret);
			return -1;
		}
		return 0;
	}
	ret = usbh_msc_scsi_read10(msc, lba, g_msc_data, blocks);
	if (ret != 0) {
		csh_printf(csh, "msc: read %u@%u fail (%d)\n",
			   blocks, lba, ret);
		return -1;
	}
	for (off = 0U; off < (blocks * msc->blocksize); off++) {
		if (g_msc_data[off] != pattern) {
			csh_printf(csh, "msc: verify MISMATCH at "
				   "byte %u (got %02x want %02x)\n",
				   off, g_msc_data[off], pattern);
			return -1;
		}
	}
	return 0;
}

/* Split a block range into chunks and run op on each. */
static int msc_range_op(chry_shell_t *csh, struct usbh_msc *msc,
			uint32_t lba, uint32_t blocks, uint8_t pattern,
			uint8_t fill)
{
	uint32_t chunk_blocks = MSC_ADAPTER_CHUNK_BYTES / msc->blocksize;
	uint32_t done = 0U;

	while (done < blocks) {
		uint32_t step = blocks - done;

		if (step > chunk_blocks) {
			step = chunk_blocks;
		}
		if (msc_chunk_op(csh, msc, lba + done, step, pattern,
				 fill) != 0) {
			return -1;
		}
		done += step;
	}
	return 0;
}

static void msc_print_head(chry_shell_t *csh)
{
	uint32_t i;

	csh_printf(csh, "msc: data[0..31]");
	for (i = 0U; i < 32U; i++) {
		csh_printf(csh, "%c%02x",
			   ((i % 8U) == 0U) ? ' ' : '-', g_msc_data[i]);
	}
	csh_printf(csh, "\n");
}

static void msc_print_list(chry_shell_t *csh)
{
	uint32_t i;

	for (i = 0U; i < MSC_ADAPTER_MAX; i++) {
		if (g_msc[i] != NULL) {
			csh_printf(csh, "msc%u: bus%u addr%u blocks=%u "
				   "bsize=%u\n",
				   i, g_msc[i]->hport->bus->busid,
				   g_msc[i]->hport->dev_addr,
				   g_msc[i]->blocknum, g_msc[i]->blocksize);
		}
	}
}

static int cmd_usbh_msc(int argc, char **argv)
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	uint32_t idx = 0U;
	char **arg = &argv[1];
	int left = argc - 1;
	struct usbh_msc *msc;

	if ((left >= 1) && (*arg[0] >= '0') && (*arg[0] <= '9')) {
		idx = (uint32_t)atoi(arg[0]);
		arg++;
		left--;
	}

	if ((left == 0) || ((left == 1) && (strcmp(arg[0], "list") == 0))) {
		msc_print_list(csh);
		return 0;
	}

	msc = msc_lookup(idx);
	if (msc == NULL) {
		csh_printf(csh, "msc%u: not claimed (see `usbh_msc list`)\n",
			   idx);
		return 1;
	}

	if (strcmp(arg[0], "read") == 0) {
		uint32_t lba, blocks;
		int mismatch;

		if (left != 3) {
			goto usage;
		}
		lba = (uint32_t)strtol(arg[1], NULL, 0);
		blocks = (uint32_t)strtol(arg[2], NULL, 0);
		if ((blocks == 0U) || msc_check_range(csh, msc, lba, blocks)) {
			return 1;
		}
		msc_lock_take();
		mismatch = msc_range_op(csh, msc, lba, blocks,
					g_w_pattern[idx], 0);
		msc_print_head(csh);
		msc_lock_give();
		if (mismatch == 0) {
			if ((g_w_blocks[idx] == blocks) &&
			    (g_w_lba[idx] == lba)) {
				csh_printf(csh, "msc: read %u@%u verified "
					   "against write pattern %02x OK\n",
					   blocks, lba, g_w_pattern[idx]);
			} else {
				csh_printf(csh, "msc: read %u@%u OK (no "
					   "matching write recorded)\n",
					   blocks, lba);
			}
			return 0;
		}
		return 1;
	}

	if (strcmp(arg[0], "write") == 0) {
		uint32_t lba, blocks, pattern;

		if (left != 4) {
			goto usage;
		}
		lba = (uint32_t)strtol(arg[1], NULL, 0);
		blocks = (uint32_t)strtol(arg[2], NULL, 0);
		pattern = (uint32_t)strtol(arg[3], NULL, 0) & 0xffU;
		if ((blocks == 0U) || msc_check_range(csh, msc, lba, blocks)) {
			return 1;
		}
		msc_lock_take();
		if (msc_range_op(csh, msc, lba, blocks, (uint8_t)pattern,
				 1) != 0) {
			msc_lock_give();
			return 1;
		}
		g_w_lba[idx] = lba;
		g_w_blocks[idx] = blocks;
		g_w_pattern[idx] = (uint8_t)pattern;
		msc_lock_give();
		csh_printf(csh, "msc: wrote %u blocks @%u pattern %02x\n",
			   blocks, lba, pattern);
		return 0;
	}

	if (strcmp(arg[0], "bw") == 0) {
		uint32_t mib = 32U;
		uint32_t mib_done = 0U, i;
		uint32_t t0, elapsed_ms;
		uint32_t blocks_per_chunk;
		uint8_t write = 0U;
		uint32_t lba_base = 0U;

		if (left == 1) {
			mib = 32U;
		} else {
			mib = (uint32_t)strtol(arg[1], NULL, 0);
		}
		if (left == 3) {
			if ((arg[2][0] != 'w') || (arg[2][1] != '\0')) {
				goto usage;
			}
			write = 1U;
		} else if (left > 3) {
			goto usage;
		}
		if (msc_check_range(csh, msc, 0U, 2U) != 0) {
			return 1;
		}
		blocks_per_chunk = MSC_ADAPTER_CHUNK_BYTES / msc->blocksize;
		if (write != 0U) {
			/* high-LBA sacrifice zone, one guard MiB below the end */
			uint32_t span = mib * 2048U;

			if ((span + 2048U) > msc->blocknum) {
				csh_printf(csh, "msc: %u MiB does not fit\n", mib);
				return 1;
			}
			lba_base = msc->blocknum - 2048U - span;
			memset(g_msc_data, 0x5AU, MSC_ADAPTER_CHUNK_BYTES);
		}
		msc_lock_take();
		t0 = osKernelGetTickCount();
		while (mib_done < mib) {
			/* sequential from lba_base */
			uint32_t chunks = (1024U * 1024U) /
					  (blocks_per_chunk *
					   msc->blocksize);

			for (i = 0U; i < chunks; i++) {
				int ret;

				if (write != 0U) {
					ret = usbh_msc_scsi_write10(
						msc, lba_base + i * blocks_per_chunk,
						g_msc_data, blocks_per_chunk);
				} else {
					ret = usbh_msc_scsi_read10(
						msc, lba_base + i * blocks_per_chunk,
						g_msc_data, blocks_per_chunk);
				}
				if (ret != 0) {
					msc_lock_give();
					csh_printf(csh, "msc: bw %s fail (%d)\n",
						   write ? "write" : "read", ret);
					return 1;
				}
			}
			mib_done++;
		}
		elapsed_ms = osKernelGetTickCount() - t0;
		msc_lock_give();
		if (elapsed_ms == 0U) {
			elapsed_ms = 1U;
		}
		csh_printf(csh, "msc: %s %u MiB in %u ms = %u.%u MB/s\n",
			   write ? "wrote" : "read", mib_done, elapsed_ms,
			   (uint32_t)(((uint64_t)mib_done * 1048576U) /
				      (elapsed_ms * 1000U)),
			   (uint32_t)((((uint64_t)mib_done * 1048576U) * 10U /
					(elapsed_ms * 1000U)) % 10U));
		return 0;
	}

	if (strcmp(arg[0], "soak") == 0) {
		uint32_t seconds, idx2 = idx;
		uint32_t t0, last_report;
		uint32_t cycles = 0U, errs = 0U;
		static const uint8_t pat[4] = { 0xA5U, 0x5AU, 0x00U, 0xFFU };
		uint32_t blocks_per_chunk;

		if ((left != 2) && (left != 3)) {
			goto usage;
		}
		seconds = (uint32_t)strtol(arg[1], NULL, 0);
		if (left == 3) {
			idx2 = (uint32_t)atoi(arg[2]);
			msc = msc_lookup(idx2);
			if (msc == NULL) {
				csh_printf(csh, "msc%u: not claimed\n", idx2);
				return 1;
			}
		}
		blocks_per_chunk = MSC_ADAPTER_CHUNK_BYTES / msc->blocksize;
		if (msc_check_range(csh, msc, 0U, blocks_per_chunk) != 0) {
			return 1;
		}

		csh_printf(csh, "msc: soak %us on msc%u (%u-block chunks)\n",
			   seconds, idx2, blocks_per_chunk);
		msc_lock_take();
		t0 = osKernelGetTickCount();
		last_report = t0;
		while ((osKernelGetTickCount() - t0) < (seconds * 1000U)) {
			uint8_t pattern = pat[cycles & 3U];

			if (msc_chunk_op(csh, msc, 0U, blocks_per_chunk,
					 pattern, 1) != 0) {
				errs++;
				continue;
			}
			if (msc_chunk_op(csh, msc, 0U, blocks_per_chunk,
					 pattern, 0) != 0) {
				errs++;
				continue;
			}
			cycles++;
			if ((osKernelGetTickCount() - last_report) >=
			    30000U) {
				csh_printf(csh, "msc: soak %us %u cycles "
					   "%u MiB moved %u errs\n",
					   (osKernelGetTickCount() - t0) /
					   1000U, cycles,
					   (uint32_t)(((uint64_t)cycles * 2U *
					    MSC_ADAPTER_CHUNK_BYTES) >> 20), errs);
				last_report = osKernelGetTickCount();
			}
		}
		msc_lock_give();
		csh_printf(csh, "msc: soak done %us cycles=%u bytes=%u MiB "
			   "errs=%u\n",
			   (osKernelGetTickCount() - t0) / 1000U, cycles,
			   (uint32_t)(((uint64_t)cycles * 2U *
			    MSC_ADAPTER_CHUNK_BYTES) >> 20), errs);
		return (errs == 0U) ? 0 : 1;
	}

usage:
	csh_printf(csh, "usage: usbh_msc [idx] "
			   "[list|read <lba> <n>|write <lba> <n> <pat>|"
			   "bw [<MiB> [w]]|soak <sec> [idx]]\n");
	return 1;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_usbh_msc, usbh_msc,
			  "usbh_msc [idx] list|read|write|bw|soak",
			  "upstream class/msc adapter + acceptance set (S5 P5-e)");
