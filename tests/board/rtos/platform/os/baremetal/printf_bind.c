/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * printf_bind.c - the C library's formatted-output names, bound to the
 * image's own printf component (components/printf).
 *
 * Why this exists at all, now that newlib supplies the rest of the C
 * library: newlib's stdio drags in malloc (and its _sbrk chain) and the
 * floating-point renderer, and this image has no heap for it and no FP
 * context switching. The console's line surface - which every judge and
 * log anchor keys on - stays with the component the anchors were built on,
 * and the component's documented degraded behaviour (no float rendering,
 * always NUL-terminated output) stays the contract. Everything else in the
 * C library (string, memory, qsort, strtol, sscanf) comes from newlib.
 *
 * The three names here are the whole binding; the component itself exports
 * underscore-suffixed spellings (printf_/vsnprintf_/...) precisely so the
 * libc names can be bound in one place like this.
 */

#include <stdarg.h>
#include <stddef.h>

/* The component's public entry points (components/printf/printf.h). */
int vsnprintf_(char *buf, size_t count, const char *fmt, va_list va);
int snprintf_(char *buf, size_t count, const char *fmt, ...);
int sprintf_(char *buf, const char *fmt, ...);

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
	return vsnprintf_(buf, cap, fmt, ap);
}

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf_(buf, cap, fmt, ap);
	va_end(ap);
	return n;
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf_(buf, (size_t)-1, fmt, ap);
	va_end(ap);
	return n;
}
