/*
 * @file   log.c
 * @brief  Stamped line sink over the polled early UART.
 *
 * Print discipline (carried over whole from the prior project; SPEC v3
 * §3.4.2): one line, one stamp, one CR LF on the wire.
 *
 * The sink ASSEMBLES lines rather than writing each call straight through,
 * because callers legitimately print one logical line in several pieces: a
 * board_log call without the trailing '\n' leaves its text pending in a
 * shared buffer, and the next write joins it. Concretely:
 *
 *   - a '\n' ends the line: stamp + text + CR LF, exactly once;
 *   - a '\n' with nothing pending is dropped            (no blank lines);
 *   - '\r' is dropped: the sink owns CR LF, callers write '\n' only;
 *   - a line longer than the buffer is split, never dropped.
 *
 * The stamp is the sink's business: boot-relative seconds from cntvct_el0,
 * lazily zeroed at the first stamped line (there is no single "C start"
 * call), rendered "[   s.mmm] " - width 4 + three fractional digits,
 * deliberately milliseconds: the console renders about one character per
 * 87 us at 115200, so finer digits would be fake precision on the wire.
 *
 * The lock: single core + IRQs masked is a critical section. Held with IRQs
 * masked and the caller's DAIF restored on release - a same-core ISR that
 * prints would otherwise spin on the lock its own interrupted context is
 * holding, and the holder would never run to release it. The LDAXR/STXR
 * pair is inline because __atomic_test_and_set would call into libatomic,
 * which a -nostdlib image does not have.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "log.h"
#include "minilibc.h"
#include "tick.h"
#include "uart.h"

/* --- boot-relative timestamp ---------------------------------------------- */

#define BOARD_TS_STAMP_LEN 16u

/* --- the stamp source -------------------------------------------------------
 *
 * Early (before the tick): boot-relative seconds from cntvct_el0, lazily
 * zeroed at the first stamped line. Once the tick is up, main switches the
 * source to the tick count (board_log_stamp_use_tick) - the switch point is
 * announced by an anchor line, and every stamp after it derives from ticks
 * (sec = count/freq, ms = remainder), which is what makes the "[ N.000]"
 * tick anchors self-consistent with the counter they report. */

/* Counter value at the first stamped print; 0 means "not captured yet".
 * Two calls racing the capture store near-identical values; the u64 store
 * is a single aligned STR, so the worst case is a one-tick offset in t0. */
static uint64_t log_t0;
static bool stamp_tick_mode;

static uint64_t read_cntvct(void)
{
	uint64_t v;

	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

static uint64_t read_cntfrq(void)
{
	uint64_t v;

	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

/* 64/32 restoring division. 64 iterations is far below the per-line cost of
 * a 115200 console. */
static uint64_t udiv64(uint64_t n, uint32_t d)
{
	uint64_t q = 0u;
	uint64_t r = 0u;
	int i;

	for (i = 63; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1u);
		if (r >= (uint64_t)d) {
			r -= (uint64_t)d;
			q |= (1ULL << i);
		}
	}
	return q;
}

/* Render the stamp for "now" into buf (BOARD_TS_STAMP_LEN capacity). */
static void render_stamp(char buf[BOARD_TS_STAMP_LEN])
{
	if (stamp_tick_mode) {
		uint32_t freq = os_tick_freq_hz();
		uint32_t t = os_tick_get_count32();
		uint32_t sec, ms;

		if (freq == 0u) {
			freq = 1u;	/* tick not set up: degenerate to t/1 */
		}
		sec = t / freq;
		ms = (t % freq) * 1000u / freq;
		(void)snprintf(buf, BOARD_TS_STAMP_LEN, "[%4u.%03u] ", sec, ms);
		return;
	}

	{
		uint64_t delta, freq, sec;
		uint32_t frac;

		if (log_t0 == 0u) {
			log_t0 = read_cntvct();
			if (log_t0 == 0u) {
				log_t0 = 1u;	/* counter really was 0: don't re-capture */
			}
		}

		delta = read_cntvct() - log_t0;
		freq = read_cntfrq();
		if (freq == 0u) {
			freq = 1u;
		}
		sec = udiv64(delta, (uint32_t)freq);
		frac = (uint32_t)udiv64((delta - sec * freq) * 1000u, (uint32_t)freq);

		(void)snprintf(buf, BOARD_TS_STAMP_LEN, "[%4u.%03u] ",
			       (unsigned int)sec, frac);
	}
}

void board_log_stamp_use_tick(void)
{
	stamp_tick_mode = true;
}

/* --- one-line print lock --------------------------------------------------- */

static volatile unsigned int print_lock_held;

/* LDAXR/STXR pair, inline: __atomic_test_and_set would call into libatomic,
 * which a -nostdlib image does not have. Returns 0 when the lock was taken. */
static inline unsigned int print_try_lock(void)
{
	unsigned int status;
	unsigned int wanted = 1u;

	__asm__ __volatile__(
		"	ldaxr	%w0, %2\n"
		"	cbnz	%w0, 1f\n"
		"	stxr	%w0, %w1, %2\n"
		"1:\n"
		: "=&r"(status)
		: "r"(wanted), "Q"(*(volatile unsigned int *)&print_lock_held)
		: "memory");

	return status;
}

static void print_lock_take(uint64_t *saved_daif)
{
	/* Mask IRQs for the whole hold, restore the caller's DAIF after
	 * release: same-core re-entrancy would otherwise deadlock (an ISR
	 * spinning on the lock its own interrupted context holds). */
	__asm__ __volatile__("mrs %0, daif" : "=r"(*saved_daif));
	__asm__ __volatile__("msr daifset, #2" ::: "memory");

	while (print_try_lock() != 0u) {
		__asm__ __volatile__("yield" ::: "memory");
	}
}

static void print_lock_give(uint64_t saved_daif)
{
	/* STLR: store with release semantics, the pairing half of LDAXR. */
	__asm__ __volatile__("stlr %w1, %0"
			     : "=Q"(print_lock_held)
			     : "r"(0u)
			     : "memory");

	/* Restore the caller's interrupt state exactly (whole DAIF, not just
	 * the I bit: callers in ISR context arrive with IRQs masked and must
	 * stay that way). */
	__asm__ __volatile__("msr daif, %0" ::"r"(saved_daif) : "memory");
}

/* --- the stamped line sink -------------------------------------------------- */

#define CONSOLE_LINE_MAX 256u
static char console_line[CONSOLE_LINE_MAX];
static unsigned int console_len;

/* Emit the pending line, stamped. Caller holds the print lock. */
static void console_line_flush(void)
{
	char ts[BOARD_TS_STAMP_LEN];

	if (console_len == 0u) {
		return;
	}
	console_line[console_len] = '\0';
	if (board_console_muted()) {
		/* A gdb stub session owns the wire: drop the line so board_log
		 * output cannot land in the middle of the RSP stream. The raw
		 * outlet (board_console_write_raw) does not ask - it is the
		 * stub's and the fault paths' own channel. */
		console_len = 0u;
		return;
	}
	render_stamp(ts);
	uart_early_puts(ts);
	uart_early_puts(console_line);
	/* '\n' only: the polled writer expands it to CR LF. Writing "\r\n"
	 * here reaches the wire as CR CR LF, and a parser that counts the
	 * bare CR reports a blank line after every stamped line. */
	uart_early_puts("\n");
	console_len = 0u;
}

/* Append a message, terminating lines as they complete. Caller holds the
 * print lock. */
static void console_feed(const char *s)
{
	for (; *s != '\0'; s++) {
		if (*s == '\r') {
			continue;
		}
		if (*s == '\n') {
			console_line_flush();
			continue;
		}
		if (console_len == CONSOLE_LINE_MAX - 1u) {
			console_line_flush();	/* overlong: split, not drop */
		}
		console_line[console_len++] = *s;
	}
}

void board_log(const char *fmt, ...)
{
	char msg[CONSOLE_LINE_MAX];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	{
		uint64_t saved_daif;

		print_lock_take(&saved_daif);
		console_feed(msg);
		print_lock_give(saved_daif);
	}
}

void board_console_write_raw(const char *data, unsigned int len)
{
	uint64_t saved_daif;
	unsigned int i;

	if (data == 0) {
		return;
	}

	print_lock_take(&saved_daif);
	/* A fragment left pending by the assembler would otherwise be glued
	 * to this stream's bytes - close it first (the assembler holds
	 * fragments across calls by design, this outlet does not). */
	console_line_flush();
	for (i = 0U; i < len; i++) {
		if (data[i] == '\r' && (i + 1U) < len && data[i + 1U] == '\n') {
			continue;
		}
		uart_early_putc(data[i]);
	}
	print_lock_give(saved_daif);
}

/* Weak default: nothing mutes the console. The gdb glue overrides this
 * with gdb_session_active. */
__attribute__((weak)) int board_console_muted(void)
{
	return 0;
}

/* Weak default: no stub compiled in, every 0x03 belongs to the shell
 * (readline's cancel-line). The threadx gdb glue overrides with
 * gdb_break(). */
__attribute__((weak)) int board_console_break_hook(void)
{
	return 0;
}

/* The stamp's wall clock, exposed: cntvct elapsed since the stamp epoch
 * (log_t0), same numbers the "[   s.mmm] " stamps carry. */
void board_uptime_parts(uint32_t *sec, uint32_t *ms)
{
	uint64_t delta, freq, s;
	uint32_t m;

	if (log_t0 == 0u) {
		log_t0 = read_cntvct();
		if (log_t0 == 0u) {
			log_t0 = 1u;
		}
	}
	delta = read_cntvct() - log_t0;
	freq = read_cntfrq();
	if (freq == 0u) {
		freq = 1u;
	}
	s = udiv64(delta, (uint32_t)freq);
	m = (uint32_t)udiv64((delta - s * freq) * 1000u, (uint32_t)freq);
	if (sec != 0) {
		*sec = (uint32_t)s;
	}
	if (ms != 0) {
		*ms = m;
	}
}
