/*
 * @file   cherrysh_adapter.c
 * @brief  CherrySH on this board: console wiring, a shell task, commands.
 *
 * CherrySH is vendored byte-identical in components/cherrysh and is not
 * modified. Everything the board has to supply lives here.
 *
 * ---------------------------------------------------------------------------
 * WHAT CHERRYSH ACTUALLY REQUIRES
 *
 * Very little, which is why it was chosen. Its entire platform surface is
 * two function pointers in chry_shell_init_t:
 *
 *     sput(rl, buf, size)  -> write bytes to the console
 *     sget(rl, buf, size)  -> read bytes, returning 0 when none available
 *
 * There is no OS abstraction struct, no malloc, no stdio, no threading
 * model. Kernel access is the CMSIS-RTOS2 face only (os/cmsis2).
 *
 * ---------------------------------------------------------------------------
 * THE DESIGN, AND WHY IT IS NOT THE OBVIOUS ONE
 *
 * The obvious shell task is:
 *
 *     for (;;) { chry_shell_task_repl(&sh); }      // with a BLOCKING sget
 *
 * That is wrong, and the reason is worth stating because it is the kind of
 * bug that only shows up once there is real work in the system. With a
 * blocking sget the task spins in chry_readline_waitkey() whenever there is
 * no input, and because it never blocks, a lower-priority task never runs.
 * The shell would appear to work perfectly while quietly starving everything
 * else - the shell being the highest-priority thing in an otherwise idle
 * system hides it completely.
 *
 * So the shell runs CONFIG_CSH_NOBLOCK=1 and the task blocks on a thread
 * flag instead:
 *
 *     for (;;) {
 *         osThreadFlagsWait(..., osWaitForever);
 *         chry_shell_task_repl(&sh);    // returns 1 immediately if no line
 *     }
 *
 * The UART RX interrupt sets the flag (osThreadFlagsSetFromISR, the project
 * extension in os/cmsis2/cmsis_os2_ext.h). The flag latches, so input that
 * arrives while the task is inside the REPL is not lost, and the task is
 * genuinely blocked when idle, so it costs nothing.
 *
 * sget returns 0 when the ring is empty, which is exactly the "no data right
 * now" contract readline's noblock mode expects - the ring buffer and the
 * noblock REPL are made for each other.
 *
 * ---------------------------------------------------------------------------
 * CHERRYRB, AND ITS BARRIER BOUNDARY
 *
 * The RX ring is CherryRB. It is a kfifo with no OS dependency, which is
 * what makes it usable from an interrupt. It also has NO MEMORY BARRIERS
 * (its own documentation is explicit about this), so the producer/consumer
 * handoff needs one added by us: the ISR writes bytes, then a `dsb sy`
 * orders them before the flag it sets; the consumer sees the flag, which is
 * a synchronising event. That ordering is why the flag is set AFTER the
 * ring write, and why CherryRB's single-producer/single-consumer design is
 * relied on rather than assumed: exactly one ISR writes, exactly one task
 * reads.
 * ---------------------------------------------------------------------------
 */

#include <stddef.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "cmsis_os2_ext.h"
#include "log.h"
#include "uart.h"

#ifdef CONFIG_GDB_STUB
/* The stub seam: board_console_break_hook (weak in log.c, strong in the
 * threadx glue) for 0x03; gdb_isr_attach + session state for the '$'
 * attach. S4 spec §3.6.2. */
#include "gdb_glue.h"
#include "gdb/gdb.h"
#endif

/* The shell task's stack is a build key (Makefile CONFIG_SHELL_STACK). */
#ifndef CONFIG_SHELL_STACK
#define CONFIG_SHELL_STACK	4096U
#endif

#include "chry_ringbuffer.h"

/* Its own header: cherrysh_init()'s declaration and the CSH_FROM_ARGV
 * convention used by every command below. */
#include "cherrysh_adapter.h"
/* csh.h, not chry_shell.h: csh.h pulls in csh_config.h (our shadow config)
 * and then the shell itself, and it is what defines the CSH_CMD_EXPORT*
 * macros and the chry_syscall_t/chry_sysvar_t types. Including chry_shell.h
 * directly leaves CONFIG_CSH_* undefined. */
#include "csh.h"

/* System heap free count (os/threadx/heap.h); extern like the rest of this
 * file's board faces - adapter objects do not take the threadx include
 * path. */
extern size_t xPortGetFreeHeapSize(void);

/* PSCI SYSTEM_RESET (board/rk3568-itx/reset.c). */
extern int board_system_reset(void);

/* --- console wiring ------------------------------------------------------- */

/* RX ring: 512 bytes, which is well past the UART's own 64-byte FIFO and
 * gives a person typing faster than the shell can echo plenty of slack.
 * Power of two because CherryRB requires it. */
static uint8_t rx_ring_storage[512];
static chry_ringbuffer_t rx_ring;

/* The shell task's handle, set once the task exists. The ISR needs it to
 * set the wake flag, and reading a handle that may still be NULL is the
 * one hazard on this path - so it is checked, and a byte that arrives
 * before the task exists is simply left in the ring for the first poll. */
static osThreadId_t shell_task_id;

/* Flag bit meaning "there is input waiting". One bit is enough: the ring
 * is the real buffer, this only says "come and look". */
#define SHELL_INPUT_FLAG	0x1U

/* How often the shell task wakes anyway: the storm-defence poll, in
 * milliseconds, converted to ticks through the kernel's own tick rate
 * (never hardcoded - the semantic dependency on 1 kHz stays a preference,
 * spec §3.3.5). Cost when RX is healthy is one timed wait per interval. */
#define SHELL_RX_POLL_MS	500U

/* Bytes the driver hands us per reception: the FIFO's full depth (64). A
 * burst that arrives as one FIFO fill is handed over in one completion -
 * arming 1 and re-arming per byte made the driver's re-arm discard every
 * byte queued behind the first, which is how a pasted line once reached a
 * shell as "p" (board-proven, prior project 2026-09). */
#define SHELL_RX_CHUNK		64U

static uint8_t rx_chunk[SHELL_RX_CHUNK];

/* RX-path forensics (2026-10-02 console-outage round): increment-only
 * counters on the ISR side, read from the app heartbeat. They split the
 * path at its two boundaries - "ISR ran" (uart_console_rx_isr_calls in
 * the driver) vs "bytes ringed" vs "bytes consumed by readline" - so a
 * heartbeat line localizes where a keystroke is lost. Never logged from
 * here: printing inside the RX ISR would perturb the path under test. */
volatile unsigned cherrysh_rx_ringed;
volatile unsigned cherrysh_rx_consumed;
volatile unsigned cherrysh_rx_wakes;

/* The RX interrupt's completion callback: the FIFO contents are already in
 * rx_chunk, the count is what actually arrived. */
static void shell_uart_rx_event(uint32_t events, uint32_t rx_count)
{
	if ((events & UART_RX_EVENT_COMPLETE) == 0U) {
		return;
	}

	/* Pull whatever arrived into the ring. Reading the count the driver
	 * actually stored is more robust than assuming, and costs nothing. */
	for (uint32_t i = 0U; i < rx_count; i++) {
#ifdef CONFIG_GDB_STUB
		/* 0x03 is the gdb stub's break-in byte (board_console_break_hook,
		 * weak here meaning "no stub"; strong in tx_glue). Consumed
		 * there, delivered to readline here - where it just cancels
		 * the current line, its ordinary meaning. */
		if (rx_chunk[i] == 0x03U && board_console_break_hook()) {
			continue;
		}
		/* '$': a host attaching to a RUNNING target. The stub owns
		 * the wire from here: hand it the byte plus everything
		 * already batched behind it (the session's first packet;
		 * the ISR-stopped world means nothing further can race),
		 * and stop consuming this batch. The session runs to
		 * completion inside this call; on return the console
		 * ownership is back (rx_gate flushed the FIFO at session
		 * exit), so a plain re-arm below resumes the shell. */
		if (rx_chunk[i] == (uint8_t)'$' && gdb_dbgport_ready() != 0 &&
		    gdb_session_active == 0) {
			gdb_isr_attach('$', &rx_chunk[i + 1U],
				       rx_count - i - 1U);
			goto rearm;
		}
#endif
		/* Overwrite rather than drop: if a person pastes faster than
		 * the shell drains, losing the OLDEST bytes keeps the most
		 * recent input, which is what someone typing wants. The
		 * non-overwriting variant would silently discard what they
		 * just typed. */
		(void)chry_ringbuffer_overwrite_byte(&rx_ring, rx_chunk[i]);
		cherrysh_rx_ringed++;
	}

#ifdef CONFIG_GDB_STUB
rearm:
#endif
	/* Order the ring writes before the wake-up. CherryRB has no barriers
	 * of its own (see the file header), so without this the consumer can
	 * be woken and read stale ring state - the classic single-producer
	 * failure that shows up as occasional dropped characters. */
	__asm__ __volatile__("dsb sy" ::: "memory");

	if (shell_task_id != NULL) {
		(void)osThreadFlagsSetFromISR(shell_task_id, SHELL_INPUT_FLAG);
		cherrysh_rx_wakes++;
	}

	/* Re-arm for the next burst. Must happen after the ring write, or a
	 * byte arriving during the copy could be overwritten before we read
	 * it. */
	(void)uart_console_rx_arm(rx_chunk, SHELL_RX_CHUNK, shell_uart_rx_event);
}

/* CherrySH output callback. Raw and unstamped by design (the prompt, the
 * echo and every redraw must reach the wire byte-for-byte), but under the
 * console's print lock so interactive output never lands inside a driver
 * line - a shell that wrote on no lock is how prompts once ended up
 * spliced into the middle of log lines (prior project, board-proven).
 *
 * Newline contract (print discipline): shell output writes '\n' only; the
 * sink owns the CR LF expansion, and board_console_write_raw drops a CR
 * that precedes an LF, so a legacy "\r\n" string still reaches the wire as
 * one CRLF instead of CR CR LF. */
static uint16_t shell_sput(chry_readline_t *rl, const void *data, uint16_t size)
{
	(void)rl;
	if (data == NULL || size == 0U) {
		return 0U;
	}
	board_console_write_raw((const char *)data, (unsigned int)size);
	return size;
}

/* CherrySH input callback. Returns the number of bytes copied, and 0 when
 * nothing is available - the "no data right now" answer that readline's
 * noblock mode is built around. It must NOT wait here. */
static uint16_t shell_sget(chry_readline_t *rl, void *data, uint16_t size)
{
	uint32_t got;

	(void)rl;
	if (data == NULL || size == 0U) {
		return 0U;
	}

	got = chry_ringbuffer_read(&rx_ring, data, size);
	cherrysh_rx_consumed += got;
	return (uint16_t)got;
}

/* --- shell task ----------------------------------------------------------- */

static chry_shell_t shell;

/* 1KiB of history: enough to scroll back through a session of bring-up
 * commands. Line and prompt buffers follow (line is CONFIG_CSH_LNBUFF_SIZE,
 * the REPL's static line buffer - noblock mode requires it out of stack). */
static char shell_history[1024];
static char shell_prompt_buf[64];
static char shell_line_buf[CONFIG_CSH_LNBUFF_SIZE];

static void shell_task(void *argument)
{
	uint32_t poll_ticks;

	(void)argument;

	/* osThreadFlagsWait's timeout is in ticks, not milliseconds. */
	poll_ticks = osKernelGetTickFreq() / (1000U / SHELL_RX_POLL_MS);
	if (poll_ticks == 0U) {
		poll_ticks = 1U;
	}

	for (;;) {
		/* Block until the RX interrupt says there is something to
		 * read. This is the whole reason the shell does not starve
		 * the system: while idle it is genuinely asleep, not
		 * spinning. The timed wait doubles as the storm-defence
		 * poll and is what draws the first prompt before any input
		 * arrives. */
		(void)osThreadFlagsWait(SHELL_INPUT_FLAG, osFlagsWaitAny,
					poll_ticks);

		/* With noblock enabled this returns 1 immediately when no
		 * complete line has been assembled yet, so a single byte of
		 * input costs one cheap call and the task goes back to
		 * sleep. Input that arrived during the call re-sets the
		 * flag, so nothing is missed. */
		(void)chry_shell_task_repl(&shell);

		/* Loop rather than block again straight away: if the flag
		 * was set more than once while we were working,
		 * osThreadFlagsWait would consume the latch and we would
		 * sleep with input still queued. */
	}
}

/* --- commands ------------------------------------------------------------- */

/* Section bounds provided by the link script. CherrySH finds its commands
 * by walking these, which is why baremetal.ld must KEEP() them. */
extern const chry_syscall_t __fsymtab_start;
extern const chry_syscall_t __fsymtab_end;
extern const chry_sysvar_t __vsymtab_start;
extern const chry_sysvar_t __vsymtab_end;

static int cmd_version(int argc, char **argv)
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	const char *kernel = "Eclipse ThreadX 6.5.1.202602a (cortex_a55 UP)";

	(void)argc;
	(void)argv;
	csh_printf(csh,
		   "freewebcam\n"
		   "  repo      freewebcam\n"
		   "  target    RK3568 ITX, cortex-a55\n"
		   "  kernel    %s\n"
		   "  api       CMSIS-RTOS2\n",
		   kernel);
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_version, version, "version",
			  "print build and target info");

/* Uptime from TWO clocks, deliberately: CNTVCT (board_uptime_parts - the
 * same clock the "[   s.mmm]" log stamps use) and the kernel tick. The
 * boot log and the shell therefore always agree on "when", and a
 * divergence between the two numbers is direct evidence of a dead or
 * racing tick. */
static int cmd_uptime(int argc, char **argv)
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	uint32_t ticks = osKernelGetTickCount();
	uint32_t hz = osKernelGetTickFreq();
	uint32_t usec = 0U;
	uint32_t ums = 0U;

	board_uptime_parts(&usec, &ums);
	csh_printf(csh,
		   "uptime: %u.%03u s (cntvct); tick %u at %u Hz = %u.%03u s\n",
		   (unsigned)usec, (unsigned)ums,
		   (unsigned)ticks, (unsigned)hz,
		   (unsigned)((hz != 0U) ? (ticks / hz) : 0U),
		   (unsigned)((hz != 0U) ? ((ticks % hz) * 1000U / hz) : 0U));
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_uptime, uptime, "uptime",
			  "show time since boot (cntvct vs kernel tick)");

static int cmd_tick(int argc, char **argv)
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	uint32_t t0 = osKernelGetTickCount();
	uint32_t t1;

	/* Sleep a known interval and measure the tick delta. If the tick
	 * were not running, this would hang - which is itself the answer.
	 * osDelay's unit is the tick: 100 ticks is 100 ms at the adjudicated
	 * 1 kHz (spec §3.3.5 - the printed reading is in that context). */
	osDelay(100U);
	t1 = osKernelGetTickCount();
	csh_printf(csh, "tick advanced %u in 100 ms of osDelay\n",
		   (unsigned)(t1 - t0));
	if ((t1 - t0) == 0U) {
		csh_printf(csh, "TICK NOT RUNNING\n");
		return -1;
	}
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_tick, tick, "tick",
			  "check the RTOS tick is advancing");

static int cmd_heap(int argc, char **argv)
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);

	(void)argc;
	(void)argv;
	csh_printf(csh, "heap free=%lu B\n",
		   (unsigned long)xPortGetFreeHeapSize());
	return 0;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_heap, heap, "heap",
			  "system heap: free bytes");

/* Warm reset back to the firmware prompt, the board discipline's preferred
 * way to the U-Boot prompt (the UStone cold cycle stays the crash path).
 * The 20-tick pause (20 ms at 1 kHz) is for the console, not the SoC: the
 * line above is queued in the DW-UART's FIFO, and at 115200 baud a few
 * characters need a millisecond or two to leave the wire - without the
 * wait they would die with the SoC. Then the SMC: on this board it
 * resets, and this task never runs again. */
static int cmd_reboot(int argc, char **argv)
{
	chry_shell_t *csh = CSH_FROM_ARGV(argc, argv);
	int ret;

	(void)argc;
	(void)argv;
	csh_printf(csh, "reboot: PSCI SYSTEM_RESET\n");
	osDelay(20U);

	ret = board_system_reset();
	/* only reachable when the firmware refused (PSCI NOT_SUPPORTED /
	 * an invalid call): the board is still running */
	csh_printf(csh, "reboot: firmware refused the reset (%d)\n", ret);
	return ret;
}

CSH_CMD_EXPORT_ALIAS_FULL(cmd_reboot, reboot, "reboot",
			  "warm reset to U-Boot through PSCI SYSTEM_RESET");

/* cherrysh resolves every command name against a PATH variable from the
 * variable table; with an empty variable table PATH is NULL and every
 * command reports "not found" no matter how correctly it was exported.
 * Register "/bin" - the section all the exports above land in - as a
 * read-only variable, through the macro so it lands in VSymTab where the
 * linker script's __vsymtab bounds expect it. */
static const char csh_path_value[] = "/bin";
CSH_RVAR_EXPORT(csh_path_value, PATH, sizeof(csh_path_value));

/* --- bring-up ------------------------------------------------------------- */

int cherrysh_init(void)
{
	chry_shell_init_t init = { 0 };
	osThreadId_t id;

	/* RX ring first: the interrupt can fire as soon as it is enabled,
	 * and it writes into this. */
	if (chry_ringbuffer_init(&rx_ring, rx_ring_storage,
				 sizeof(rx_ring_storage)) != 0) {
		return -1;
	}

	/* Install the console callbacks and the command tables. */
	init.sput = shell_sput;
	init.sget = shell_sget;
	init.command_table_beg = &__fsymtab_start;
	init.command_table_end = &__fsymtab_end;
	init.variable_table_beg = &__vsymtab_start;
	init.variable_table_end = &__vsymtab_end;
	init.prompt_buffer = shell_prompt_buf;
	init.prompt_buffer_size = sizeof(shell_prompt_buf);
	init.history_buffer = shell_history;
	init.history_buffer_size = sizeof(shell_history);
	init.line_buffer = shell_line_buf;
	init.line_buffer_size = sizeof(shell_line_buf);
	init.host = "rk3568";
	init.user[0] = "root";

	if (chry_shell_init(&shell, &init) != 0) {
		return -1;
	}

	/* The shell task: normal priority. It is blocked almost all the
	 * time, so its priority barely matters - what matters is that it
	 * can be preempted. */
	id = osThreadNew(shell_task, NULL,
			 &(osThreadAttr_t){ .name = "shell",
					    .stack_size = CONFIG_SHELL_STACK,
					    .priority = osPriorityNormal });
	if (id == NULL) {
		return -1;
	}
	shell_task_id = id;

	/* Arm the reception LAST, and only now. Everything above has to be
	 * in place first: the driver's ISR drains the FIFO into rx_chunk
	 * and calls shell_uart_rx_event(), which writes into the ring and
	 * wakes the task. Enabling the interrupt before the task exists
	 * would have that callback run with nothing to wake. From this line
	 * on, a byte can arrive at any moment - which is also why this
	 * whole function must run from task context (cherrysh_adapter.h). */
	if (uart_console_rx_arm(rx_chunk, SHELL_RX_CHUNK,
				shell_uart_rx_event) != 0) {
		return -1;
	}

	return 0;
}
