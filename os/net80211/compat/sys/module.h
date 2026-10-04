/*
 * @file
 * @brief module(9) shell: built-in, no autoload.
 */

#ifndef _COMPAT_SYS_MODULE_H_
#define _COMPAT_SYS_MODULE_H_

#include <sys/cdefs.h>

typedef enum { MODULE_CMD_INIT, MODULE_CMD_FINI, MODULE_CMD_AUTOUNLOAD,
	MODULE_CMD_STAT } modcmd_t;

typedef enum module_class {
	MODULE_CLASS_ANY,
	MODULE_CLASS_DRIVER,
	MODULE_CLASS_MISC,
	MODULE_CLASS_EXEC,
	MODULE_CLASS_VM
} module_class_t;

typedef int (*modcmd_t_fn)(modcmd_t, void *);

#define MODULE(...)

static inline int module_autoload(const char *name, module_class_t cls) {
	(void) name; (void) cls;
	return -1;
}

#endif /* _COMPAT_SYS_MODULE_H_ */
