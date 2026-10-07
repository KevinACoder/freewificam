/*
 * @file
 * @brief Kernel services shell: panic, delay, printf.
 */

#ifndef _SYS_SYSTM_H_
#define _SYS_SYSTM_H_

#include <sys/cdefs.h>
#include "types.h"
#include <sys/intr.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


/* NetBSD calls panic with the arguments in a second pair of
 * parentheses: panic(("message %d", x)). */

void delay(unsigned int us);
#define DELAY(x) delay(x)

void get_random_bytes(void *, size_t);

int kprintf(const char *fmt, ...) __printflike(1, 2);
#define printf kprintf
#define uprintf kprintf
#define aprint_naive kprintf
#define aprint_verbose kprintf
#define aprint_debug kprintf

#define NBBY 8
static inline void setbit(volatile unsigned char *p, unsigned int n) {
	p[n / NBBY] |= (unsigned char) (1 << (n % NBBY));
}
static inline void clrbit(volatile unsigned char *p, unsigned int n) {
	p[n / NBBY] &= (unsigned char) ~(1 << (n % NBBY));
}
static inline int isset(const volatile unsigned char *p, unsigned int n) {
	return p[n / NBBY] & (1 << (n % NBBY));
}
static inline int isclr(const volatile unsigned char *p, unsigned int n) {
	return !(p[n / NBBY] & (1 << (n % NBBY)));
}

ipl_t splnet(void);
void splx(ipl_t);

/* Process-context sleeps. timo is in ticks (<= 0 waits forever); the
 * wait honours the timeout, dropping the port serializer around it so
 * the interrupt worker can run while a firmware command is pending. */
#define PCATCH 0x100
int tsleep(void *ident, int pri, const char *wmesg, int timo);
void wakeup(void *ident);
void wakeup_one(void *ident);

int uimin(int a, int b);
int uimax(int a, int b);

int copyin(const void *, void *, size_t);
int copyout(const void *, void *, size_t);
int copystr(const void *, void *, size_t, size_t *);


static inline void *explicit_memset(void *b, int c, size_t len) {
    volatile uint8_t *p = b;
    size_t i;
    for (i = 0; i < len; i++) p[i] = (uint8_t)c;
    return b;
}
static inline int consttime_memequal(const void *a, const void *b, size_t len) {
    const volatile uint8_t *x = a, *y = b;
    uint8_t diff = 0;
    size_t i;
    for (i = 0; i < len; i++) diff |= (uint8_t)(x[i] ^ y[i]);
    return diff == 0;
}

/* only the imported crypto self-tests call this; no-op */
static inline void
hexdump(int (*print_fn)(const char *, ...) __attribute__((unused)),
    const char *tag, const void *buf, int len)
{
	(void) tag;
	(void) buf;
	(void) len;
}

#endif /* _SYS_SYSTM_H_ */
