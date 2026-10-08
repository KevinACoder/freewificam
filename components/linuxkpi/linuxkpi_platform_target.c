/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linuxkpi_platform_target.c
 * @brief   The linuxkpi platform stubs a target image has no machinery for:
 *          the debugfs face (there is no filesystem) and the hex-dump sink.
 *
 * @design stub substitution of the Linux diagnostic surface. The drivers'
 *         debugfs sites are written to tolerate a NULL dentry (the debugfs
 *         helpers return NULL by contract when the filesystem is absent), so
 *         the read callbacks behind them can never run and the dump helper is
 *         only reachable through code the drivers gate on their own debug
 *         switch - the same reasoning the host suite's platform file uses
 *         (tests/host/wlan-bridge-test/lkpi_platform_host.c:93-128), stated
 *         once here for target images. Target-only: host links supply their
 *         own copies, exactly like the USB backend
 *         (ports/cherryusb/lkpi_usb_cherryusb.c).
 * @date    08.10.2026
 */

#include <stdbool.h>
#include <stddef.h>

#include "linuxkpi_core.h"
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/printk.h>

struct dentry *
debugfs_create_dir(const char *name, struct dentry *parent)
{

	(void)name;
	(void)parent;
	return NULL;		/* no debugfs on a target image */
}

struct dentry *
debugfs_create_file_short(const char *name, umode_t mode,
    struct dentry *parent, void *data, const void *aux,
    const struct debugfs_short_fops *fops)
{

	(void)name; (void)mode; (void)parent; (void)data; (void)aux; (void)fops;
	return NULL;
}

ssize_t
simple_read_from_buffer(void __user *to, size_t count, loff_t *ppos,
    const void *from, size_t available)
{

	(void)to; (void)count; (void)ppos; (void)from; (void)available;
	return 0;
}

void
print_hex_dump(const char *level, const char *prefix_str, int prefix_type,
    int rowsize, int groupsize, const void *buf, size_t len, bool ascii)
{

	(void)level; (void)prefix_str; (void)prefix_type; (void)rowsize;
	(void)groupsize; (void)buf; (void)len; (void)ascii;
}
