/*
 * @file
 * @brief module(9) shell: built-in, no autoload.
 */

#ifndef _COMPAT_SYS_MODULE_H_
#define _COMPAT_SYS_MODULE_H_

#include <sys/cdefs.h>

typedef enum { MODULE_CMD_INIT, MODULE_CMD_FINI, MODULE_CMD_AUTOUNLOAD,
	MODULE_CMD_STAT } modcmd_t;

typedef int (*modcmd_t_fn)(modcmd_t, void *);

#define MODULE(...)

#endif /* _COMPAT_SYS_MODULE_H_ */
