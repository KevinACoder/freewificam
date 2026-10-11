/**
 * @file    cmsis2_board.c
 * @brief   Board side of the CMSIS-RTOS2 backend: the creation hook and the two
 *          support seams.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  callback registration - the kernel hands the application exactly one
 *          pre-scheduler window (tx_application_define) and the board glue calls
 *          tx_board_app_define() from it (ref os/threadx/board/rk3568-itx/
 *          tx_glue.c:48); the CMSIS backend's deferred-create replay runs here.
 *          proxy - the two seams declared in os/threadx/cmsis2_support.h are
 *          implemented per side; this is the board implementation.
 *
 * The include order below is load-bearing: tx_api.h pulls tx_port.h, which
 * force-includes the flavour of tx_user.h the include path selected. The gate
 * right after it asserts that the flavour in effect IS this image's CMSIS
 * flavour - a build with the bring-up flavour (no CMSIS slot backlink) would
 * otherwise fail later with an obscure error inside the backend.
 */

#include "tx_api.h"

#ifndef OS_THREADX_BOARD_RK3568_CMSIS2_TX_USER_H
#error "the CMSIS2 flavour of the ThreadX board configuration is not the one in effect"
#endif

#include <stdarg.h>

#include "cmsis2_support.h"
#include "log.h"
#include "minilibc.h"

/* The backend's deferred-create replay: threads and other objects claimed
 * before the kernel start call are created there, in the only window ThreadX
 * allows pre-scheduler object creation. */
extern void cmsis2_tx_application_define( void );

/* The SPSel hand-over stub (tx_vectors.S): enters the kernel with SP_EL0 = the
 * boot stack. The port itself has no SPSel handling, and this image runs with
 * SPSel=0 everywhere, so the board must enter through here. Never returns. */
extern void tx_kernel_enter_spsel0( void );

void tx_board_app_define( void )
{
    cmsis2_tx_application_define();
}

void cmsis2_log( const char * fmt, ... )
{
    /* 192 bytes is roomy for every message the backend carries (the longest is
     * a thread name plus a format head). board_log assembles the line and adds
     * the stamp and the CR LF - one outlet for stamped output, no second sink. */
    char buf[192];
    va_list ap;

    va_start( ap, fmt );
    ( void ) vsnprintf( buf, sizeof( buf ), fmt, ap );
    va_end( ap );

    LOG_INFO( "%s", buf );
}

void cmsis2_tx_kernel_enter( void )
{
    tx_kernel_enter_spsel0();
}
