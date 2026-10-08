/**
 * @file    cmsis2_ext_board.c
 * @brief   Board-side critical section for the CMSIS-RTOS2 extension.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  adapter - ref linux:kernel/locking/spinlock.c:88 (the irqsave
 *          take/release pair, whose state is the caller's saved flag word);
 *          the host's counterpart is os/freertos/cmsis2_ext.c (a process-wide
 *          lock, because pthread signal masking cannot exclude pthreads).
 *
 * The section masks interrupts and returns the previous mask as the opaque
 * state, so exit restores exactly what was there and nesting is per-context by
 * construction. Masking an interrupt from inside one is legal - the kernel
 * runs its own critical sections the same way - so the pair also serves the
 * completion context the contract promises.
 *
 * The raw DAIF access stays in the architecture layer (arch_asm.h); this file
 * only picks the bits and the pairing.
 */

#include <stdint.h>

#include "os/cmsis2/cmsis2.h"

#include "arch_asm.h"

os_critical_state_t osCriticalEnter( void )
{
	uint64_t saved;

	ARCH_DAIF_GET( saved );
	ARCH_IRQ_MASK();

	return ( os_critical_state_t ) saved;
}

void osCriticalExit( os_critical_state_t state )
{
	ARCH_DAIF_PUT( ( uint64_t ) state );
}
