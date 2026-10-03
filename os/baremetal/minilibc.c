/*
 * @file   minilibc.c
 * @brief  printf integer subset, hand-written (no third-party dependency,
 *         fully auditable; SPEC v3 §3.2.3).
 *
 * Supported: %c %s %d %i %u %x %X %p %%; flags '-' (left align) and '0'
 * (zero fill); field width as decimal digits or '*'; precision '.' digits or
 * '*'; length 'l', 'll', 'z' (all 64-bit on this LP64 target). Anything else
 * - floating-point included - prints the raw spec characters (C99 degraded
 * behaviour). Output is truncated to the buffer but always NUL-terminated;
 * the return value is the length of the COMPLETE output, snprintf-style.
 *
 * Output only fills the buffer; what happens to it (which sink, which lock)
 * is the caller's business. Integer division uses the A64 hardware UDIV
 * (A55 has SDIV/UDIV; no libgcc helper is involved - the build self-check
 * asserts none appears).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "minilibc.h"
#include "log.h"

struct obuf {
	char *buf;
	size_t cap;
	size_t len;	/* length of the complete output, truncation aside */
};

static void ob_ch(struct obuf *o, char c)
{
	if (o->cap > 0u && o->len < o->cap - 1u) {
		o->buf[o->len] = c;
	}
	o->len++;
}

static void ob_str(struct obuf *o, const char *s, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		ob_ch(o, s[i]);
	}
}

static void ob_pad(struct obuf *o, size_t n, char c)
{
	while (n-- > 0u) {
		ob_ch(o, c);
	}
}

/* Unsigned to ASCII (reversed fill), returns length. */
static size_t u64_to_str(char *tmp, uint64_t v, unsigned base, bool upper)
{
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	size_t n = 0;
	size_t i;

	do {
		tmp[n++] = digits[v % base];
		v /= base;
	} while (v != 0u);

	for (i = 0; i < n / 2u; i++) {
		char t = tmp[i];

		tmp[i] = tmp[n - 1u - i];
		tmp[n - 1u - i] = t;
	}
	return n;
}

/* Prepend precision zeros to a digit string in place. Clamps at cap-1
 * digits (a precision larger than that is truncated - a documented
 * deviation; this image's own callers use small precisions). */
static size_t apply_prec(char *tmp, size_t n, int prec, size_t cap)
{
	size_t extra;

	if (prec <= 0 || (size_t)prec <= n) {
		return n;
	}
	extra = (size_t)prec - n;
	if (extra > cap - 1u - n) {
		extra = cap - 1u - n;
	}
	memmove(tmp + extra, tmp, n);
	memset(tmp, '0', extra);
	return n + extra;
}

/* Field emit for numbers: prefix (sign / 0x), digits, width padding. The
 * '0' flag zero-fills after the prefix; '-' (left align) wins over it. */
static void ob_num(struct obuf *o, const char *digits, size_t dlen,
		   const char *prefix, int width, bool minus, bool zero)
{
	size_t plen = strlen(prefix);
	size_t total = dlen + plen;
	size_t pad = (width > 0 && (size_t)width > total)
			? (size_t)width - total : 0u;

	if (minus) {
		ob_str(o, prefix, plen);
		ob_str(o, digits, dlen);
		ob_pad(o, pad, ' ');
	} else if (zero) {
		ob_str(o, prefix, plen);
		ob_pad(o, pad, '0');
		ob_str(o, digits, dlen);
	} else {
		ob_pad(o, pad, ' ');
		ob_str(o, prefix, plen);
		ob_str(o, digits, dlen);
	}
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
	struct obuf o = { buf, cap, 0u };
	const char *p = fmt;

	while (*p != '\0') {
		const char *spec;
		bool minus = false;
		bool zero = false;
		int width = 0;
		int prec = -1;
		int lcount = 0;
		bool z_len = false;
		char conv;

		if (*p != '%') {
			ob_ch(&o, *p++);
			continue;
		}
		spec = p++;

		/* flags */
		for (;; p++) {
			if (*p == '-') {
				minus = true;
			} else if (*p == '0') {
				zero = true;
			} else {
				break;
			}
		}
		/* width */
		if (*p == '*') {
			width = va_arg(ap, int);
			if (width < 0) {
				minus = true;
				width = -width;
			}
			p++;
		} else {
			while (*p >= '0' && *p <= '9') {
				width = width * 10 + (*p - '0');
				p++;
			}
		}
		/* precision */
		if (*p == '.') {
			p++;
			prec = 0;
			if (*p == '*') {
				prec = va_arg(ap, int);
				p++;
			} else {
				while (*p >= '0' && *p <= '9') {
					prec = prec * 10 + (*p - '0');
					p++;
				}
			}
		}
		/* length */
		while (*p == 'l') {
			lcount++;
			p++;
		}
		if (*p == 'z') {
			z_len = true;
			p++;
		}
		conv = *p;

		switch (conv) {
		case '%':
			ob_ch(&o, '%');
			p++;
			break;
		case 'c': {
			char c = (char)va_arg(ap, int);
			size_t pad = (width > 1) ? (size_t)width - 1u : 0u;

			if (!minus) {
				ob_pad(&o, pad, ' ');
			}
			ob_ch(&o, c);
			if (minus) {
				ob_pad(&o, pad, ' ');
			}
			p++;
			break;
		}
		case 's': {
			const char *s = va_arg(ap, const char *);
			size_t n;
			size_t pad;

			if (s == 0) {
				s = "(null)";
			}
			n = strlen(s);
			if (prec >= 0 && (size_t)prec < n) {
				n = (size_t)prec;
			}
			pad = (width > 0 && (size_t)width > n)
				? (size_t)width - n : 0u;
			if (!minus) {
				ob_pad(&o, pad, ' ');
			}
			ob_str(&o, s, n);
			if (minus) {
				ob_pad(&o, pad, ' ');
			}
			p++;
			break;
		}
		case 'd':
		case 'i': {
			long long v = (lcount > 0) ? va_arg(ap, long long)
						   : va_arg(ap, int);
			bool neg = v < 0;
			uint64_t m = neg ? (uint64_t)0u - (uint64_t)v
					 : (uint64_t)v;
			char tmp[24];
			size_t n = u64_to_str(tmp, m, 10u, false);

			if (prec == 0 && m == 0u) {
				n = 0;	/* "%.0d" of 0 prints nothing */
			}
			n = apply_prec(tmp, n, prec, sizeof(tmp));
			ob_num(&o, tmp, n, neg ? "-" : "", width, minus, zero);
			p++;
			break;
		}
		case 'u':
		case 'x':
		case 'X': {
			unsigned long long v;
			unsigned base = (conv == 'u') ? 10u : 16u;
			char tmp[24];
			size_t n;

			if (z_len) {
				v = va_arg(ap, size_t);
			} else if (lcount > 0) {
				v = va_arg(ap, unsigned long long);
			} else {
				v = va_arg(ap, unsigned int);
			}
			n = u64_to_str(tmp, v, base, conv == 'X');
			if (prec == 0 && v == 0u) {
				n = 0;
			}
			n = apply_prec(tmp, n, prec, sizeof(tmp));
			ob_num(&o, tmp, n, "", width, minus, zero);
			p++;
			break;
		}
		case 'p': {
			void *pv = va_arg(ap, void *);
			char tmp[24];
			size_t n = u64_to_str(tmp, (uint64_t)pv, 16u, false);

			ob_num(&o, tmp, n, "0x", width, minus, zero);
			p++;
			break;
		}
		case '\0':
			/* format ended mid-spec: emit what was consumed */
			break;
		default: {
			/* unsupported conversion: raw spec characters */
			const char *q = spec;

			while (q <= p) {
				ob_ch(&o, *q++);
			}
			p++;
			break;
		}
		}
	}

	if (cap > 0u) {
		buf[o.len < cap ? o.len : cap - 1u] = '\0';
	}
	return (o.len > (size_t)0x7fffffff) ? 0x7fffffff : (int)o.len;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, cap, fmt, ap);
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

/* --- S7b/netutils 补齐（FR-10 缺口登记：strcpy/strcat/strtoul） ------------- */

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
