/*
 * @file   tx_up_shim.c
 * @brief  The SMP-port protection pair reduced to its single-core meaning.
 *
 * The CMSIS adapter (os/cmsis2/cmsis2_threadx.c, from T3) and the tlsf heap
 * take their critical sections through
 * _tx_thread_smp_protect/_tx_thread_smp_unprotect. Under the SMP kernel
 * those are port spinlocks; the UP kernel does not provide them - its own
 * critical section IS the DAIF mask (tx_port.h TX_DISABLE/TX_RESTORE).
 * This shim supplies the pair so adapter code stays kernel-shape agnostic.
 *
 * Semantics: protect() returns the interrupted DAIF posture and masks
 * IRQ+FIQ; unprotect() restores the posture wholesale. On one core with the
 * kernel up, that is the whole mutual-exclusion story.
 */

#include <stdint.h>
#include "arch_asm.h"

unsigned int _tx_thread_smp_protect(void)
{
	unsigned int daif;

	ARCH_DAIF_GET(daif);
	ARCH_DAIF_SET(3);
	return daif;
}

void _tx_thread_smp_unprotect(unsigned int posture)
{
	ARCH_DAIF_PUT(posture);
}
