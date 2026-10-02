#ifndef _LINUXKPI_LINUX_PROC_FS_H_
#define _LINUXKPI_LINUX_PROC_FS_H_
/* [S6] B8/C-tier: procfs is eliminated - this face only makes rtw_proc.h's
 * declarations parse (rtw_proc.c itself is NOT in the compile set). */
#include "../../linuxkpi_core.h"

struct proc_dir_entry;

struct proc_ops {
	unsigned int	proc_flags;
	void		*proc_open_placeholder;
};

#define	proc_create_data(name, mode, parent, ops, data)	((void *)(parent), (void *)(data), NULL)
#define	proc_mkdir(name, parent)	((void *)(parent), NULL)
#define	proc_remove(de)			do { (void)(de); } while (0)
#define	remove_proc_entry(name, parent)	do { (void)(name); (void)(parent); } while (0)
#define	proc_set_size(de, size)		do { (void)(de); (void)(size); } while (0)
#define	proc_set_user(de, uid, gid)	do { (void)(de); } while (0)
#define	pde_data(de)			(NULL)
#define	PDE_DATA(de)			(NULL)
#endif
