/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * fw_registry - the minimal in-memory firmware registry (SPEC S6/linuxkpi
 * §3.6-6).  The image has no filesystem, so request_firmware() resolves
 * against compiled-in C arrays registered here by bare name.
 *
 * PROVENANCE: the registry mechanism is first-party (shaped on the prior
 * line's firmware_cmsis.c, [EXPL] linuxkpi_core.c:1110-1165).  Firmware
 * blobs themselves are NOT part of this file: a blob lands here as its own
 * generated TU (gen_firmware_array.py output) with its own PROVENANCE
 * header and license note, and only ever through the gates cleanroom
 * allowlist.  The registry ships empty - the U-disk acceptance line (case
 * A) consumes zero firmware; the wlan lines add their blobs on their own
 * slices.
 */

#include <stddef.h>
#include <string.h>

#include <linux/usb.h>	/* u8 + dev_name through the core header */

#include "linuxkpi_impl.h"

struct lkpi_fw_entry {
	const char	*name;
	const u8	*data;
	size_t		size;
};

/* populated by generated blob TUs via lkpi_fw_register() */
#define	LKPI_FW_MAX	8

static struct lkpi_fw_entry lkpi_fw_table[LKPI_FW_MAX];
static unsigned int lkpi_fw_count;

void
lkpi_fw_register(const char *name, const u8 *data, size_t size)
{

	if (lkpi_fw_count >= LKPI_FW_MAX) {
		lkpi_printf("lkpi: fw registry full, '%s' dropped\n", name);
		return;
	}
	lkpi_fw_table[lkpi_fw_count].name = name;
	lkpi_fw_table[lkpi_fw_count].data = data;
	lkpi_fw_table[lkpi_fw_count].size = size;
	lkpi_fw_count++;
}

int
lkpi_fw_registry_find(const char *base, const u8 **data, size_t *size)
{
	unsigned int i;

	for (i = 0; i < lkpi_fw_count; i++) {
		if (strcmp(lkpi_fw_table[i].name, base) == 0) {
			*data = lkpi_fw_table[i].data;
			*size = lkpi_fw_table[i].size;
			return 0;
		}
	}
	return -1;
}
