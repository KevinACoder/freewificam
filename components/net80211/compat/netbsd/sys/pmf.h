/*
 * @file
 * @brief pmf(9) shell: the device registration hooks are no-ops here
 * (there is no power management framework to hang them off), so this is
 * a view onto the stubs sys/device.h already provides.
 */

#ifndef _COMPAT_SYS_PMF_H_
#define _COMPAT_SYS_PMF_H_

#include <sys/device.h>

#endif /* _COMPAT_SYS_PMF_H_ */
