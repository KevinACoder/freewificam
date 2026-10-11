/*
 * @file   log.c
 * @brief  Stamped line sink over the polled early UART.
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
 * Once the tick is up the source switches to the tick count (see
 * board_log_stamp_use_tick below), which is what makes the "[ N.000]" tick
 * anchors self-consistent with the counter they report.
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
#ifdef ULOG_ENABLED
#include "log_anchors.h"
#endif
#include "minilibc.h"
#include "arch_asm.h"
#include "generic_timer.h"
#include "tick.h"
#include "uart.h"

/* --- boot-relative timestamp ---------------------------------------------- */

#define BOARD_TS_STAMP_LEN 16u

/* Counter value at the first stamped print; 0 means "not captured yet".
 * Two calls racing the capture store near-identical values; the u64 store
 * is a single aligned STR, so the worst case is a one-tick offset in t0. */
static uint64_t log_t0;
static bool stamp_tick_mode;

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
			log_t0 = gtimer_count_get();
			if (log_t0 == 0u) {
				log_t0 = 1u;	/* counter really was 0: don't re-capture */
			}
		}

		delta = gtimer_count_get() - log_t0;
		freq = gtimer_freq_get();
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
 * which a -nostdlib image does not have. Returns 0 when the lock was taken.
 * The host spelling is for the outlet suite, which compiles this file to
 * exercise the line assembly and the repeat gate; there libatomic exists. */
static inline unsigned int print_try_lock(void)
{
#ifdef __aarch64__
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
#else
	return __atomic_test_and_set(&print_lock_held, __ATOMIC_ACQUIRE) ? 1u : 0u;
#endif
}

static void print_lock_take(uint64_t *saved_daif)
{
#ifdef __aarch64__
	/* Mask IRQs for the whole hold, restore the caller's DAIF after
	 * release: same-core re-entrancy would otherwise deadlock (an ISR
	 * spinning on the lock its own interrupted context holds). */
	ARCH_DAIF_GET(*saved_daif);
	ARCH_IRQ_MASK();
#else
	*saved_daif = 0u;
#endif

	while (print_try_lock() != 0u) {
#ifdef __aarch64__
		__asm__ __volatile__("yield" ::: "memory");
#endif
	}
}

static void print_lock_give(uint64_t saved_daif)
{
#ifdef __aarch64__
	/* STLR: store with release semantics, the pairing half of LDAXR. */
	__asm__ __volatile__("stlr %w1, %0"
			     : "=Q"(print_lock_held)
			     : "r"(0u)
			     : "memory");

	/* Restore the caller's interrupt state exactly (whole DAIF, not just
	 * the I bit: callers in ISR context arrive with IRQs masked and must
	 * stay that way). */
	ARCH_DAIF_PUT(saved_daif);
#else
	(void)saved_daif;
	__atomic_store_n(&print_lock_held, 0u, __ATOMIC_RELEASE);
#endif
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

/*
 * Consecutive identical lines collapse into one, with the length of the run
 * reported when it ends.  A driver that logs the same fault from a retry loop
 * can emit tens of thousands of copies - the rtw89 SWSI retry produced 7739 of
 * one run's 10844 serial lines - and the lab's serial ring drops bursts, so the
 * flood does not merely waste space: it evicts the lines the run is actually
 * judged on (and read from).  The first copy is kept, because an anchor or a
 * judge pattern must still match it, and the run's count is printed, so nothing
 * is hidden.
 */
static char repeat_prev[CONSOLE_LINE_MAX];
static unsigned int repeat_n;

static void console_repeat_flush(void)
{
	char tail[64];

	if (repeat_n > 1u) {
		(void)snprintf(tail, sizeof(tail),
		    "[previous line repeated %u times]\n", repeat_n - 1u);
		console_feed(tail);
	}
	repeat_n = 0u;
	repeat_prev[0] = '\0';
}

static void console_repeat_gate(const char *msg)
{

	if (repeat_n != 0u && strcmp(msg, repeat_prev) == 0) {
		repeat_n++;
		return;
	}
	console_repeat_flush();
	(void)snprintf(repeat_prev, sizeof(repeat_prev), "%s", msg);
	repeat_n = 1u;
	console_feed(msg);
}

/* --- the level plane -------------------------------------------------------- */

#ifdef ULOG_ENABLED

/*
 * A line that matches a never-filtered family is promoted to ALWAYS, so no
 * runtime threshold can silence a judged line, a one-boot ledger line, a
 * protocol line or a fault line. This is the one place the promotion happens,
 * which is what lets vendored trees (whose call sites we cannot annotate) stay
 * judged: their lines are promoted here by text. The family table is generated
 * from the judges' anchor tables by tools/gates/log-anchors.py.
 */
static bool log_anchor_promote(const char *msg)
{
	unsigned int i;

	for (i = 0U; i < log_anchor_family_count; i++) {
		if (strstr(msg, log_anchor_families[i]) != NULL) {
			return true;
		}
	}
	return false;
}

/* Outlet subscriber. The engine calls this with the entry-held print lock, so
 * the line discipline runs without taking the lock again. */
static void log_sink(ulog_level_t severity, char *msg)
{
	(void)severity;
	console_repeat_gate(msg);
}

static bool log_ready;
static ulog_level_t log_current_level = (ulog_level_t)LOG_DEFAULT_LEVEL;

void log_emit(ulog_level_t severity, const char *fmt, ...)
{
	char msg[CONSOLE_LINE_MAX];
	va_list ap;
	uint64_t saved_daif;

	va_start(ap, fmt);
	(void)vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	/* The promotion must see the text, so the format happens before the
	 * gate. A filtered line still pays one vsnprintf - cheaper than the
	 * serial write it would otherwise cost. */
	if (log_anchor_promote(msg)) {
		severity = ULOG_ALWAYS_LEVEL;
	}

	print_lock_take(&saved_daif);
	if (!log_ready) {
		/* Lazy: boot-early callers (the heap probe) print before main. */
		ULOG_INIT();
		(void)ULOG_SUBSCRIBE(log_sink, (ulog_level_t)LOG_DEFAULT_LEVEL);
		log_ready = true;
	}
	ulog_message(severity, "%s", msg);
	print_lock_give(saved_daif);
}

ulog_level_t log_level_get(void)
{
	return log_current_level;
}

void log_level_set(ulog_level_t severity)
{
	uint64_t saved_daif;

	print_lock_take(&saved_daif);
	if (!log_ready) {
		ULOG_INIT();
		log_ready = true;
	}
	/* ulog's change model: re-subscribing an existing function updates its
	 * threshold (and a first call with no prior subscription installs it). */
	(void)ULOG_SUBSCRIBE(log_sink, severity);
	log_current_level = severity;
	print_lock_give(saved_daif);
}

#else /* !ULOG_ENABLED */

/*
 * The compile-time master gate. With ULOG_ENABLED undefined the level plane
 * generates no code at all: the entry points stay, so the call sites and the
 * console command still link, and the raw outlets below (the console bytes and
 * the emergency print) are what remains of the console.
 */
void log_emit(ulog_level_t severity, const char *fmt, ...)
{
	(void)severity;
	(void)fmt;
}

ulog_level_t log_level_get(void)
{
	return (ulog_level_t)LOG_DEFAULT_LEVEL;
}

void log_level_set(ulog_level_t severity)
{
	(void)severity;
}

#endif /* ULOG_ENABLED */

void board_log(const char *fmt, ...)
{
	char msg[CONSOLE_LINE_MAX];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	log_emit(ULOG_INFO_LEVEL, "%s", msg);
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

void board_log_emergency(const char *fmt, ...)
{
	/* Static, not stack: the likeliest caller is a stack-overflow victim.
	 * No lock, no stamp, no line assembler: straight to the polled UART.
	 * A torn line in a live log is noise; a missing death line is an open
	 * issue. IRQs are masked around the output so a second emergency
	 * caller (a storming ISR) cannot shred the line this one is
	 * transmitting - the caller's DAIF is restored afterwards, so an ISR
	 * caller stays masked. */
	static char buf[CONSOLE_LINE_MAX];
	va_list ap;
	uint64_t saved_daif;
	unsigned int n;

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	for (n = 0U; (n < sizeof(buf)) && (buf[n] != '\0'); n++) {
		continue;
	}
	/* Ensure the line ends as a line even when the format forgot. */
	if ((n == 0u) || (buf[n - 1u] != '\n')) {
		if (n < sizeof(buf) - 1u) {
			buf[n++] = '\n';
			buf[n] = '\0';
		}
	}

	ARCH_DAIF_GET(saved_daif);
	ARCH_IRQ_MASK();
	uart_early_puts("!!! ");
	uart_early_puts(buf);
	ARCH_DAIF_PUT(saved_daif);
}
