/*
 * @file
 * @brief Kernel services shell for the imported stack: print, delay, sleeps,
 * copies and bit helpers.
 *
 * @design kernel-services shim: same declaration face as the component's compat
 * header (which the imported sources were written against). The implementation
 * lives in the port osal: kprintf goes to the port log seam, tsleep/wakeup are
 * the bounded identified waits of osal/cmsis2/osal_cmsis2.c.
 */

#ifndef _WLAN_SYS_SYSTM_H_
#define _WLAN_SYS_SYSTM_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/intr.h>

void delay(unsigned int us);
#define DELAY(x) delay(x)

void get_random_bytes(void *, size_t);

int kprintf(const char *fmt, ...) __printflike(1, 2);
#define printf kprintf
#define uprintf kprintf
#define aprint_naive kprintf
#define aprint_verbose kprintf
#define aprint_debug kprintf

/* The imported drivers give up through panic(); the port prints and stops
 * (host abort semantics; a target build may reset instead). */
void panic(const char *fmt, ...) __printflike(1, 2);

#define NBBY 8

static inline void setbit(volatile unsigned char *p, unsigned int n)
{
	p[n / NBBY] |= (unsigned char) (1 << (n % NBBY));
}

static inline void clrbit(volatile unsigned char *p, unsigned int n)
{
	p[n / NBBY] &= (unsigned char) ~(1 << (n % NBBY));
}

static inline int isset(const volatile unsigned char *p, unsigned int n)
{
	return p[n / NBBY] & (1 << (n % NBBY));
}

static inline int isclr(const volatile unsigned char *p, unsigned int n)
{
	return !(p[n / NBBY] & (1 << (n % NBBY)));
}

ipl_t splnet(void);
void splx(ipl_t);

/* Process-context sleeps. timo is in ticks (<= 0 waits forever); the wait
 * drops the port serializer around the blocking section so the interrupt
 * worker can run while a firmware command is pending. */
#define PCATCH 0x100
int tsleep(void *ident, int pri, const char *wmesg, int timo);
void wakeup(void *ident);
void wakeup_one(void *ident);

int uimin(int a, int b);
int uimax(int a, int b);

int copyin(const void *, void *, size_t);
int copyout(const void *, void *, size_t);
int copystr(const void *, void *, size_t, size_t *);

static inline void *explicit_memset(void *b, int c, size_t len)
{
	volatile uint8_t *p = b;
	size_t i;

	for (i = 0; i < len; i++) {
		p[i] = (uint8_t) c;
	}
	return b;
}

static inline int consttime_memequal(const void *a, const void *b, size_t len)
{
	const volatile uint8_t *x = a, *y = b;
	uint8_t diff = 0;
	size_t i;

	for (i = 0; i < len; i++) {
		diff |= (uint8_t) (x[i] ^ y[i]);
	}
	return diff == 0;
}

/* Only the imported crypto self-tests call this; no-op. */
static inline void
hexdump(int (*print_fn)(const char *, ...) __attribute__((unused)),
    const char *tag, const void *buf, int len)
{
	(void) tag;
	(void) buf;
	(void) len;
}

#endif /* _WLAN_SYS_SYSTM_H_ */
