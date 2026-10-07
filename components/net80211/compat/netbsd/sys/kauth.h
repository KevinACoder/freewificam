/*
 * @file
 * @brief kauth(9) stub: single-privilege context.
 */

#ifndef _COMPAT_SYS_KAUTH_H_
#define _COMPAT_SYS_KAUTH_H_

#include <sys/cdefs.h>

struct kauth_cred;
typedef struct kauth_cred kauth_cred_t;
#define kauth_authorize_generic(...) 1

#endif /* _COMPAT_SYS_KAUTH_H_ */
