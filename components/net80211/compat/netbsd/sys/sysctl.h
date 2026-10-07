/*
 * @file
 * @brief sysctl(7) stub: the whole tree compiles out.
 *
 * The imported glue wraps its sysctl nodes in SYSCTL_SETUP blocks;
 * with the macros below they expand to nothing, no registration
 * happens, and the runtime knobs keep their defaults.
 * sysctl_createv reports failure (any nonzero) so callers take their
 * error paths and never dereference nodes.
 */

#ifndef _COMPAT_SYS_SYSCTL_H_
#define _COMPAT_SYS_SYSCTL_H_

#include <sys/cdefs.h>

struct sysctllog { int unused; };
typedef struct sysctllog sysctllog;

struct sysctlnode {
	unsigned sysctl_num;
	void *sysctl_data;
};

#define CTL_KERN 1
#define CTL_HW 6
#define KERN_SYSVIPC 39

#define CTL_EOL 0
#define CTL_CREATE 0

#define SYSCTL_INT 1
#define SYSCTL_QUAD 2
#define SYSCTL_STRING 3
#define SYSCTL_NODE 4

#define CTLFLAG_READWRITE 0
#define CTLFLAG_READONLY 0
#define CTLFLAG_HIDDEN 0
#define CTLFLAG_PERMANENT 0
#define CTLTYPE_INT 0
#define CTLTYPE_NODE 0

#define SYSCTL_DESCR(desc) (desc)
#define SYSCTLFN_PROTO struct sysctlnode *rnode, void *newp
#define SYSCTLFN_ARGS SYSCTLFN_PROTO
#define SYSCTLFN_CALL(nodep) (nodep), NULL

#define SYSCTL_SETUP(name, desc) static void name(void)

/* always fails; the callers take their error paths and never
 * dereference the node */
#define sysctl_createv(...) (-1)

static inline int sysctl_lookup(struct sysctlnode *node, void *newp) {
	(void) node;
	(void) newp;
	return 0;
}

#endif /* _COMPAT_SYS_SYSCTL_H_ */
