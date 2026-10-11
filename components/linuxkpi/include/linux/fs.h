/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/fs.h
 * @brief   The slice of the VFS objects a driver touches: the file, its inode and the
 *          directory entry, plus the two helpers the debugfs read path uses.
 *
 * @design declaration-only compatibility face. A freestanding image has no virtual
 *         filesystem, so these are the shapes only: the private pointer a driver hangs
 *         off an inode and the buffered-read helper are what a diagnostics file needs,
 *         and nothing here is ever mounted. Declared as the interface declares them
 *         (include/linux/fs.h) so driver code reads unchanged. Reference for the face
 *         shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    08.10.2026
 */

#ifndef _LINUXKPI_LINUX_FS_H_
#define _LINUXKPI_LINUX_FS_H_

#include "../../linuxkpi_core.h"

struct dentry;

struct inode {
	/* The caller's own pointer, set when the file is created and read back
	 * through file_inode() from the read handler. */
	void		*i_private;
};

struct file {
	struct inode	*f_inode;
	void		*private_data;
};

static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}

/*
 * Copy up to `available` bytes out of `from` into a reader's buffer, honouring the
 * read position. Returns the number of bytes copied, 0 at end of data, or a negative
 * errno.
 */
ssize_t simple_read_from_buffer(void __user *to, size_t count, loff_t *ppos,
				const void *from, size_t available);

#endif /* _LINUXKPI_LINUX_FS_H_ */