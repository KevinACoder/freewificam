#ifndef _LINUXKPI_LINUX_UTSNAME_H_
#define _LINUXKPI_LINUX_UTSNAME_H_
/* [S6] B8: the version-string face the dump paths print (utsname()->release) */
#include "../../linuxkpi_core.h"

struct new_utsname {
	char	sysname[65];
	char	nodename[65];
	char	release[65];
	char	version[65];
	char	machine[65];
	char	domainname[65];
};

static __always_inline __unused struct new_utsname *
utsname(void)
{
	static struct new_utsname u = {
		.sysname = "freewebcam",
		.nodename = "rk3568-itx",
		.release = "6.12.0-lkpi",
		.version = "linuxkpi",
		.machine = "aarch64",
		.domainname = "",
	};

	return &u;
}
#endif
