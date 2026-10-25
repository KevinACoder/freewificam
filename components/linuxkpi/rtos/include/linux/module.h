/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/module.h
 * @brief   What a loadable module's registration lines need, beyond the module plumbing
 *          the core already carries.
 *
 * @design declaration-only compatibility face with inert bodies, and the reason is the
 *         load model rather than convenience: a Linux module is loaded by the kernel's
 *         module loader, which consumes MODULE_* metadata and the parameter table, while
 *         this project links every driver into one image whose lifetime is the boot. The
 *         driver's parameter variables are declared next to their module_param() calls, so
 *         dropping the registration changes nothing the compiler sees - but the call sites
 *         stay, which is what keeps the vendored bytes and this gate compiling the same
 *         source. The core header already defines the common half of this interface
 *         (MODULE_LICENSE/AUTHOR/DESCRIPTION/FIRMWARE/PARM_DESC, module_param,
 *         module_param_named, MODULE_DEVICE_TABLE, module_init/exit, THIS_MODULE); this
 *         file adds only the spellings it does not carry, so no name has two definitions.
 *         MODULE_LICENSE is the one of these the licence audit reads, and the suite's spdx
 *         gate checks the vendored driver's own anchors in its sources, not here.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_MODULE_H_
#define _LINUXKPI_LINUX_MODULE_H_

#include "../../linuxkpi_core.h"
#include "init.h"

struct module;

/* The array and string parameter spellings: same reasoning as module_param (the core),
 * each expanding to a repeated declaration that keeps the call site's trailing semicolon
 * meaningful. */
#define	module_param_array(name, type, nump, perm)	extern int lkpi_module_param_inert__
#define	module_param_string(name, string, len, perm)	extern int lkpi_module_param_inert__

/* The remaining description lines a module may carry. */
#define	MODULE_VERSION(version)			extern int lkpi_module_version_inert__
#define	MODULE_SUPPORTED_DEVICE(name)		extern int lkpi_module_supported_inert__
#define	MODULE_ALIAS(alias)			extern int lkpi_module_alias_inert__
/* [LNX] include/linux/module.h:302: the imported-namespace line. It is module
 * metadata for the loader; this project links one image, so it is inert. */
#define	MODULE_IMPORT_NS(ns)			extern int lkpi_module_import_ns_inert__

/* ------------------------------------------------------------------ */
/* The reference-count and module-request calls the compiled set       */
/* names, in the interface's !CONFIG_MODULES arm. This project links   */
/* every driver into one image whose lifetime is the boot: there is no */
/* module to reference-count and no loader that could refuse a         */
/* reference, so the take always succeeds and the release is empty.    */
/* THIS_MODULE is NULL (linuxkpi_core.h:1663), which both tolerate.    */
/* ------------------------------------------------------------------ */

/* [LNX] include/linux/module.h:831-834 (!CONFIG_MODULES) */
static __always_inline __unused bool
try_module_get(struct module *module)
{
	(void)module;
	return true;
}

/* [LNX] include/linux/module.h:836-838 (!CONFIG_MODULES) */
static __always_inline __unused void
module_put(struct module *module)
{
	(void)module;
}

/* [LNX] include/linux/kmod.h:28 (!CONFIG_MODULES): the interface reaches this
 * through <linux/module.h>, which includes <linux/kmod.h>; a driver's firmware
 * path names it when the op-mode it wants is not registered. One image has no
 * module to load, so the interface's own not-available answer is the honest one. */
static __always_inline __unused int
request_module_nowait(const char *name, ...)
{
	(void)name;
	return -ENOSYS;
}

#endif /* _LINUXKPI_LINUX_MODULE_H_ */
