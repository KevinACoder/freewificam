/**
 * @file    cmsis2_support.h
 * @brief   The two seams the shared CMSIS-RTOS2 backend needs from its platform.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  proxy - the same declarations resolve to a host or a board implementation
 *          by compile set; ref os/threadx/heap.h:20-21 (the allocation seam) and
 *          os/cmsis2/cmsis2.h:35-46 (the end-scheduler seam).
 *
 * cmsis2_threadx.c is compiled on both sides and two of its needs cannot come from
 * the kernel itself: a diagnostic outlet (the host prints to stderr, the board has
 * the stamped console sink) and the kernel entry (the host port enters the kernel
 * directly, the board must hand over through the SPSel stub). Declared here,
 * implemented per side:
 *   host  - os/threadx/cmsis2_host.c
 *   board - os/threadx/board/rk3568-itx/cmsis2/cmsis2_board.c
 */

#ifndef CMSIS2_SUPPORT_H
#define CMSIS2_SUPPORT_H

/* Diagnostics from the backend (the priority map, deferred-create failures,
 * allocation failures). Host: stderr, byte-identical to the historical behaviour
 * (stdout stays the clean result channel of the host smokes). Board: the stamped
 * console sink, one line per message. */
void cmsis2_log( const char * fmt, ... );

/* Enter the kernel. Host: tx_kernel_enter(). Board: tx_kernel_enter_spsel0() - the
 * SPSel hand-over stub, because the board image runs with SPSel=0 and the port has
 * no SPSel handling of its own. Never returns on either side. */
void cmsis2_tx_kernel_enter( void );

#endif /* CMSIS2_SUPPORT_H */
