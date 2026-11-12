/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    hostap_printf_bind.h
 * @brief   Bind the imported tree's stdio printf to the image's console
 *          component (third-party/printf, printf_()).
 *
 * The image deliberately keeps the libc printf names unbound except for
 * snprintf/vsnprintf/sprintf (platform/os/baremetal/printf_bind.c): every
 * first-party world prints through lkpi_printf()/ulog.  Hostapd is not
 * first-party: its logging calls printf() directly, and with the libc name
 * left unbound the call reaches newlib's FILE-based printf, which is not
 * part of this freestanding image and hangs on its stdio lock.  Mapping the
 * two names onto the component's spellings is the whole port face of that.
 */
#ifndef LKPI_HOSTAP_PRINTF_BIND_H
#define LKPI_HOSTAP_PRINTF_BIND_H

int printf_(const char *format, ...);
int vprintf_(const char *format, __builtin_va_list va);

#define printf	printf_
#define vprintf	vprintf_

#endif /* LKPI_HOSTAP_PRINTF_BIND_H */
