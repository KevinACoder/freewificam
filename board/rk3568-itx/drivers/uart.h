/*
 * @file   uart.h
 * @brief  Console UART2 early polled path (board interface).
 */

#ifndef FREEWEBCAM_UART_H
#define FREEWEBCAM_UART_H

#include <stdbool.h>
#include <stdint.h>

/* Fully re-program the UART for polled 115200 8N1. MUST run before the first
 * polled write: a UART inherited from the loader has unreliable THRE
 * semantics (short bursts inside the FIFO get out, then bytes drop or the
 * poll wedges). */
void uart_early_init(void);

/* Polled byte output; unbounded THRE wait (a bounded timeout is actively
 * harmful here - see uart.c); expands a bare '\n' to CR LF on the wire. */
void uart_early_putc(char c);
void uart_early_puts(const char *s);

/* 8 lowercase hex digits, no prefix. */
void uart_early_put_hex32(uint32_t value);

/* Non-blocking RX: 1 with *out filled when a byte is waiting, 0 otherwise
 * (the gdb stub's transport; the console itself has no reader this round). */
bool uart_early_try_getc(int *out);

/* Fault-path speaker: re-programs the UART UNCONDITIONALLY, then prints the
 * message. A fault handler that cannot speak is indistinguishable from a
 * dead board, and a fault may fire before any init. */
void uart_early_panic(const char *message);

/* --- interrupt reception (S4-a, SPEC cherrysh-adapt §3.4) ------------------- */

#ifdef CONFIG_UART_RX_IRQ

/* Completion event bits for uart_rx_event_cb_t. */
#define UART_RX_EVENT_COMPLETE	0x1U

typedef void (*uart_rx_event_cb_t)(uint32_t events, uint32_t rx_count);

/* Arm one interrupt-driven reception: the ISR drains the RX FIFO into buf
 * (batch, not per-byte - the re-arm-per-byte shape is how a pasted line
 * once reached a shell as its first character) and then calls cb with the
 * byte count actually received. The consumer re-arms from the callback.
 *
 * ORDER IS LOAD-BEARING (board-proven in the prior project, 2026-09-21):
 * the client buffer goes in BEFORE the RX interrupt is enabled - enabling
 * first means boot-time bytes land in driver-internal scratch: the count
 * is right, the bytes are somewhere else, and the first line typed after
 * boot silently vanishes. Never re-runs the polled console setup here -
 * re-initializing a live console is how output silently stops.
 *
 * Returns 0 on success. Registers console INTID (board_conf.h) on the
 * first call; the polled path above is untouched. */
int uart_console_rx_arm(uint8_t *buf, uint32_t len, uart_rx_event_cb_t cb);

/* 1 once uart_console_rx_arm has run (the console RX interrupt is the
 * byte owner; the tick-side gdb poll must stand down or the two race
 * for the same FIFO). */
int uart_console_rx_armed(void);

/* Session-boundary console ownership (the gdb stub's intr_ctrl):
 * stub_owns=1 masks the console INTID at the GIC - the shell's RX
 * interrupt goes silent for the length of a stub session; stub_owns=0
 * flushes whatever the session left in the FIFO (session-typed bytes
 * must not resurface as shell input), clears the line, and re-enables
 * the interrupt iff armed. */
void uart_console_rx_gate(int stub_owns);

/* One-line RX-path health check (bring-up): UART IER/IIR/LSR + the GIC
 * enable/pending bit for the console INTID + the ISR counters. */
void uart_console_rx_diag(void);

#endif /* CONFIG_UART_RX_IRQ */

#endif /* FREEWEBCAM_UART_H */
