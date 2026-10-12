/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    main.c
 * @brief   The storage stack's host contract suite: the real FatFs, the real
 *          ports/fatfs adapter, a scripted NVMe controller underneath.
 *
 * What is under test is not a card or a controller - that is the board's
 * question - but the contract the storage stack promises: an exFAT volume on a
 * 4096-byte sector round-trips byte-exactly, a 4096-byte filesystem sector maps
 * onto eight 512-byte device blocks, and a device that misbehaves (never
 * completes, drops a doorbell, delivers a late interrupt, suppresses an
 * interrupt) produces a bounded error and an exact completion count rather than
 * a hang or a silent corruption.
 *
 * Usage:
 *   pcie-storage-test                        the whole battery
 *   pcie-storage-test --tamper <case-id>     red control: one named case's
 *                                            expectation is inverted, so the run
 *                                            must fail (the proof a case can go
 *                                            red rather than merely print ok=1)
 *   pcie-storage-test --fault lying-device   red control: the scripted device
 *                                            acknowledges commands but returns
 *                                            inverted bytes, so the round trip
 *                                            must fail
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "os/cmsis2/cmsis2.h"

#include "ff.h"
#include "diskio.h"

#include "blkdev.h"
#include "fs.h"
#include "scripted_nvme.h"

/* The compile-time half of the round-trip contract: the shadow config header
 * must match the vendored FatFs revision, and the window must be the 4096-byte
 * sector this build was designed for. A mismatch here is a build error, not a
 * runtime one. */
_Static_assert(FFCONF_DEF == FF_DEFINED,
	       "ffconf.h revision must match the vendored FatFs");
_Static_assert(FF_MAX_SS == 4096 && FF_MIN_SS == 4096,
	       "the suite asserts the fixed 4096-byte filesystem sector");

#define FS_SECTOR	4096u
#define FILE_BYTES	(256u * 1024u)
#define BIG_BUF		(3u * 4096u)

static int failures;
static const char *tamper;
static int lying;

static void check(const char *id, int ok, const char *fmt, ...)
{
	va_list ap;

	/* The red control: a named case's verdict is inverted, so the battery
	 * must fail - it demonstrates the case is not a rubber stamp. */
	if (tamper != NULL && strcmp(tamper, id) == 0) {
		ok = !ok;
	}

	printf("storage-test: %s ok=%d ", id, ok ? 1 : 0);
	va_start(ap, fmt);
	(void)vprintf(fmt, ap);
	va_end(ap);
	printf("\n");

	if (!ok) {
		failures++;
	}
}

/* Start a case with a fresh device (counters and media cleared) and the device
 * marked ready, so the direct diskio cases do not depend on an earlier case's
 * mount state. */
static void begin(void)
{
	scripted_nvme_reset();
	if (lying) {
		scripted_nvme_set_fault(SCRIPTED_FAULT_LIE);
	}
	(void)disk_initialize(0);
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

static uint8_t pattern_byte(uint32_t index)
{
	uint32_t seed = index * 2654435761u + 12345u;

	return (uint8_t)(seed >> 16);
}

/* --- cases ---------------------------------------------------------------- */

static void case_config(void)
{
	/* No device needed: the compile-time contract and the window size. */
	int ok = (FFCONF_DEF == FF_DEFINED) &&
		 (FF_MAX_SS == FF_MIN_SS) &&
		 (FF_MAX_SS == 4096) &&
		 (fs_sector_size() == 4096);

	check("config", ok, "FFCONF_DEF=%d FF_DEFINED=%d FF_MAX_SS=%d sector=%lu",
	      (int)FFCONF_DEF, (int)FF_DEFINED, (int)FF_MAX_SS,
	      (unsigned long)fs_sector_size());
}

static void case_bind(void)
{
	int ok;

	begin();
	ok = (fs_init() == 0) &&
	     (blkdev_count() == 1) &&
	     (blkdev_name(0) != NULL) &&
	     (strcmp(blkdev_name(0), "nvme0") == 0);

	check("bind", ok, "count=%lu name=%s", (unsigned long)blkdev_count(),
	      (blkdev_name(0) != NULL) ? blkdev_name(0) : "(none)");
}

static void case_sector_map(void)
{
	static uint8_t buf[BIG_BUF];
	DRESULT a;
	DRESULT b;
	uint64_t lba_a;
	uint32_t blk_a;
	uint64_t lba_b;
	uint32_t blk_b;

	begin();

	/* One 4096-byte FatFs sector must reach the device as eight 512-byte
	 * blocks: sector 1 -> lba 8. */
	a = disk_read(0, buf, 1, 1);
	lba_a = scripted_nvme_last_lba();
	blk_a = scripted_nvme_last_blocks();

	/* Three sectors starting at 2 -> lba 16, 24 blocks. */
	b = disk_read(0, buf, 2, 3);
	lba_b = scripted_nvme_last_lba();
	blk_b = scripted_nvme_last_blocks();

	check("sector-map",
	      (a == RES_OK) && (b == RES_OK) &&
		      (lba_a == 8u) && (blk_a == 8u) &&
		      (lba_b == 16u) && (blk_b == 24u),
	      "s1(lba=%llu blocks=%lu) s2(lba=%llu blocks=%lu)",
	      (unsigned long long)lba_a, (unsigned long)blk_a,
	      (unsigned long long)lba_b, (unsigned long)blk_b);
}

static void case_format_mount(void)
{
	int ok = 1;
	const char *detail = "ok";

	begin();

	if (fs_format_exfat(0) != 0) {
		ok = 0;
		detail = "format";
	}
	if (ok && fs_mount(0) != 0) {
		ok = 0;
		detail = "mount";
	}
	if (ok && !fs_is_mounted(0)) {
		ok = 0;
		detail = "not-mounted";
	}
	if (ok) {
		const char *type = fs_type_name(0);

		if (type == NULL || strcmp(type, "exFAT") != 0) {
			ok = 0;
			detail = "type";
		}
	}

	check("format-mount", ok, "detail=%s type=%s", detail,
	      fs_is_mounted(0) ? fs_type_name(0) : "(none)");

	(void)fs_unmount(0);
}

static void case_roundtrip(void)
{
	static uint8_t wbuf[FILE_BYTES];
	static uint8_t rbuf[FILE_BYTES];
	FIL fp;
	FRESULT fr;
	UINT bw = 0;
	UINT br = 0;
	uint32_t i;
	int ok = 1;
	const char *detail = "ok";

	begin();

	if (fs_format_exfat(0) != 0) {
		ok = 0;
		detail = "format";
	}
	if (ok && fs_mount(0) != 0) {
		ok = 0;
		detail = "mount";
	}

	if (ok) {
		for (i = 0; i < FILE_BYTES; i++) {
			wbuf[i] = pattern_byte(i);
		}
		fr = f_open(&fp, "0:/stor.bin", FA_CREATE_ALWAYS | FA_WRITE);
		if (fr != FR_OK) {
			ok = 0;
			detail = "create";
		} else {
			fr = f_write(&fp, wbuf, FILE_BYTES, &bw);
			if (fr != FR_OK || bw != FILE_BYTES) {
				ok = 0;
				detail = "write";
			}
			(void)f_close(&fp);
		}
	}

	if (ok) {
		fr = f_open(&fp, "0:/stor.bin", FA_READ);
		if (fr != FR_OK) {
			ok = 0;
			detail = "reopen";
		} else {
			fr = f_read(&fp, rbuf, FILE_BYTES, &br);
			(void)f_close(&fp);
			if (fr != FR_OK || br != FILE_BYTES) {
				ok = 0;
				detail = "read";
			} else if (memcmp(wbuf, rbuf, FILE_BYTES) != 0) {
				ok = 0;
				detail = "bytes";
			} else if (fnv1a(wbuf, FILE_BYTES, 2166136261u) !=
				   fnv1a(rbuf, FILE_BYTES, 2166136261u)) {
				ok = 0;
				detail = "checksum";
			}
		}
	}

	check("exfat-roundtrip", ok, "bytes=%lu detail=%s fnv=%08lx",
	      (unsigned long)FILE_BYTES, detail,
	      (unsigned long)fnv1a(wbuf, FILE_BYTES, 2166136261u));

	(void)fs_unmount(0);
}

static void case_f1_completion_timeout(void)
{
	static uint8_t buf[FS_SECTOR];
	DRESULT res;
	uint32_t p0;
	uint32_t p1;
	uint32_t t0;
	uint32_t t1;

	begin();
	scripted_nvme_set_fault(SCRIPTED_FAULT_DROP_COMPLETION);

	p0 = scripted_nvme_polls();
	t0 = osKernelGetTickCount();
	res = disk_read(0, buf, 0, 1);
	t1 = osKernelGetTickCount();
	p1 = scripted_nvme_polls();

	scripted_nvme_clear_fault(SCRIPTED_FAULT_DROP_COMPLETION);

	/* A completion that never arrives must be a bounded error, not a hang:
	 * the read returns RES_ERROR, the wait used no more than its bound of
	 * iterations, and the wall time stayed small. */
	check("f1-completion-timeout",
	      (res == RES_ERROR) &&
		      ((p1 - p0) <= SCRIPTED_NVME_MAX_POLLS) &&
		      ((t1 - t0) <= 1000u),
	      "res=%d polls=%lu ms=%lu", (int)res,
	      (unsigned long)(p1 - p0), (unsigned long)(t1 - t0));
}

static void case_f2_late_irq(void)
{
	static uint8_t buf[FS_SECTOR];
	DRESULT res;
	uint32_t comp0;
	uint32_t cons0;
	uint32_t comp1;
	uint32_t cons1;

	begin();
	scripted_nvme_set_fault(SCRIPTED_FAULT_LATE_IRQ);

	res = disk_read(0, buf, 0, 1);
	comp0 = scripted_nvme_completions();
	cons0 = scripted_nvme_consumed();

	/* The interrupt arrives only now, after the completion was already
	 * reaped by the wait loop: it must find the queue empty and consume
	 * nothing. */
	scripted_nvme_late_irq();

	comp1 = scripted_nvme_completions();
	cons1 = scripted_nvme_consumed();

	scripted_nvme_clear_fault(SCRIPTED_FAULT_LATE_IRQ);

	check("f2-late-irq",
	      (res == RES_OK) && (comp0 == 1u) && (cons0 == 1u) &&
		      (comp1 == comp0) && (cons1 == cons0),
	      "res=%d completions=%lu->%lu consumed=%lu->%lu", (int)res,
	      (unsigned long)comp0, (unsigned long)comp1,
	      (unsigned long)cons0, (unsigned long)cons1);
}

static void case_f3_doorbell_drop(void)
{
	static uint8_t w[FS_SECTOR];
	static uint8_t r[FS_SECTOR];
	DRESULT drop;
	DRESULT next;
	DRESULT readback;
	uint32_t p0;
	uint32_t p1;

	(void)memset(w, 0xA5, sizeof(w));
	(void)memset(r, 0, sizeof(r));

	begin();
	scripted_nvme_set_fault(SCRIPTED_FAULT_DROP_DOORBELL);

	p0 = scripted_nvme_polls();
	drop = disk_write(0, w, 0, 1);
	p1 = scripted_nvme_polls();

	scripted_nvme_clear_fault(SCRIPTED_FAULT_DROP_DOORBELL);

	/* The next command must complete cleanly: the dropped submission left
	 * no residue behind it (no cross-talk). */
	next = disk_write(0, w, 0, 1);
	readback = disk_read(0, r, 0, 1);

	check("f3-doorbell-drop",
	      (drop == RES_ERROR) &&
		      ((p1 - p0) <= SCRIPTED_NVME_MAX_POLLS) &&
		      (next == RES_OK) && (readback == RES_OK) &&
		      (memcmp(w, r, sizeof(w)) == 0) &&
		      (scripted_nvme_doorbell_dropped() == 1u),
	      "drop=%d polls=%lu next=%d read=%d cmp=%d dropped=%lu", (int)drop,
	      (unsigned long)(p1 - p0), (int)next, (int)readback,
	      memcmp(w, r, sizeof(w)),
	      (unsigned long)scripted_nvme_doorbell_dropped());
}

static void case_conservation(void)
{
	static uint8_t buf[FS_SECTOR];
	uint32_t i;
	const uint32_t n = 4u;
	int ok = 1;

	begin();
	for (i = 0; i < n; i++) {
		if (disk_read(0, buf, 0, 1) != RES_OK) {
			ok = 0;
		}
	}

	/* Conservation: every command that completed was consumed exactly once,
	 * and a clean run delivered exactly one interrupt per completion. */
	ok = ok && (scripted_nvme_submissions() == n) &&
	     (scripted_nvme_completions() == n) &&
	     (scripted_nvme_consumed() == n) &&
	     (scripted_nvme_irqs() == n) &&
	     (scripted_nvme_spurious_irqs() == 0u);

	check("conservation", ok,
	      "sub=%lu comp=%lu consumed=%lu irqs=%lu spurious=%lu",
	      (unsigned long)scripted_nvme_submissions(),
	      (unsigned long)scripted_nvme_completions(),
	      (unsigned long)scripted_nvme_consumed(),
	      (unsigned long)scripted_nvme_irqs(),
	      (unsigned long)scripted_nvme_spurious_irqs());
}

static void case_irq_suppression(void)
{
	static uint8_t buf[FS_SECTOR];
	DRESULT res;
	int detected;

	begin();
	scripted_nvme_set_fault(SCRIPTED_FAULT_SUPPRESS_IRQ);

	res = disk_read(0, buf, 0, 1);

	scripted_nvme_clear_fault(SCRIPTED_FAULT_SUPPRESS_IRQ);

	/* A run where the interrupt is suppressed is detected by exactly this
	 * comparison: the completion was posted and consumed, but no interrupt
	 * was delivered. This is the reverse image of the board's stability
	 * criterion (there, an unexpected interrupt is the alarm). */
	detected = (res == RES_OK) &&
		   (scripted_nvme_completions() == 1u) &&
		   (scripted_nvme_consumed() == 1u) &&
		   (scripted_nvme_irqs() == 0u);

	check("irq-suppression-detected", detected,
	      "res=%d comp=%lu irqs=%lu", (int)res,
	      (unsigned long)scripted_nvme_completions(),
	      (unsigned long)scripted_nvme_irqs());
}

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--tamper") == 0 && (i + 1) < argc) {
			tamper = argv[i + 1];
			i++;
		} else if (strcmp(argv[i], "--fault") == 0 && (i + 1) < argc) {
			if (strcmp(argv[i + 1], "lying-device") == 0) {
				lying = 1;
			}
			i++;
		}
	}

	case_config();
	case_bind();
	case_sector_map();
	case_format_mount();
	case_roundtrip();
	case_f1_completion_timeout();
	case_f2_late_irq();
	case_f3_doorbell_drop();
	case_conservation();
	case_irq_suppression();

	if (failures == 0) {
		printf("storage-test: PASS\n");
	} else {
		printf("storage-test: FAIL failures=%d\n", failures);
	}

	return (failures == 0) ? 0 : 1;
}
