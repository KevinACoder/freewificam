#ifndef _LINUXKPI_LINUX_INIT_H_
#define _LINUXKPI_LINUX_INIT_H_
/* [S6] B8: init-section markers are no-ops in a built-in image */
#define	__init
#define	__exit
#define	__initdata
#define	__exitdata
#define	__initconst
#define	__devinit
#define	__devexit
#define	__ref
#define	module_init_ok(fn)	((void)(fn), 0)
#endif
