/*
 * @file
 * @brief Minimal lib/libkern/libkern.h shadow: the helpers the
 * verbatim crypto imports expect.  Byte order lives in sys/endian.h,
 * kernel primitives in sys/systm.h like on NetBSD.
 *
 * @date 08.09.2026
 * @author zhugengyu
 */

#ifndef _COMPAT_LIB_LIBKERN_H_
#define _COMPAT_LIB_LIBKERN_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/systm.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef __predict_true
#define __predict_true(exp) (exp)
#endif
#ifndef __predict_false
#define __predict_false(exp) (exp)
#endif

#endif /* _COMPAT_LIB_LIBKERN_H_ */
