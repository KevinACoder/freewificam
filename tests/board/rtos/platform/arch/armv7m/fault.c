/* SPDX-License-Identifier: BSD-3-Clause */
/* fault.c - ARMv7-M exception entry points the vector table names. */

#include <stdint.h>

#include "uart.h"

static uint32_t fault_msp(void)
{
	uint32_t value;
	__asm__ __volatile__("mrs %0, msp" : "=r"(value));
	return value;
}

static uint32_t fault_psp(void)
{
	uint32_t value;
	__asm__ __volatile__("mrs %0, psp" : "=r"(value));
	return value;
}

static void fault_dump(const char *kind)
{
	uint32_t lr;
	uint32_t cfsr = *(volatile uint32_t *)0xE000ED28UL;
	uint32_t hfsr = *(volatile uint32_t *)0xE000ED2CUL;
	uint32_t mmfar = *(volatile uint32_t *)0xE000ED34UL;
	uint32_t bfar = *(volatile uint32_t *)0xE000ED38UL;
	uint32_t *frame;

	__asm__ __volatile__("mov %0, lr" : "=r"(lr));
	frame = (uint32_t *)((lr & 4UL) ? fault_psp() : fault_msp());

	uart_early_panic("fatal: ");
	uart_early_puts(kind);
	uart_early_puts("\ncfsr=");
	uart_early_put_hex32(cfsr);
	uart_early_puts(" hfsr=");
	uart_early_put_hex32(hfsr);
	uart_early_puts(" mmfar=");
	uart_early_put_hex32(mmfar);
	uart_early_puts(" bfar=");
	uart_early_put_hex32(bfar);
	uart_early_puts("\npc=");
	uart_early_put_hex32(frame[6]);
	uart_early_puts(" lr=");
	uart_early_put_hex32(frame[5]);
	uart_early_puts(" xpsr=");
	uart_early_put_hex32(frame[7]);
	uart_early_puts("\n");
}

#define FAULT_HANDLER(name, label) \
	void name(void) \
	{ \
		fault_dump(label); \
		for (;;) { \
			__asm__ __volatile__("wfe"); \
		} \
	}

FAULT_HANDLER(HardFault_Handler, "HardFault")
FAULT_HANDLER(MemManage_Handler, "MemManage")
FAULT_HANDLER(BusFault_Handler, "BusFault")
FAULT_HANDLER(UsageFault_Handler, "UsageFault")

void NMI_Handler(void)
{
	uart_early_panic("fatal: NMI\n");
	for (;;) {
		__asm__ __volatile__("wfe");
	}
}

void DebugMon_Handler(void)
{
	for (;;) {
		__asm__ __volatile__("wfe");
	}
}
