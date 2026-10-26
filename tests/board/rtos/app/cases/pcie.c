/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   pcie.c
 * @brief  Case pcie (firefly v1): walk the firmware-trained ECAM tree
 *         probe-only and file it against the board's own device fixture.
 *
 * The fixture is the 11-function table the board's own firmware enumerates
 * (the boot loader's `pci` walk and an independent OS walk on the same tree
 * agree on it). The walk
 * keeps the firmware BAR assignment untouched (probe-only), so a mismatch
 * means either the walk or the tree changed - both worth a red verdict,
 * never a silent diff.
 */

#include <stdint.h>

#include "cases.h"

#include "log.h"
#include "pcie_ecam.h"

struct pcie_enum_expect {
	uint8_t bus;
	uint8_t dev;
	uint8_t fn;
	uint16_t vendor;
	uint16_t device;
	uint16_t class_16;
};

static const struct pcie_enum_expect expect[] = {
	{ 0, 0, 0, 0x17cd, 0xdc16, 0x0604 },
	{ 0, 1, 0, 0x17cd, 0xdc08, 0x0604 },
	{ 0, 2, 0, 0x17cd, 0xdc01, 0x0604 },
	{ 0, 3, 0, 0x17cd, 0xdc16, 0x0604 },
	{ 0, 4, 0, 0x17cd, 0xdc08, 0x0604 },
	{ 0, 5, 0, 0x17cd, 0xdc01, 0x0604 },
	{ 1, 0, 0, 0x144d, 0xa809, 0x0108 },
	{ 3, 0, 0, 0x1b21, 0x0612, 0x0106 },
	{ 4, 0, 0, 0x8086, 0x10d3, 0x0200 },
	{ 5, 0, 0, 0x144d, 0xa809, 0x0108 },
	{ 6, 0, 0, 0x1912, 0x0014, 0x0c03 },
};

#define EXPECT_COUNT (sizeof(expect) / sizeof(expect[0]))

static const struct pcie_ecam_func *find_bdf(uint8_t bus, uint8_t dev,
					     uint8_t fn)
{
	uint32_t count = pcie_ecam_dev_count();

	for (uint32_t i = 0u; i < count; i++) {
		const struct pcie_ecam_func *f = pcie_ecam_dev(i);

		if (f->bus == bus && f->dev == dev && f->fn == fn) {
			return f;
		}
	}
	return 0;
}

int case_pcie_run(void)
{
	uint32_t count;
	uint32_t matches = 0u;
	unsigned int i;
	int walked = pcie_ecam_init();

	if (walked < 0) {
		CASE_ANCHOR("pcie: FAIL ecam walk\n");
		return -1;
	}
	count = (uint32_t)walked;

	/* The tree itself is evidence first: the dump lines carry B:D.F,
	 * VID:PID, class and the assigned BARs. */
	pcie_ecam_dump();

	if (count != (uint32_t)EXPECT_COUNT) {
		CASE_ANCHOR("pcie: FAIL devs=%u fixture=%u\n",
			    (unsigned)count, (unsigned)EXPECT_COUNT);
		return -1;
	}

	for (i = 0u; i < EXPECT_COUNT; i++) {
		const struct pcie_ecam_func *f = find_bdf(expect[i].bus,
							  expect[i].dev,
							  expect[i].fn);

		if (f == 0) {
			CASE_ANCHOR("pcie: FAIL missing %02x:%02x.%x\n",
				    expect[i].bus, expect[i].dev,
				    expect[i].fn);
			return -1;
		}
		if (f->vendor_id != expect[i].vendor ||
		    f->device_id != expect[i].device ||
		    (uint16_t)(f->class_code >> 8) != expect[i].class_16) {
			CASE_ANCHOR("pcie: FAIL mismatch %02x:%02x.%x "
				    "got %04x:%04x class %02x%02x "
				    "want %04x:%04x class %04x\n",
				    f->bus, f->dev, f->fn, f->vendor_id,
				    f->device_id,
				    (unsigned)((f->class_code >> 16) & 0xffu),
				    (unsigned)((f->class_code >> 8) & 0xffu),
				    expect[i].vendor, expect[i].device,
				    expect[i].class_16);
			return -1;
		}
		matches++;
	}

	for (i = 0u; i < count; i++) {
		const struct pcie_ecam_func *f = pcie_ecam_dev(i);

		CASE_ANCHOR("pcie: %02x:%02x.%x %04x:%04x class %02x%02x\n",
			    f->bus, f->dev, f->fn, f->vendor_id, f->device_id,
			    (unsigned)((f->class_code >> 16) & 0xffu),
			    (unsigned)((f->class_code >> 8) & 0xffu));
	}
	CASE_ANCHOR("pcie: devs=%u matches=%u\n",
		    (unsigned)count, (unsigned)matches);
	return 0;
}
