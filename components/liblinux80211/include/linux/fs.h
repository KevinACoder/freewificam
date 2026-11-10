/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/fs.h
 * @brief   The slice of the VFS objects a driver touches: the file, its inode and the
 *          directory entry, the open-file operation table, and the helpers the file
 *          read/write path and the debugfs read path use.
 *
 * @design declaration-only compatibility face. A freestanding image has no virtual
 *         filesystem, so these are the shapes only: the private pointer a driver hangs
 *         off an inode, the buffered-read helper, and the file path a driver walks to
 *         reach the inode of an opened firmware/efuse file are what a diagnostics or
 *         load path needs, and nothing here is ever mounted. Declared as the interface
 *         declares them (include/linux/fs.h), so driver code reads unchanged. The
 *         operation table is carried with the interface's leading members (owner,
 *         llseek, read, write) - the read/write pair is what the vendored file reader
 *         names and the rest of the interface's large table arrives with a caller.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    11.10.2026
 */

#ifndef _LINUXKPI_LINUX_FS_H_
#define _LINUXKPI_LINUX_FS_H_

#include "../../linuxkpi_core.h"

struct dentry;
struct vfsmount;
struct module;

/*
 * [LNX] include/linux/types.h / include/linux/fs.h: the file-mode word and the
 * two owner/mode helper types the file surface names.
 */
typedef unsigned int	fmode_t;
typedef unsigned short	umode_t;
typedef void		*fl_owner_t;

/* [LNX] include/linux/fs.h:108-153: the file-mode bits, at the interface's values.
 * FMODE_CAN_READ is what the vendored file reader tests before it reads. */
#define	FMODE_READ		((__force fmode_t)(1 << 0))
#define	FMODE_WRITE		((__force fmode_t)(1 << 1))
#define	FMODE_CAN_READ		((__force fmode_t)(1 << 17))

/*
 * [LNX] include/uapi/asm-generic/fcntl.h:19-27: the open flags the vendored file
 * reader passes to filp_open(). Only the leading family a caller names is carried.
 */
#ifndef O_RDONLY
#define	O_RDONLY	00000000
#endif
#ifndef O_WRONLY
#define	O_WRONLY	00000001
#endif
#ifndef O_RDWR
#define	O_RDWR		00000002
#endif
#ifndef O_CREAT
#define	O_CREAT		00000100
#endif

/* [LNX] include/linux/dcache.h:103: the directory entry, carrying the inode a driver
 * reaches through the file path. */
struct dentry {
	struct inode	*d_inode;
};

/* [LNX] include/linux/path.h: the path record; the file keeps one. */
struct path {
	struct vfsmount	*mnt;
	struct dentry	*dentry;
};

/* [LNX] include/linux/fs.h:1919-1929: the open-file operation table, carrying the
 * leading members the compiled set names; read/write are the two the vendored file
 * reader calls. */
struct file_operations {
	struct module		*owner;
	loff_t			(*llseek) (struct file *, loff_t, int);
	ssize_t			(*read) (struct file *, char __user *, size_t, loff_t *);
	ssize_t			(*write) (struct file *, const char __user *, size_t, loff_t *);
};

/* [LNX] include/linux/fs.h: the inode, carrying the private pointer a driver hangs
 * its data off and the size word i_size_read() returns. */
struct inode {
	loff_t		i_size;
	/* The caller's own pointer, set when the file is created and read back
	 * through file_inode() from the read handler. */
	void		*i_private;
};

/* [LNX] include/linux/fs.h:1255-1292: the open file, carrying the members the vendored
 * file reader reaches (the mode word, the operation table, the position and the path
 * whose inode gives the file size). */
struct file {
	fmode_t				f_mode;
	const struct file_operations	*f_op;
	void				*private_data;
	struct inode			*f_inode;
	union {
		const struct path	f_path;
		struct path		__f_path;
	};
	loff_t				f_pos;
};

static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}

/* [LNX] include/linux/fs.h:1123-1143: the size accessor the vendored file reader uses
 * to learn how large an opened file is. The interface's atomic load collapses to the
 * plain read on this port, which is single-threaded at this level. */
static inline loff_t i_size_read(const struct inode *inode)
{
	return inode->i_size;
}

/* [LNX] include/linux/fs.h: the open-file lifecycle and read entry points, declared as
 * the interface declares them; the bodies belong to the port's file seam. */
struct file *filp_open(const char *name, int flags, umode_t mode);
int filp_close(struct file *filp, fl_owner_t id);
ssize_t kernel_read(struct file *file, void *buf, size_t count, loff_t *pos);

/*
 * Copy up to `available` bytes out of `from` into a reader's buffer, honouring the
 * read position. Returns the number of bytes copied, 0 at end of data, or a negative
 * errno.
 */
ssize_t simple_read_from_buffer(void __user *to, size_t count, loff_t *ppos,
				const void *from, size_t available);

#endif /* _LINUXKPI_LINUX_FS_H_ */
