/*
 * @file   minilibc.h
 * @brief  The freestanding C surface this image provides for itself.
 *
 * Two groups: the string/memory primitives (defined in memops.c - the
 * compiler emits calls to them for struct copies and pattern loops, so
 * declarations must be visible wherever they are used) and the printf
 * integer subset (defined in minilibc.c).
 */

#ifndef FREEWEBCAM_MINILIBC_H
#define FREEWEBCAM_MINILIBC_H

#include <stddef.h>

/* --- memops.c -------------------------------------------------------------- */

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memchr(const void *s, int c, size_t n);
int memcmp(const void *lhs, const void *rhs, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strncpy(char *dest, const char *src, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int atoi(const char *s);
long strtol(const char *s, char **endptr, int base);

/* --- minilibc.c: printf integer subset -------------------------------------- */

#include <stdarg.h>

/* Supported: %c %s %d %i %u %x %X %p %%; flags '-' and '0'; field width as
 * decimal digits or '*'; precision '.' digits or '*'; length 'l', 'll', 'z'.
 * Floating-point and any other conversion prints the raw spec characters
 * (C99 degraded behaviour). Output is truncated but always NUL-terminated;
 * the return value is the length of the complete output. */
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
int snprintf(char *buf, size_t cap, const char *fmt, ...);

#endif /* FREEWEBCAM_MINILIBC_H */
