/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * @file    linux/debugfs.h
 * @brief   The diagnostics files a driver may create under its wiphy directory.
 *
 * @design declaration-only compatibility face. The interface's 7.3 shape is kept,
 *         including the `_Generic` dispatch that picks the short or the full operation
 *         table from the argument's type: driver sources call one name and must compile
 *         unchanged, so the dispatch has to exist here too. In a freestanding image
 *         there is no filesystem to serve the files; the calls exist so the driver's
 *         diagnostics path links, and the bodies return the "no entry" answer the
 *         interface itself uses when debugfs is compiled out. Reference for the face
 *         shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_DEBUGFS_H_
#define _LINUXKPI_LINUX_DEBUGFS_H_

#include "../../linuxkpi_core.h"
#include <linux/fs.h>
/* umode_t lives in this tree's sysfs face; the interface has it in linux/types.h. */
#include <linux/sysfs.h>

struct file_operations;

/*
 * The abbreviated operation table a driver may hand debugfs when it only needs the
 * basic read/write entry points.
 */
struct debugfs_short_fops {
	ssize_t	(*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t	(*write)(struct file *, const char __user *, size_t, loff_t *);
	loff_t	(*llseek)(struct file *, loff_t, int);
};

struct dentry *debugfs_create_dir(const char *name, struct dentry *parent);
void debugfs_remove(struct dentry *dentry);
#define	debugfs_remove_recursive	debugfs_remove

struct dentry *debugfs_create_file_full(const char *name, umode_t mode,
					struct dentry *parent, void *data,
					const void *aux,
					const struct file_operations *fops);
struct dentry *debugfs_create_file_short(const char *name, umode_t mode,
					 struct dentry *parent, void *data,
					 const void *aux,
					 const struct debugfs_short_fops *fops);

#define	debugfs_create_file(name, mode, parent, data, fops)		\
	_Generic(fops,							\
		 const struct file_operations *: debugfs_create_file_full, \
		 const struct debugfs_short_fops *: debugfs_create_file_short, \
		 struct file_operations *: debugfs_create_file_full,	\
		 struct debugfs_short_fops *: debugfs_create_file_short) \
		(name, mode, parent, data, NULL, fops)

#endif /* _LINUXKPI_LINUX_DEBUGFS_H_ */