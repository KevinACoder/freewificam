/*
 * @file   minilibc.c
 * @brief  Freestanding libc completions: the formatter binding and the
 *         integer/string helpers the image's callers need.
 *
 * The formatting engine is mpaland/printf, vendored at components/printf
 * (MIT, archived upstream at its final commit; see that directory's
 * PROVENANCE.md). It is a full C99 integer formatter: flags, width and
 * precision (including their '*' forms), the h/hh/l/ll/z/j/t length
 * modifiers and %d %i %u %x %X %o %b %c %s %p %%. Float and exponential
 * rendering are compiled out at the build rule - the image is
 * -mgeneral-regs-only and has no FP state - so those specifiers degrade like
 * any unknown one.
 *
 * Binding, not aliasing: printf.h is deliberately NOT included here. Its
 * standard-name #define aliases (printf -> printf_, ...) would collide with
 * the standard-named wrappers this file defines and the rest of the image
 * links against, so the underscore realization is declared directly.
 *
 * The helpers below (atoi/strtol/strtoul/qsort/sscanf/...) are first-party:
 * the imported engine covers formatting only. Output only fills the caller's
 * buffer; which sink and which lock it lands in is the caller's business.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "minilibc.h"

/* The vendored engine (components/printf/printf.c). */
extern int vsnprintf_(char *buf, size_t count, const char *fmt, va_list va);

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

int atoi(const char *s)
{
	int v = 0;
	int sign = 1;

	while (*s == ' ' || (*s >= '\t' && *s <= '\r')) {
		s++;
	}
	if (*s == '-') {
		sign = -1;
		s++;
	} else if (*s == '+') {
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (*s - '0');
		s++;
	}
	return sign * v;
}

/* CherryUSB's lsusb (usbh_core.c) parses "bus:dev" and "vid:pid" arguments
 * with strtol in bases 10 and 16 (S5/P5-a link surface). Base 0 honours the
 * 0x/0X prefix; 2..36 are the C semantics; anything else fails without
 * consuming digits. Whitespace/sign skipping matches atoi above. */
long strtol(const char *s, char **endptr, int base)
{
	long v = 0;
	int sign = 1;
	int d;

	while (*s == ' ' || (*s >= '\t' && *s <= '\r')) {
		s++;
	}
	if (*s == '-') {
		sign = -1;
		s++;
	} else if (*s == '+') {
		s++;
	}
	if ((base == 0 || base == 16) && s[0] == '0' &&
	    (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		base = (base == 0) ? 16 : base;
	} else if (base == 0) {
		base = (*s == '0') ? 8 : 10;
	}
	for (;;) {
		if (*s >= '0' && *s <= '9') {
			d = *s - '0';
		} else if (*s >= 'a' && *s <= 'z') {
			d = *s - 'a' + 10;
		} else if (*s >= 'A' && *s <= 'Z') {
			d = *s - 'A' + 10;
		} else {
			break;
		}
		if (d >= base) {
			break;
		}
		v = v * base + d;
		s++;
	}
	if (endptr != 0) {
		*(char **)endptr = (char *)s;
	}
	return sign * v;
}

/* --- string completions (strcpy/strcat/strtoul) ---------------------------- */

char *strcpy(char *dest, const char *src)
{
	char *d = dest;

	while ((*d++ = *src++) != '\0') {
	}
	return dest;
}

char *strcat(char *dest, const char *src)
{
	char *d = dest;

	while (*d != '\0') {
		d++;
	}
	while ((*d++ = *src++) != '\0') {
	}
	return dest;
}

unsigned long strtoul(const char *s, char **endptr, int base)
{
	unsigned long acc = 0UL;
	int d;

	while (*s == ' ' || (*s >= '\t' && *s <= '\r')) {
		s++;
	}
	if ((base == 0 || base == 16) && s[0] == '0' &&
	    (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		base = (base == 0) ? 16 : base;
	} else if (base == 0) {
		base = (*s == '0') ? 8 : 10;
	}
	for (;;) {
		if (*s >= '0' && *s <= '9') {
			d = *s - '0';
		} else if (*s >= 'a' && *s <= 'z') {
			d = *s - 'a' + 10;
		} else if (*s >= 'A' && *s <= 'Z') {
			d = *s - 'A' + 10;
		} else {
			break;
		}
		if (d >= base) {
			break;
		}
		acc = acc * (unsigned long)base + (unsigned long)d;
		s++;
	}
	if (endptr != NULL) {
		*(const char **)endptr = s;
	}
	return acc;
}

#if defined(CONFIG_LIBC_COMPLETION)

/* --- libc completion (S7a/P7a-1) -------------------------------------------
 *
 * The wpa_supplicant compile set references four symbols the surface above
 * does not carry; the prior project satisfied them from its console glue
 * (wlan_console.c) - they move here, the declared freestanding C surface,
 * so every config links the same set. None of them touches the heap (the
 * heap-backed strdup lives in the wpa port layer instead).
 */

int abs(int n)
{
	return n < 0 ? -n : n;
}

/* heap sort over byte cells: worst-case bounds, no recursion, no
 * allocation - the bss list sorting is the only consumer in the wpa set */
static void mlc_heap_siftdown(char *b, size_t root, size_t n, size_t width,
			  int (*cmp)(const void *, const void *))
{
	for (;;) {
		size_t child = 2 * root;
		size_t k;

		if (child > n) {
			break;
		}
		if (child + 1 <= n &&
		    cmp(b + (child - 1) * width, b + child * width) < 0) {
			child++;
		}
		if (cmp(b + (root - 1) * width, b + (child - 1) * width) >= 0) {
			break;
		}
		for (k = 0; k < width; k++) {
			char t = b[(root - 1) * width + k];

			b[(root - 1) * width + k] = b[(child - 1) * width + k];
			b[(child - 1) * width + k] = t;
		}
		root = child;
	}
}

void qsort(void *base, size_t nmemb, size_t width,
	   int (*cmp)(const void *, const void *))
{
	char *b = base;
	size_t n = nmemb;
	size_t i;

	if (n < 2 || width == 0) {
		return;
	}
	for (i = n / 2; i > 0; i--) {
		mlc_heap_siftdown(b, i, n, width, cmp);
	}
	for (i = n; i > 1; i--) {
		size_t k;

		for (k = 0; k < width; k++) {
			char t = b[k];

			b[k] = b[(i - 1) * width + k];
			b[(i - 1) * width + k] = t;
		}
		mlc_heap_siftdown(b, 1, i - 1, width, cmp);
	}
}

/* A scanf subset: the compiled wpa files only reach it through a config
 * path this image does not run (sched_scan_plans), but the symbol must
 * resolve. Handles %d/%i/%u/%x/%s plus literals, spaces and %%; anything
 * else stops the parse (the prior project's proven shape). */
static int mlc_hexval(int c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

int sscanf(const char *str, const char *fmt, ...)
{
	va_list ap;
	int assigned = 0;
	const char *s = str;

	va_start(ap, fmt);
	for (; *fmt != '\0'; fmt++) {
		if (*fmt != '%') {
			if (*fmt == ' ' || *fmt == '\t') {
				while (*s == ' ' || *s == '\t') {
					s++;
				}
				continue;
			}
			if (*s == *fmt) {
				s++;
			} else {
				break;
			}
			continue;
		}
		fmt++;
		if (*fmt == '%') {
			if (*s == '%') {
				s++;
			} else {
				break;
			}
			continue;
		}
		{
			int suppress = (*fmt == '*');

			if (suppress) {
				fmt++;
			}
			while (*fmt >= '0' && *fmt <= '9') {
				fmt++;
			}
			if (*fmt == 'l' || *fmt == 'h') {
				fmt++;
			}
			if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' ||
			    *fmt == 'x' || *fmt == 's') {
				int neg = 0;
				unsigned long v = 0;
				int any = 0;
				char *out;

				if (*fmt == 's') {
					out = suppress ? NULL :
					      va_arg(ap, char *);
					while (*s == ' ' || *s == '\t') {
						s++;
					}
					while (*s != '\0' && *s != ' ' &&
					       *s != '\t') {
						if (out != NULL) {
							*out++ = *s;
						}
						s++;
						any = 1;
					}
					if (out != NULL) {
						*out = '\0';
					}
				} else {
					int hex = (*fmt == 'x');

					while (*s == ' ' || *s == '\t') {
						s++;
					}
					if (*s == '-') {
						neg = 1;
						s++;
					}
					for (;;) {
						int d;

						if (hex) {
							d = mlc_hexval(*s);
						} else if (*s >= '0' &&
							   *s <= '9') {
							d = *s - '0';
						} else {
							break;
						}
						if (d < 0) {
							break;
						}
						v = v * (hex ? 16U : 10U) +
						    (unsigned long)d;
						s++;
						any = 1;
					}
					if (!suppress) {
						out = va_arg(ap, char *);
						if (neg) {
							*(long *)out =
								-(long)v;
						} else {
							*(unsigned long *)
								out = v;
						}
					}
				}
				if (!any) {
					break;
				}
				if (!suppress) {
					assigned++;
				}
				continue;
			}
			break;
		}
	}
	va_end(ap);
	return assigned;
}

int strcasecmp(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		int ca = (unsigned char)*a;
		int cb = (unsigned char)*b;
		int d;

		if (ca >= 'A' && ca <= 'Z') {
			ca += 'a' - 'A';
		}
		if (cb >= 'A' && cb <= 'Z') {
			cb += 'a' - 'A';
		}
		d = ca - cb;
		if (d != 0) {
			return d;
		}
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

/* newlib runtime compat: the ctype macros read this table, and the
 * stdout-setup path references the reent stdio. The reent structure
 * stays all-zero (stdout == NULL): the printf family above ignores the
 * stream argument, so nothing ever dereferences the FILE pointers -
 * these only satisfy the references (the prior project's proven shape). */
extern const char _ctype_[];
const char _ctype_[257] = {
	/* [0] is EOF */
	040,
	/* 1..31: control */
	040, 040, 040, 040, 040, 040, 040, 040, 010, 010, 010, 010, 010,
	040, 040, 040, 040, 040, 040, 040, 040, 040, 040, 040, 040, 040,
	040, 040, 040, 040, 040, 040,
	01, 01, 01, 01,
	/* 91..96: punctuation */
	020, 020, 020, 020, 020, 020,
	/* 97..122: lowercase (a-f also hex) */
	02 + 0100, 02 + 0100, 02 + 0100, 02 + 0100, 02 + 0100, 02 + 0100,
	02, 02, 02, 02, 02, 02, 02, 02, 02, 02, 02, 02, 02, 02, 02, 02,
	02, 02, 02, 02,
	/* 123..127: punctuation + DEL */
	020, 020, 020, 020, 040,
	/* 128..255: undefined in this table */
	0
};

int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
	/* the console is unbuffered; nothing to configure */
	(void) stream;
	(void) buf;
	(void) mode;
	(void) size;
	return 0;
}

#include <sys/reent.h>

static struct _reent mlc_reent;
struct _reent *_impure_ptr = &mlc_reent;

/* fatal park: the wpa world's wpabuf_overflow lands here; print via the
 * console sink and spin (no unwinding exists in this image) */
void abort(void)
{
	board_console_write_raw("abort() called\n", 15);
	for (;;) {
	}
}
#endif /* CONFIG_LIBC_COMPLETION */
