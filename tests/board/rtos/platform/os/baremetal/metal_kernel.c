/* SPDX-License-Identifier: GPL-2.0 */
/*
 * metal_kernel.c - cooperative scheduler loop, wait lists and time base.
 *
 * Policy:
 *   - pick the highest-priority READY thread; equal priorities round-robin;
 *   - no preemption: a thread leaves the CPU only in metal_switch();
 *   - IRQs never switch threads, they only make a thread READY;
 *   - the loop itself polls deadlines and software timers between switches;
 *   - the virtual timer is a one-shot: armed only when the CPU would idle,
 *     disarmed in the handler, so there is no periodic tick at all.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "metal_kernel.h"
#include "board_conf.h"
#include "generic_timer.h"
#include "gicv3.h"

#define METAL_FRAME_WORDS 12	/* x19..x30 */

extern void metal_switch(uint64_t **save_sp, uint64_t *new_sp);
extern void metal_task_entry(void);
extern char metal_vector_table[];

static struct metal_thread s_threads[CMSIS2_MAX_THREADS];
static struct metal_timer s_timers[CMSIS2_MAX_TIMERS];
static uint64_t *s_kernel_sp;
static uint32_t s_rr;
static uint32_t s_freq;
static volatile uint32_t s_timer_events;
static volatile uint32_t s_heartbeat;

/* DEBUG: P2 bring-up heartbeat, removed once the metal line is green. */
extern int printf(const char *fmt, ...);
static uint64_t s_hb_next;

static void vtimer_arm(uint64_t us);

extern void uart_early_puts(const char *);

static void raw_hex32(uint32_t v)
{
	static const char dig[] = "0123456789abcdef";
	char b[11];
	uint32_t i;

	b[0] = '0';
	b[1] = 'x';
	for (i = 0u; i < 8u; i++)
		b[2 + i] = dig[(v >> (28u - 4u * i)) & 0xfu];
	b[10] = 0;
	uart_early_puts(b);
}

static uint32_t s_wd_shots;
static int s_wd_armed;
static volatile uint64_t s_last_loop_us;

static void watchdog_poll(void)
{
	if (s_wd_armed || s_wd_shots >= 30u)
		return;
	s_wd_armed = 1;
	/* wd disabled for isolation */;
}

static void watchdog_report(void)
{
	uint64_t elr, sp, daif;

	if (s_wd_shots >= 30u)
		return;
	s_wd_armed = 0;
	if (metal_now_us() - s_last_loop_us < 500000ull)
		return;
	s_wd_shots++;
	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile("mov %0, sp" : "=r"(sp));
	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	uart_early_puts("metal: wd pc=");
	raw_hex32((uint32_t)elr);
	uart_early_puts(" sp=");
	raw_hex32((uint32_t)sp);
	uart_early_puts(" daif=");
	raw_hex32((uint32_t)daif);
	uart_early_puts(" cur=");
	uart_early_puts(metal_current != NULL ? metal_current->name : "(none)");
	uart_early_puts("\r\n");
}

static void heartbeat_poll(void)
{
	uint64_t now = metal_now_us();
	uint32_t i, ready = 0u, blocked = 0u;
	static uint32_t s_hb_prints;

	if (s_hb_next == 0ull)
		s_hb_next = now + 2000000ull;
	if (now < s_hb_next)
		return;
	s_hb_next = now + 2000000ull;
	for (i = 0u; i < CMSIS2_MAX_THREADS; i++) {
		if (!s_threads[i].used)
			continue;
		if (s_threads[i].state == METAL_READY)
			ready++;
		else if (s_threads[i].state == METAL_BLOCKED)
			blocked++;
	}
	if (s_hb_prints < 8u) {
		s_hb_prints++;
		uart_early_puts("metal: hb tick=");
		raw_hex32(s_timer_events);
		uart_early_puts(" ready=");
		raw_hex32(ready);
		uart_early_puts(" blocked=");
		raw_hex32(blocked);
		uart_early_puts(" cur=");
		uart_early_puts(metal_current != NULL ? metal_current->name : "(loop)");
		uart_early_puts("\r\n");
	}
}

struct metal_thread *metal_current;

/* ------------------------------------------------------------------ */
/* time and IRQ state                                                  */
/* ------------------------------------------------------------------ */

uint64_t metal_now_us(void)
{
	uint64_t c = gtimer_count_get();
	uint32_t f = s_freq;

	if (f == 0u)
		f = gtimer_freq_get();
	return (c * 1000000ull) / f;
}

uint32_t metal_now_ms(void)
{
	return (uint32_t)(metal_now_us() / 1000ull);
}

uint32_t metal_irq_save(void)
{
	uint64_t daif;

	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	__asm__ volatile("msr daifset, #2" ::: "memory");
	return (uint32_t)daif;
}

void metal_irq_restore(uint32_t state)
{
	__asm__ volatile("msr daif, %0" :: "r"((uint64_t)state) : "memory");
}

void metal_irq_enable(void)
{
	__asm__ volatile("msr daifclr, #2" ::: "memory");
}

void metal_irq_disable(void)
{
	__asm__ volatile("msr daifset, #2" ::: "memory");
}

static void vtimer_stop(void)
{
	gtimer_vtimer_ctl_set(0u);
}

static void vtimer_arm(uint64_t us)
{
	uint64_t ticks;

	if (us == 0ull)
		us = 1ull;
	ticks = (us * (uint64_t)s_freq) / 1000000ull;
	if (ticks == 0ull)
		ticks = 1ull;
	if (ticks > 0x3fffffffull)
		ticks = 0x3fffffffull;

	gtimer_vtimer_ctl_set(0u);
	gtimer_vtimer_tval_set((uint32_t)ticks);
	gtimer_vtimer_ctl_set(1u);
}

static void metal_timer_isr(void *arg)
{
	(void)arg;
	/* The generic timer line is level sensitive and is cleared by
	 * rewriting TVAL (measured on this board, see tick.c); merely
	 * clearing ENABLE leaves the line asserted and the GIC immediately
	 * re-pends after EOI. Re-arming is the loop's decision. */
	gtimer_vtimer_ctl_set(0u);
	gtimer_vtimer_tval_set(0x3fffffffu);
	{
		static uint32_t isr_logged;

		if (isr_logged < 4u) {
			isr_logged++;
			uart_early_puts("metal: isr ctl=");
			raw_hex32(gtimer_vtimer_ctl_get());
			uart_early_puts("\r\n");
		}
	}
	s_timer_events++;
}

/* Called from the IRQ vector with the caller-saved state already spilled. */
void metal_irq_handler(void)
{
	uint32_t iar = gic_iar_read();

	if (iar == 1023u)
		return;
	{
		extern void uart_early_puts(const char *);
		static uint32_t logged;

		if (logged < 8u) {
			uint64_t elr;

			uint64_t spsr, daif;

			logged++;
			__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
			__asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
			__asm__ volatile("mrs %0, daif" : "=r"(daif));
			uart_early_puts("metal: irq iar=");
			raw_hex32(iar);
			uart_early_puts(" elr=");
			raw_hex32((uint32_t)elr);
			uart_early_puts(" spsr=");
			raw_hex32((uint32_t)spsr);
			uart_early_puts(" daif=");
			raw_hex32((uint32_t)daif);
			uart_early_puts("\r\n");
		}
	}
	gic_irq_dispatch(iar);
	gic_irq_eoi(iar);
	{
		static uint32_t eoi_logged;

		if (eoi_logged < 12u) {
			eoi_logged++;
			uart_early_puts("metal: irq eoi\r\n");
		}
		/* no re-arm while isolating the timer */
	}
}

/* ------------------------------------------------------------------ */
/* threads                                                             */
/* ------------------------------------------------------------------ */

struct metal_thread *metal_self(void)
{
	return metal_current;
}

struct metal_thread *metal_thread_find(const char *name)
{
	uint32_t i;

	if (name == NULL)
		return NULL;
	for (i = 0u; i < CMSIS2_MAX_THREADS; i++) {
		if (s_threads[i].used && s_threads[i].name != NULL &&
		    strcmp(s_threads[i].name, name) == 0)
			return &s_threads[i];
	}
	return NULL;
}

uint32_t metal_thread_count(void)
{
	uint32_t i, n = 0u;

	for (i = 0u; i < CMSIS2_MAX_THREADS; i++)
		n += s_threads[i].used ? 1u : 0u;
	return n;
}

struct metal_thread *metal_thread_create(metal_fn_t fn, void *arg,
					 const char *name, int prio,
					 size_t stack_size)
{
	struct metal_thread *t = NULL;
	uint64_t *frame;
	uint8_t *top;
	void *mem;
	uint32_t state;
	uint32_t i;

	if (fn == NULL)
		return NULL;
	if (stack_size < 1024u)
		stack_size = 8192u;
	stack_size = (stack_size + 15u) & ~15u;

	mem = metal_heap_alloc(stack_size + 16u);
	if (mem == NULL)
		return NULL;
	top = (uint8_t *)(((uintptr_t)mem + stack_size + 16u) & ~(uintptr_t)15u);

	/* First-run frame: metal_switch() pops x19..x30 and returns into
	 * metal_task_entry, which calls fn(arg) out of x19/x20. */
	frame = (uint64_t *)(top - (METAL_FRAME_WORDS * 8));
	memset(frame, 0, METAL_FRAME_WORDS * 8);
	frame[0] = (uint64_t)(uintptr_t)fn;		/* x19 */
	frame[1] = (uint64_t)(uintptr_t)arg;		/* x20 */
	frame[11] = (uint64_t)(uintptr_t)metal_task_entry;	/* x30 */

	state = metal_irq_save();
	for (i = 0u; i < CMSIS2_MAX_THREADS; i++) {
		if (!s_threads[i].used) {
			t = &s_threads[i];
			break;
		}
	}
	if (t != NULL) {
		memset(t, 0, sizeof(*t));
		t->sp = frame;
		t->stack = mem;
		t->stack_top = top;
		t->stack_size = (uint32_t)stack_size;
		t->name = name;
		t->prio = (uint8_t)((prio < 1) ? 1 : (prio > 63 ? 63 : prio));
		t->base_prio = t->prio;
		t->state = METAL_READY;
		t->used = 1u;
	}
	metal_irq_restore(state);

	if (t == NULL)
		metal_heap_free(mem);
	return t;
}

void metal_unlink(struct metal_thread *t)
{
	struct metal_thread **head;
	struct metal_thread **p;

	if (t == NULL || t->wait_head == NULL)
		return;
	head = (struct metal_thread **)t->wait_head;
	p = head;
	while (*p != NULL && *p != t)
		p = &(*p)->wait_next;
	if (*p == t)
		*p = t->wait_next;
	t->wait_next = NULL;
	t->wait_head = NULL;
}

void metal_wake_one(void *wait_head, int status)
{
	struct metal_thread **head = (struct metal_thread **)wait_head;
	struct metal_thread *t;

	if (head == NULL)
		return;
	t = *head;
	if (t == NULL)
		return;
	*head = t->wait_next;
	t->wait_next = NULL;
	t->wait_head = NULL;
	t->timeout_us = 0ull;
	t->wake_status = status;
	t->state = METAL_READY;
}

void metal_wake_all(void *wait_head, int status)
{
	struct metal_thread **head = (struct metal_thread **)wait_head;

	while (head != NULL && *head != NULL)
		metal_wake_one(head, status);
}

void metal_yield(void)
{
	struct metal_thread *t = metal_current;

	if (t == NULL)
		return;
	t->state = METAL_READY;
	metal_switch(&t->sp, s_kernel_sp);
}

int metal_block_on(void *wait_head, uint64_t timeout_us)
{
	return metal_block_until(wait_head,
				 timeout_us ? metal_now_us() + timeout_us : 0ull);
}

int metal_block_until(void *wait_head, uint64_t deadline_us)
{
	struct metal_thread *t = metal_current;
	uint32_t state;

	if (t == NULL)
		return METAL_RESOURCE;

	state = metal_irq_save();
	if (wait_head != NULL) {
		t->wait_next = *(struct metal_thread **)wait_head;
		*(struct metal_thread **)wait_head = t;
		t->wait_head = wait_head;
	}
	t->timeout_us = deadline_us;
	t->wake_status = METAL_OK;
	t->state = METAL_BLOCKED;
	metal_irq_restore(state);

	metal_switch(&t->sp, s_kernel_sp);

	t->timeout_us = 0ull;
	t->wait_head = NULL;
	return t->wake_status;
}

int metal_block_when(void *wait_head, uint64_t deadline_us,
		     int (*cond)(void *), void *arg)
{
	struct metal_thread *t = metal_current;
	uint32_t state;

	if (t == NULL)
		return METAL_RESOURCE;

	state = metal_irq_save();
	if (cond != NULL && cond(arg)) {
		metal_irq_restore(state);
		return METAL_OK;
	}
	if (wait_head != NULL) {
		t->wait_next = *(struct metal_thread **)wait_head;
		*(struct metal_thread **)wait_head = t;
		t->wait_head = wait_head;
	}
	t->timeout_us = deadline_us;
	t->wake_status = METAL_OK;
	t->state = METAL_BLOCKED;
	metal_irq_restore(state);

	metal_switch(&t->sp, s_kernel_sp);

	t->timeout_us = 0ull;
	t->wait_head = NULL;
	return t->wake_status;
}

int metal_thread_terminate_id(struct metal_thread *t)
{
	uint32_t state;

	if (t == NULL || !t->used || t->state == METAL_TERMINATED)
		return -1;

	state = metal_irq_save();
	if (t->state == METAL_BLOCKED)
		metal_unlink(t);
	t->state = METAL_TERMINATED;
	t->timeout_us = 0ull;
	metal_irq_restore(state);
	return 0;
}

void metal_thread_terminate_self(void)
{
	struct metal_thread *t = metal_current;
	uint32_t state;

	state = metal_irq_save();
	if (t != NULL) {
		t->state = METAL_TERMINATED;
		t->timeout_us = 0ull;
	}
	metal_irq_restore(state);
	if (t != NULL)
		metal_switch(&t->sp, s_kernel_sp);
	for (;;)
		;
}

/* ------------------------------------------------------------------ */
/* timers                                                              */
/* ------------------------------------------------------------------ */

struct metal_timer *metal_timer_alloc(void)
{
	struct metal_timer *t = NULL;
	uint32_t state, i;

	state = metal_irq_save();
	for (i = 0u; i < CMSIS2_MAX_TIMERS; i++) {
		if (!s_timers[i].used) {
			t = &s_timers[i];
			memset(t, 0, sizeof(*t));
			t->used = 1u;
			break;
		}
	}
	metal_irq_restore(state);
	return t;
}

int metal_timer_start(struct metal_timer *t, uint32_t ms)
{
	uint32_t state;

	if (t == NULL)
		return -1;
	state = metal_irq_save();
	t->period_ms = ms;
	t->due_us = metal_now_us() + (uint64_t)ms * 1000ull;
	t->active = 1u;
	metal_irq_restore(state);
	return 0;
}

int metal_timer_stop(struct metal_timer *t)
{
	uint32_t state;

	if (t == NULL)
		return -1;
	state = metal_irq_save();
	t->active = 0u;
	metal_irq_restore(state);
	return 0;
}

void metal_timer_release(struct metal_timer *t)
{
	uint32_t state;

	if (t == NULL)
		return;
	state = metal_irq_save();
	t->active = 0u;
	t->used = 0u;
	metal_irq_restore(state);
}

/* ------------------------------------------------------------------ */
/* scheduler loop                                                      */
/* ------------------------------------------------------------------ */

static struct metal_thread *pick_next(void)
{
	int best = -1;
	uint32_t i, k;

	for (i = 0u; i < CMSIS2_MAX_THREADS; i++) {
		struct metal_thread *t = &s_threads[i];

		if (t->used && t->state == METAL_READY && (int)t->prio > best)
			best = (int)t->prio;
	}
	if (best < 0)
		return NULL;

	for (k = 1u; k <= CMSIS2_MAX_THREADS; k++) {
		i = (s_rr + k) % CMSIS2_MAX_THREADS;
		if (s_threads[i].used && s_threads[i].state == METAL_READY &&
		    (int)s_threads[i].prio == best) {
			s_rr = i;
			return &s_threads[i];
		}
	}
	return NULL;
}

static uint64_t next_deadline(void)
{
	uint64_t next = 0ull;
	uint32_t i;

	for (i = 0u; i < CMSIS2_MAX_THREADS; i++) {
		struct metal_thread *t = &s_threads[i];

		if (t->used && t->state == METAL_BLOCKED && t->timeout_us != 0ull &&
		    (next == 0ull || t->timeout_us < next))
			next = t->timeout_us;
	}
	for (i = 0u; i < CMSIS2_MAX_TIMERS; i++) {
		struct metal_timer *t = &s_timers[i];

		if (t->used && t->active &&
		    (next == 0ull || t->due_us < next))
			next = t->due_us;
	}
	return next;
}

static void timeouts_poll(void)
{
	uint64_t now = metal_now_us();
	uint32_t i;

	for (i = 0u; i < CMSIS2_MAX_THREADS; i++) {
		struct metal_thread *t = &s_threads[i];

		if (!t->used || t->state != METAL_BLOCKED || t->timeout_us == 0ull)
			continue;
		if (now < t->timeout_us)
			continue;
		metal_unlink(t);
		t->timeout_us = 0ull;
		t->wake_status = METAL_TIMEOUT;
		t->state = METAL_READY;
	}
}

static void timers_poll(void)
{
	uint64_t now = metal_now_us();
	uint32_t i;

	for (i = 0u; i < CMSIS2_MAX_TIMERS; i++) {
		struct metal_timer *t = &s_timers[i];

		if (!t->used || !t->active || t->due_us > now)
			continue;
		if (t->periodic)
			t->due_us = now + (uint64_t)t->period_ms * 1000ull;
		else
			t->active = 0u;
		t->fn(t->arg);
	}
}

static void idle(void)
{
	static int reported;

	if (!reported) {
		reported = 1;
		printf("metal: idle reached\n");
	}
	/* The loop itself is the idle thread: poll the deadlines instead of
	 * sleeping on WFI. Keeps the backend independent of timer delivery
	 * ordering while the bring-up is in progress. */
	__asm__ volatile("yield" ::: "memory");
}

void metal_kernel_heartbeat(void)
{
	s_heartbeat++;
}

void metal_kernel_init(void)
{
	memset(s_threads, 0, sizeof(s_threads));
	memset(s_timers, 0, sizeof(s_timers));
	s_rr = 0u;
	s_freq = gtimer_freq_get();
	if (s_freq == 0u)
		s_freq = 24000000u;
	metal_heap_init();
}

void metal_kernel_start(void)
{
	__asm__ volatile("msr vbar_el1, %0" :: "r"((uint64_t)(uintptr_t)metal_vector_table) : "memory");
	__asm__ volatile("isb" ::: "memory");

	gicv3_init();
	irq_register(BOARD_TICK_INTID, metal_timer_isr, NULL);
	irq_set_priority(BOARD_TICK_INTID, 0xe0u);
	irq_set_mode_level(BOARD_TICK_INTID);
	irq_clear_pending(BOARD_TICK_INTID);
	irq_enable(BOARD_TICK_INTID);

	metal_irq_enable();
	for (;;) {
		struct metal_thread *t;

		/* A thread blocks with IRQs masked; the loop must re-open them
		 * before it can be interrupted by the next one-shot timer. */
		metal_irq_enable();
		metal_current = NULL;
		s_last_loop_us = metal_now_us();
		watchdog_poll();
		timeouts_poll();
		timers_poll();
		heartbeat_poll();
		t = pick_next();
		if (t == NULL) {
			idle();
			continue;
		}
		t->state = METAL_RUNNING;
		metal_current = t;
		metal_switch(&s_kernel_sp, t->sp);
		{
			static uint32_t sw;
			if (sw < 3u) {
				sw++;
				printf("metal: loop back from %s\n", t->name);
			}
		}
		metal_current = NULL;
	}
}
/* Fault reporting: reached from the vector table before any scheduler state is
 * trustworthy, so it only reads the syndrome registers and prints. */
void metal_fault_report(uint64_t esr, uint64_t elr, uint64_t far, uint64_t lr)
{
	uart_early_puts("metal: FAULT esr=");
	raw_hex32((uint32_t)esr);
	uart_early_puts(" elr=");
	raw_hex32((uint32_t)elr);
	uart_early_puts(" far=");
	raw_hex32((uint32_t)far);
	uart_early_puts(" lr=");
	raw_hex32((uint32_t)lr);
	uart_early_puts("\r\n");
	for (;;)
		__asm__ volatile("wfi");
}
