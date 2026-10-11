/*
 * @file    host_shim.c
 * @brief   The board layer's two ends for the outlet suite: the wire and the clock.
 *
 * os/baremetal/log.c is compiled into this suite unchanged, so the cases judge
 * the real line assembly, the real stamp and the real repeat gate. Two things
 * the board supplies have to come from here instead: the UART (captured into a
 * buffer the cases read back) and the generic counter the stamp is a function
 * of (advanced by the case, so the stamp is predictable).
 */

#include <stdint.h>
#include <stddef.h>

#include "uart.h"
#include "generic_timer.h"
#include "tick.h"

#define	CAP_MAX	(256u * 1024u)

static char cap[CAP_MAX];
static size_t cap_len;
static uint64_t counter;
static uint32_t counter_freq = 1000000u;	/* 1 MHz: 1 tick = 1 us */

/* ---- the wire ---------------------------------------------------------- */

void uart_early_putc(char c)
{
	if (cap_len < CAP_MAX - 1u) {
		cap[cap_len++] = c;
		cap[cap_len] = '\0';
	}
}

void uart_early_puts(const char *s)
{
	for (; s != NULL && *s != '\0'; s++) {
		uart_early_putc(*s);
	}
}

/* ---- the clock --------------------------------------------------------- */

uint32_t gtimer_freq_get(void)
{
	return counter_freq;
}

uint64_t gtimer_count_get(void)
{
	return counter;
}

uint32_t os_tick_get_count32(void)
{
	return (uint32_t)counter;
}

uint32_t os_tick_freq_hz(void)
{
	return counter_freq;
}

/* ---- the suite's window into both -------------------------------------- */

const char *ulog_test_wire(void)
{
	return cap;
}

void ulog_test_wire_reset(void)
{
	cap_len = 0u;
	cap[0] = '\0';
}

void ulog_test_clock_set(uint64_t us)
{
	counter = us;
}
