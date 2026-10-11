/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/circ_buf.h
 * @brief   The ring-buffer count helpers and the ring-buffer record, as the interface
 *          defines them.
 *
 * @design declaration-only compatibility face: this header is all macros and one small
 *         struct, so there is nothing to leave undefined. The four macros are the
 *         interface's (include/linux/circ_buf.h), transcribed token for token, including
 *         the two whose bodies are GCC statement expressions - the arithmetic is the
 *         contract a ring buffer's reader and writer agree on, and the vendor's own
 *         include/circ_buf.h defines the first two with the same tokens, so the
 *         repetition is a redefinition with an identical body (no diagnostic) rather
 *         than a second mechanism. Reference for the face shape:
 *         components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_CIRC_BUF_H_
#define _LINUXKPI_LINUX_CIRC_BUF_H_

#include "../../linuxkpi_core.h"

/* [LNX] include/linux/circ_buf.h: the ring buffer the helpers below operate on. */
struct circ_buf {
	char	*buf;
	int	head;
	int	tail;
};

/* [LNX] include/linux/circ_buf.h:11-12. The spelling of these two (including the
 * spacing inside the bodies) is deliberately the interface's character for character:
 * the vendored tree carries its own copy of this header, reached by the bare name
 * `circ_buf.h`, and an identical spelling makes its redefinition a no-op instead of a
 * redefinition warning. */
#define CIRC_CNT(head,tail,size) (((head) - (tail)) & ((size)-1))

/* [LNX] include/linux/circ_buf.h:17 */
#define CIRC_SPACE(head,tail,size) CIRC_CNT((tail),((head)+1),(size))

/* [LNX] include/linux/circ_buf.h:22-24 */
#define	CIRC_CNT_TO_END(head, tail, size)				\
	({int end = (size) - (tail);					\
	  int n = ((head) + end) & ((size) - 1);			\
	  n < end ? n : end;})

/* [LNX] include/linux/circ_buf.h:27-29 */
#define	CIRC_SPACE_TO_END(head, tail, size)				\
	({int end = (size) - 1 - (head);				\
	  int n = (end + (tail)) & ((size) - 1);			\
	  n <= end ? n : end + 1;})

#endif /* _LINUXKPI_LINUX_CIRC_BUF_H_ */
