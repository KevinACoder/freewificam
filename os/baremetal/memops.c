/*
 * @file   memops.c
 * @brief  The handful of C library functions a freestanding build must supply.
 *
 * The image links -nostdlib, so nothing provides memcpy/memset/memcmp/
 * memmove/strlen. The compiler itself emits calls to these for struct copies
 * and pattern loops, so the link fails without them. The RTOS kernel is one
 * such caller (TCB zeroing, queue item copies).
 *
 * memcpy/memset are word-at-a-time (head/tail bytes + aligned u64 middle):
 * the original byte loops were fine for struct copies, but a bulk-transfer
 * path measured ~4 MB/s slower through them than through the word-at-a-time
 * form. The MMU maps the image region as Normal cacheable, and the A55
 * tolerates unaligned u64 access, so the alignment juggling below is about
 * speed, not correctness. memmove/memcmp/strlen stay byte loops - nothing
 * hot uses them.
 */

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	size_t head = (size_t)(-(uintptr_t)d) & (sizeof(uint64_t) - 1);
	size_t tail;
	size_t i;

	if (head > n)
		head = n;
	for (i = 0; i < head; i++)
		d[i] = s[i];
	tail = (n - head) & (sizeof(uint64_t) - 1);
	{
		uint64_t *dw = (uint64_t *)(d + head);
		const uint64_t *sw = (const uint64_t *)(s + head);
		size_t words = (n - head) / sizeof(uint64_t);

		for (i = 0; i < words; i++)
			dw[i] = sw[i];
	}
	for (i = n - tail; i < n; i++)
		d[i] = s[i];
	return dest;
}

void *memset(void *dest, int value, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	unsigned char v = (unsigned char)value;
	uint64_t pattern = v;
	size_t head = (size_t)(-(uintptr_t)d) & (sizeof(uint64_t) - 1);
	size_t tail;
	size_t i;

	pattern |= pattern << 8;
	pattern |= pattern << 16;
	pattern |= pattern << 32;
	if (head > n)
		head = n;
	for (i = 0; i < head; i++)
		d[i] = v;
	tail = (n - head) & (sizeof(uint64_t) - 1);
	{
		uint64_t *dw = (uint64_t *)(d + head);
		size_t words = (n - head) / sizeof(uint64_t);

		for (i = 0; i < words; i++)
			dw[i] = pattern;
	}
	for (i = n - tail; i < n; i++)
		d[i] = v;
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
