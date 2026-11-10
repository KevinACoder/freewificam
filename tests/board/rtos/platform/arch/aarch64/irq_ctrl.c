/*
 * SPDX-License-Identifier: BSD-3-Clause
 * @file   irq_ctrl.c
 * @brief  CMSIS-Core(A) interrupt-controller API, implemented on the board's
 *         GICv3 registry.
 *
 * The ported board drivers (the PCIe endpoints and the ITS-backed MSI domain)
 * speak the CMSIS-Core(A) vocabulary - IRQ_SetHandler / IRQ_Enable /
 * IRQ_Disable - while this project's GICv3 layer (gicv3.h) exposes
 * irq_register / irq_enable. This file is the bridge between the two shapes and
 * the only place that knows both; the CMSIS declarations themselves come from
 * the vendored CMSIS-Core(A) header, so there is no second copy of the API.
 *
 * Two shape differences are resolved here:
 *
 *   - the handler signature. CMSIS handlers take no argument (void (*)(void));
 *     the GICv3 registry's take a context pointer. One trampoline, registered
 *     with the CMSIS handler as its context, reconciles them, so a caller never
 *     sees the registry's own shape.
 *
 *   - LPIs are not enabled through the CPU interface. An LPI's enable state
 *     lives in the ITS property table, so IRQ_Enable/IRQ_Disable dispatch on
 *     the INTID range: LPIs go to gic_lpi_set_state, everything else to the
 *     GICv3 SPI/PPI enable. Registering the handler is one call for both,
 *     because dispatch is by raw INTID either way.
 *
 * Deliberately not implemented: IRQ_SetPriority. An LPI's priority lives in the
 * ITS property table, not in a per-INTID register the CPU can write, so there
 * is no correct per-INTID priority call for the message-interrupt range; the
 * ITS driver stamps the property byte itself (BOARD_IRQ_PRIORITY_API_CALL_RAW).
 *
 * @author zhugengyu
 * @date   12.10.2026
 */

#include <stdint.h>

#include "gicv3.h"
#include "gicv3_its.h"
#include "irq_ctrl.h"

/* CMSIS handlers take no argument; the GICv3 registry passes one. Register this
 * with the CMSIS handler as its context so the registry's dispatch lands on the
 * right function. */
static void irq_trampoline(void *arg)
{
	((IRQHandler_t)arg)();
}

int32_t IRQ_SetHandler(IRQn_ID_t irqn, IRQHandler_t handler)
{
	if (irqn < 0) {
		return -1;
	}

	return irq_register((uint32_t)irqn, irq_trampoline, (void *)handler);
}

int32_t IRQ_Enable(IRQn_ID_t irqn)
{
	if (irqn < 0) {
		return -1;
	}

	if ((uint32_t)irqn >= IRQ_LPI_INTID_FIRST) {
		gic_lpi_set_state((uint32_t)irqn, 1);
		return 0;
	}

	return irq_enable((uint32_t)irqn);
}

int32_t IRQ_Disable(IRQn_ID_t irqn)
{
	if (irqn < 0) {
		return -1;
	}

	if ((uint32_t)irqn >= IRQ_LPI_INTID_FIRST) {
		gic_lpi_set_state((uint32_t)irqn, 0);
		return 0;
	}

	return irq_disable((uint32_t)irqn);
}
