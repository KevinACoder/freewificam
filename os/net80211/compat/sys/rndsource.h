/*
 * @file
 * @brief Random source shell; entropy comes from the port PRNG.
 */

#ifndef _COMPAT_SYS_RNDSOURCE_H_
#define _COMPAT_SYS_RNDSOURCE_H_

#include <sys/cdefs.h>
#include <sys/types.h>

typedef struct { int unused; } rndsource_element_t;
typedef struct { int unused; } krndsource_t;

#define RND_TYPE_NET 4
#define RND_FLAG_DEFAULT 0

#define rnd_attach_source(r, name, type, flags) ((void) (r), (void) (name))
#define rnd_detach_source(r) ((void) (r))
#define rnd_add_uint32(r, v) ((void) (r), (void) (v))
#define rnd_add_data(r, p, len, est) ((void) (r))

#endif /* _COMPAT_SYS_RNDSOURCE_H_ */
