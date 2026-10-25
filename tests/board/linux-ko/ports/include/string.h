/*
 * @file
 * @brief C string names for the kernel build.
 *
 * The net80211 world must not see kernel headers that drag in the Linux
 * networking/assembly vocabulary (linux/kernel.h alone defines the assembler
 * ALIGN in the BSD world's namespace).  So this face declares the handful of
 * exported string functions instead of including <linux/string.h>; size_t
 * comes from <linux/types.h>, the one kernel header the integer model needs.
 */

#ifndef _WLAN_LINUX_STRING_H_
#define _WLAN_LINUX_STRING_H_

#include <linux/types.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

static inline __attribute__((__unused__)) size_t
strlcpy(char *dst, const char *src, size_t size)
{
	size_t len = strlen(src);

	if (size != 0) {
		size_t n = len < size - 1 ? len : size - 1;

		memcpy(dst, src, n);
		dst[n] = '\0';
	}
	return len;
}

#endif /* _WLAN_LINUX_STRING_H_ */
