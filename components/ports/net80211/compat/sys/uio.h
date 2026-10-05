/*
 * @file
 * @brief uio(9) shell: the usb device node's control-request passthrough
 * builds one; the copy lands through uiomove's single-iov path.
 */

#ifndef _COMPAT_SYS_UIO_H_
#define _COMPAT_SYS_UIO_H_

#include <sys/cdefs.h>
#include <stddef.h>
#include <string.h>

enum uio_rw { UIO_READ, UIO_WRITE };
enum uio_seg { UIO_USERSPACE, UIO_SYSSPACE };

struct iovec {
	void *iov_base;
	size_t iov_len;
};

struct vmspace;
struct proc;

struct uio {
	struct iovec *uio_iov;
	int uio_iovcnt;
	size_t uio_resid;
	off_t uio_offset;
	enum uio_rw uio_rw;
	enum uio_seg uio_segflg;
	struct vmspace *uio_vmspace;
};

static inline int
uiomove(void *buf, size_t n, struct uio *u)
{
	struct iovec *iov = u->uio_iov;
	size_t len = n < u->uio_resid ? n : u->uio_resid;

	if (u->uio_segflg == UIO_SYSSPACE || iov->iov_len < len) {
		len = iov->iov_len < len ? iov->iov_len : len;
	}
	if (u->uio_rw == UIO_WRITE) {
		memcpy(buf, iov->iov_base, len);
	} else {
		memcpy(iov->iov_base, buf, len);
	}
	iov->iov_base = (char *) iov->iov_base + len;
	iov->iov_len -= len;
	u->uio_resid -= len;
	return 0;
}

#endif /* _COMPAT_SYS_UIO_H_ */
