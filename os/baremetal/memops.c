/*
 * @file   memops.c
 * @brief  The handful of C library functions a freestanding build must supply.
 *
 * The image links -nostdlib, so nothing provides memcpy/memset/memcmp/
 * memmove/strlen. The compiler itself emits calls to these for struct copies
 * and pattern loops, so the link fails without them.
 *
 * These are deliberately simple byte loops rather than word-at-a-time
 * versions. Two reasons: the compiler recognises these patterns and emits
 * efficient inline code anyway, and a hand-optimised version that mishandles
 * unaligned access would be far more expensive to debug than it saves. Note
 * that memset/memcpy here must tolerate unaligned pointers - the MMU maps
 * the image region as Normal cacheable memory precisely so they can.
 */

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	size_t i;

	for (i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return dest;
}

void *memset(void *dest, int value, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	size_t i;

	for (i = 0; i < n; i++) {
		d[i] = (unsigned char)value;
	}
	return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;

	if (d == s || n == 0U) {
		return dest;
	}
	if (d < s) {
		size_t i;

		for (i = 0; i < n; i++) {
			d[i] = s[i];
		}
	} else {
		size_t i = n;

		while (i > 0U) {
			i--;
			d[i] = s[i];
		}
	}
	return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t n)
{
	const unsigned char *a = (const unsigned char *)lhs;
	const unsigned char *b = (const unsigned char *)rhs;
	size_t i;

	for (i = 0; i < n; i++) {
		if (a[i] != b[i]) {
			return (int)a[i] - (int)b[i];
		}
	}
	return 0;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (s1[i] != s2[i] || s1[i] == '\0') {
			return (int)(unsigned char)s1[i] -
			       (int)(unsigned char)s2[i];
		}
	}
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n] != '\0') {
		n++;
	}
	return n;
}

size_t strnlen(const char *s, size_t maxlen)
{
	size_t n = 0;

	while (n < maxlen && s[n] != '\0') {
		n++;
	}
	return n;
}

void *memchr(const void *s, int c, size_t n)
{
	const unsigned char *p = s;
	unsigned char b = (unsigned char)c;

	for (size_t i = 0; i < n; i++) {
		if (p[i] == b) {
			return (void *)(uintptr_t)&p[i];
		}
	}
	return 0;
}

int strcmp(const char *s1, const char *s2)
{
	while (*s1 != '\0' && *s1 == *s2) {
		s1++;
		s2++;
	}
	return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

char *strchr(const char *s, int c)
{
	char b = (char)c;

	for (; *s != '\0'; s++) {
		if (*s == b) {
			return (char *)(uintptr_t)s;
		}
	}
	return (b == '\0') ? (char *)(uintptr_t)s : 0;
}

char *strncpy(char *dest, const char *src, size_t n)
{
	size_t i = 0;

	for (; i < n && src[i] != '\0'; i++) {
		dest[i] = src[i];
	}
	for (; i < n; i++) {
		dest[i] = '\0';
	}
	return dest;
}

char *strstr(const char *haystack, const char *needle)
{
	size_t nlen = strlen(needle);

	if (nlen == 0) {
		return (char *)(uintptr_t)haystack;
	}
	for (; *haystack != '\0'; haystack++) {
		if (strncmp(haystack, needle, nlen) == 0) {
			return (char *)(uintptr_t)haystack;
		}
	}
	return 0;
}

char *strrchr(const char *s, int c)
{
	const char *last = 0;
	char b = (char)c;

	for (; *s != '\0'; s++) {
		if (*s == b) {
			last = s;
		}
	}
	return (b == '\0') ? (char *)(uintptr_t)s : (char *)(uintptr_t)last;
}
