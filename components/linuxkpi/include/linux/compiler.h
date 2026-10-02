#ifndef _LINUXKPI_LINUX_COMPILER_H_
#define _LINUXKPI_LINUX_COMPILER_H_
/* [S6] B8: the compiler-attr face the osdep funnel expects; the port maps
 * them onto the plain spellings this freestanding build already uses. */
#define	__user
#define	__kernel
#define	__safe
#define	__force
#define	__nocast
#define	__iomem
#define	__chk_user_ptr(x)	do { } while (0)
#define	__may_alias
#define	__printf(a, b)		__attribute__((__format__(printf, a, b)))
#define	__scanf(a, b)
#define	__attribute_const__	__attribute__((__const__))
#define	__used			__attribute__((__used__))
#define	__compiler_offsetof(a, b) __builtin_offsetof(a, b)
#define	__always_unused		__attribute__((__unused__))
#define	__maybe_unused		__attribute__((__unused__))
#define	noinline		__attribute__((__noinline__))
#define	__section(s)		__attribute__((__section__(s)))
#define	__visible
#define	__branch_check__(x, y, z) (x)
#endif
