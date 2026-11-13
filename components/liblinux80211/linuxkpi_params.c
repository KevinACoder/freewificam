/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_params.c
 * @brief   The kernel-parameter records a vendored driver's module_param()
 *          family needs to link, and the one honest answer this port can give
 *          about changing them at runtime.
 *
 * @design The interface's module_param() macros place a `struct kernel_param`
 *         in the image's `__param` section and take the address of one of the
 *         `param_ops_*` records; nothing else is required to compile a driver
 *         (include/linux/moduleparam.h states the records, not the bodies,
 *         because the kernel's bodies live in kernel/params.c).  This port has
 *         neither sysfs nor module loading, so there is no runtime channel a
 *         set could arrive through.  Rather than write a parser no caller can
 *         reach, the setters say exactly that (-EOPNOTSUPP) and the getters
 *         report the value the code initialized - which is what a diagnostic
 *         reading the record would want.
 *
 *         The GT round set the same variables through insmod's parameter
 *         channel (`rtw_power_mgnt=0 rtw_lps_level=0`); the station case sets
 *         the vendor globals directly before module init, and the face header
 *         (wireless-drivers/rtl8189fs/driver_config.h) states the defaults.
 *         The values are therefore the same two ways; only the channel differs,
 *         and the difference is recorded here rather than hidden in a stub.
 *
 * @date    13.11.2026
 */

#include "../../linuxkpi_core.h"

#include <linux/moduleparam.h>

/* One page, the kernel's own bound for a parameter's printable value
 * (kernel/params.c:param_get_* write into a PAGE_SIZE buffer). */
#define LKPI_PARAM_BUF		4096

static int
lkpi_param_set_no_channel(const char *val, const struct kernel_param *kp)
{

	(void)val;
	(void)kp;
	return -EOPNOTSUPP;
}

static int
lkpi_param_get_int(char *buffer, const struct kernel_param *kp)
{

	return scnprintf(buffer, LKPI_PARAM_BUF, "%d", *(const int *)kp->arg);
}

static int
lkpi_param_get_uint(char *buffer, const struct kernel_param *kp)
{

	return scnprintf(buffer, LKPI_PARAM_BUF, "%u",
	    *(const unsigned int *)kp->arg);
}

static int
lkpi_param_get_bool(char *buffer, const struct kernel_param *kp)
{

	return scnprintf(buffer, LKPI_PARAM_BUF, "%c",
	    *(const bool *)kp->arg ? 'Y' : 'N');
}

static int
lkpi_param_get_charp(char *buffer, const struct kernel_param *kp)
{
	const char *s = *(char *const *)kp->arg;

	return scnprintf(buffer, LKPI_PARAM_BUF, "%s", (s != NULL) ? s : "");
}

/* [LNX] kernel/params.c: the exported records the module_param() expansion
 * names (`param_ops_##type`).  The setter is the port's answer, the getter is
 * the value's. */
const struct kernel_param_ops param_ops_int = {
	.set = lkpi_param_set_no_channel,
	.get = lkpi_param_get_int,
};

const struct kernel_param_ops param_ops_uint = {
	.set = lkpi_param_set_no_channel,
	.get = lkpi_param_get_uint,
};

const struct kernel_param_ops param_ops_bool = {
	.set = lkpi_param_set_no_channel,
	.get = lkpi_param_get_bool,
};

const struct kernel_param_ops param_ops_charp = {
	.set = lkpi_param_set_no_channel,
	.get = lkpi_param_get_charp,
};

/* The array record: module_param_array() takes its address and keeps the
 * element count in a separate variable the code already owns.  There is no
 * channel to set it through, and no reader that needs a rendering, so both
 * entries are absent rather than invented (the struct's own contract allows
 * them to be NULL; only set/get are documented as the pair a caller uses). */
const struct kernel_param_ops param_array_ops = {
	.set = NULL,
	.get = NULL,
};
