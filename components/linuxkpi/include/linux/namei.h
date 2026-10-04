#ifndef _LINUXKPI_LINUX_NAMEI_H_
#define _LINUXKPI_LINUX_NAMEI_H_
/* [S5b-W1] rtl8851bu funnel header (osdep_service_linux.h:26): only the
 * isDirReadable() compile face - struct path + kern_path(LOOKUP_FOLLOW).
 * The image has no filesystem (every filp_open fails) and the helper is
 * dead-path; the kern_path stub lives beside filp_open in linuxkpi_core.c. */
struct path {
	void	*mnt;
	void	*dentry;
};

#define	LOOKUP_FOLLOW		1

int kern_path(const char *pathname, unsigned flags, struct path *path);
#endif
