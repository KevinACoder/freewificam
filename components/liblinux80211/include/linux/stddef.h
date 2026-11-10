/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/stddef.h
 * @brief   The include Linux code names for offsetof/struct_group; the
 *          definitions are the contract header's.
 *
 * @design declaration-only compatibility face.  The interface's stddef.h owns
 *         offsetof, struct_group and the field helpers; the component's
 *         contract header states them (linuxkpi_core.h, the container_of and
 *         struct_group sections).  The real stack headers include this name;
 *         restating the macros here would be the second spelling.
 * @date    10.11.2026
 */

#ifndef _LINUXKPI_LINUX_STDDEF_H_
#define _LINUXKPI_LINUX_STDDEF_H_

#include "../../linuxkpi_core.h"

#endif /* _LINUXKPI_LINUX_STDDEF_H_ */
