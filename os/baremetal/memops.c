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

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n] != '\0') {
		n++;
	}
	return n;
}
