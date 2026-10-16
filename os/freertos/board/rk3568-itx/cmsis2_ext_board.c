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

#include <stddef.h>
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

/*
 * The execution-profile query with no profile: the Execution Profile Kit is a
 * ThreadX-side facility (its instrumented kernel is built only in EPK=1
 * images), and the extension header's contract is that every other backend
 * answers with an empty profile so callers need no per-kernel switch. The
 * wired suite's console then prints "unavailable" instead of zeros. The stub
 * was missing and the shared wired case failed to link on this image until it
 * was added.
 */
size_t osExecProfileGet( os_exec_profile_entry_t *entries, size_t max,
			 uint64_t *isr_ticks, uint64_t *idle_ticks )
{
	( void ) entries;
	( void ) max;
	*isr_ticks = 0u;
	*idle_ticks = 0u;
	return 0u;
}
