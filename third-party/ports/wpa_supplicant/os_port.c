/*
 * @file
 * @brief os_* port for the wpa_supplicant.
 *
 * Memory and string primitives come from the libc macros in
 * src/utils/os.h (OS_NO_C_LIB_DEFINES stays undefined); this file
 * carries the functions os.h always expects from the port plus the
 * bits the freestanding environment has no wrapper for. The clock
 * reads the AArch64 generic timer directly, so no libc time stubs are
 * involved. malloc/free/calloc/realloc are the tlsf-backed heap
 * bindings (os/threadx/heap.c).
 *
 * Ported from the prior project's os_port.c ([S7a] §3.3.1): ksleep
 * became wpa_msleep, implemented here on the CMSIS tick.
 */

#include <includes.h>

#include "utils/os.h"
#include <stdint.h>
#include <string.h>

#include "cmsis_os2.h"
#include "generic_timer.h"

#include "wpa_port_api.h"

/* A scanf subset for the wpa world: the compiled files only reach it
 * through a config path this image does not run (sched_scan_plans), but
 * the symbol must resolve. Handles %d/%i/%u/%x/%s plus literals, spaces
 * and %%; anything else stops the parse (the prior project's proven
 * shape). WPA_CFG renames the token (sscanf=wpa_port_sscanf) so the
 * linuxkpi/libc sscanf stays the only plain one in combined images. */
static int wpa_port_hexval(int c)
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
							d = wpa_port_hexval(*s);
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

/* strdup is heap-backed, so it lives in the wpa port layer rather than
 * the base minilibc (which must stay linkable in the heap-less
 * baremetal configs); the prior project satisfied it from its console
 * glue the same way. utils/os.h maps os_strdup onto this. */
extern void *malloc(size_t size);

char *strdup(const char *s) {
	size_t n = strlen(s) + 1;
	char *p = malloc(n);

	if (p != NULL) {
		memcpy(p, s, n);
	}
	return p;
}

void wpa_msleep(unsigned int ms) {
	uint32_t ticks;

	/* osDelay counts ticks, not milliseconds: convert through the
	 * tick rate even at the identity 1000 Hz ([S7a] §3.3.2) */
	ticks = (uint32_t) ((uint64_t) ms *
	    (uint64_t) osKernelGetTickFreq() / 1000U);
	if (ticks == 0U) {
		ticks = 1U;
	}
	(void) osDelay(ticks);
}

void os_sleep(os_time_t sec, os_time_t usec) {
	unsigned int ms;

	ms = (unsigned int) (sec * 1000 + usec / 1000);
	if (ms == 0 && (sec != 0 || usec != 0)) {
		ms = 1;
	}
	wpa_msleep(ms);
}

static void counter_now(struct os_time *t) {
	uint64_t freq, ticks, us;

	freq = gtimer_freq_get();
	ticks = gtimer_count_get();
	us = ticks * 1000000ULL / freq;
	t->sec = (os_time_t) (us / 1000000ULL);
	t->usec = (os_time_t) (us % 1000000ULL);
}

int os_get_time(struct os_time *t) {
	/* no wall clock on this port; the monotonic source stands in */
	counter_now(t);
	return 0;
}

int os_get_reltime(struct os_reltime *t) {
	counter_now((struct os_time *) t);
	return 0;
}

int os_mktime(int year, int month, int day, int hour, int min, int sec,
    os_time_t *t) {
	static const int days_before_month[12] = {
		0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
	};
	int leap;
	long days;

	if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) {
		return -1;
	}
	leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	days = 365 * (year - 1970) + (year - 1969) / 4
	    - (year - 1901) / 100 + (year - 1601) / 400
	    + days_before_month[month - 1] + (month > 2 && leap ? 1 : 0)
	    + day - 1;
	*t = (os_time_t) days * 86400 + hour * 3600 + min * 60 + sec;
	return 0;
}

int os_gmtime(os_time_t t, struct os_tm *tm) {
	/* not used on the PSK-only path; timestamps stay relative */
	(void) t;
	(void) tm;
	return -1;
}

int os_daemonize(const char *pid_file) {
	(void) pid_file;
	return -1;
}

void os_daemonize_terminate(const char *pid_file) {
	(void) pid_file;
}

int os_get_random(unsigned char *buf, size_t len) {
	/* no entropy source behind this port: stir the free-running
	 * counter into a xorshift state; good enough for the SNonce
	 * ([S7a] N9/open question 4 - known-weak, recorded) */
	static uint64_t state;
	size_t i;

	if (state == 0) {
		state = gtimer_count_get();
		state |= 1;
	}
	for (i = 0; i < len; i++) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		buf[i] = (unsigned char) (state >> 24);
	}
	return 0;
}

char *os_rel2abs_path(const char *rel_path) {
	(void) rel_path;
	return NULL;
}

int os_program_init(void) {
	return 0;
}

void os_program_deinit(void) {
}

int os_setenv(const char *name, const char *value, int overwrite) {
	(void) name;
	(void) value;
	(void) overwrite;
	return -1;
}

int os_unsetenv(const char *name) {
	(void) name;
	return -1;
}

char *os_readfile(const char *name, size_t *len) {
	(void) name;
	(void) len;
	return NULL;
}

size_t os_strlcpy(char *dest, const char *src, size_t siz) {
	size_t srclen;

	srclen = strlen(src);
	if (siz != 0) {
		size_t n = srclen < siz - 1 ? srclen : siz - 1;

		memcpy(dest, src, n);
		dest[n] = '\0';
	}
	return srclen;
}

void *os_memdup(const void *src, size_t len) {
	void *r;

	r = os_malloc(len);
	if (r != NULL) {
		os_memcpy(r, src, len);
	}
	return r;
}

int os_exec(const char *program, const char *arg, int wait_completion) {
	(void) program;
	(void) arg;
	(void) wait_completion;
	return -1;
}

/* os_zalloc is declared in os.h but not provided by the libc macros */
void *os_zalloc(size_t size)
{
	void *r = malloc(size ? size : 1);

	if (r != NULL) {
		memset(r, 0, size);
	}
	return r;
}

int os_memcmp_const(const void *a, const void *b, size_t len)
{
	const uint8_t *x = a;
	const uint8_t *y = b;
	uint8_t diff = 0;
	size_t i;

	for (i = 0; i < len; i++) {
		diff |= (uint8_t) (x[i] ^ y[i]);
	}
	return diff;
}

unsigned long os_random(void)
{
	unsigned char b[4];

	os_get_random(b, sizeof(b));
	return (unsigned long) b[0] | ((unsigned long) b[1] << 8) |
	    ((unsigned long) b[2] << 16) | ((unsigned long) b[3] << 24);
}

/* the toolchain libc has no inet_ntop; robust_av.c formats addresses
 * for debug output only */
#include <stdio.h>

const char *inet_ntop(int af, const void *src, char *dst, unsigned int size)
{
	const unsigned char *a = src;
	int n;

	if (af == AF_INET) {
		n = snprintf(dst, size, "%u.%u.%u.%u",
		    a[0], a[1], a[2], a[3]);
	} else if (af == AF_INET6 && size >= 46) {
		n = snprintf(dst, size,
		    "%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
		    "%02x%02x:%02x%02x:%02x%02x:%02x%02x",
		    a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
		    a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
	} else {
		return NULL;
	}
	return n < 0 || (unsigned) n >= size ? NULL : dst;
}
