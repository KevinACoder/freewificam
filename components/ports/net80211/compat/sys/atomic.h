/*
 * @file
 * @brief atomic(9) shell over compiler builtins.
 */

#ifndef _COMPAT_SYS_ATOMIC_H_
#define _COMPAT_SYS_ATOMIC_H_

#include <sys/cdefs.h>
#include <sys/types.h>

static inline void atomic_inc_uint(volatile unsigned int *p) {
	__atomic_add_fetch(p, 1, __ATOMIC_RELAXED);
}

static inline void atomic_dec_uint(volatile unsigned int *p) {
	__atomic_sub_fetch(p, 1, __ATOMIC_RELAXED);
}

static inline unsigned int atomic_inc_uint_nv(volatile unsigned int *p) {
	return __atomic_add_fetch(p, 1, __ATOMIC_RELAXED);
}

static inline unsigned int atomic_dec_uint_nv(volatile unsigned int *p) {
	return __atomic_sub_fetch(p, 1, __ATOMIC_RELAXED);
}

static inline uint32_t atomic_inc_32_nv(volatile uint32_t *p) {
	return __atomic_add_fetch(p, 1, __ATOMIC_RELAXED);
}

static inline uint32_t atomic_dec_32_nv(volatile uint32_t *p) {
	return __atomic_sub_fetch(p, 1, __ATOMIC_RELAXED);
}

static inline void atomic_inc_32(volatile uint32_t *p) {
	__atomic_add_fetch(p, 1, __ATOMIC_RELAXED);
}

static inline void atomic_dec_32(volatile uint32_t *p) {
	__atomic_sub_fetch(p, 1, __ATOMIC_RELAXED);
}

static inline void *atomic_swap_ptr(volatile void *p, void *v) {
	return __atomic_exchange_n((void *volatile *) p, v, __ATOMIC_RELAXED);
}

static inline void atomic_store_relaxed(volatile unsigned int *p,
	unsigned int v) {
	__atomic_store_n(p, v, __ATOMIC_RELAXED);
}

static inline unsigned int atomic_load_relaxed(const volatile unsigned int *p) {
	return __atomic_load_n((unsigned int *) p, __ATOMIC_RELAXED);
}

/* NetBSD's convention: the previous value comes back, so callers test it
 * against the expected one to learn whether they won the race */
static inline unsigned int atomic_cas_uint(volatile unsigned int *p,
	unsigned int old, unsigned int new) {
	__atomic_compare_exchange_n(p, &old, new, false,
	    __ATOMIC_RELAXED, __ATOMIC_RELAXED);
	return old;
}

/* audio(4)'s track lock is an xchg spinlock: atomic_swap_uint stores the
 * new value and returns the previous one (upstream shape), and the
 * release/acquire pair brackets the critical section.  The builtins carry
 * the ordering; the membar names are the architectural form. */
static inline unsigned int atomic_swap_uint(volatile unsigned int *p,
	unsigned int v) {
	return __atomic_exchange_n(p, v, __ATOMIC_ACQ_REL);
}

static inline void atomic_store_release(volatile unsigned int *p,
	unsigned int v) {
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

static inline void membar_acquire(void) {
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	__asm__ volatile("dmb ish" ::: "memory");
}

static inline void membar_release(void) {
	__atomic_thread_fence(__ATOMIC_RELEASE);
	__asm__ volatile("dmb ish" ::: "memory");
}

static inline void membar_sync(void) {
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	__asm__ volatile("dmb ish" ::: "memory");
}

#endif /* _COMPAT_SYS_ATOMIC_H_ */
