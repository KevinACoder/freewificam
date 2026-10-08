/**
 * @file    tlsf_heap.c
 * @brief   Case tlsf-heap: the board heap allocates, round-trips and frees.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  case body of the regression table (tests/board/common/board_suite.h);
 *          exercises the allocation seam the CMSIS-RTOS2 backend hands its
 *          thread stacks from (os/threadx/heap.h:20-21) over the board heap's
 *          window (os/threadx/board/rk3568-itx/heap.c), whose ready anchor the
 *          judge checks separately.
 */

#include "board_suite.h"

#include "os/threadx/heap.h"

#define HEAP_BLOCK 1024U

static unsigned char heap_pattern( unsigned i )
{
	return ( unsigned char )( i * 7U + 3U );
}

static void tlsf_heap( void )
{
	unsigned char *block;
	unsigned char *again;

	block = pvPortMalloc( HEAP_BLOCK );
	if( block == 0 ) {
		board_case_fail( "first allocation failed" );
		return;
	}
	for( unsigned i = 0; i < HEAP_BLOCK; i++ ) {
		block[i] = heap_pattern( i );
	}
	for( unsigned i = 0; i < HEAP_BLOCK; i++ ) {
		if( block[i] != heap_pattern( i ) ) {
			board_case_fail( "pattern read-back differs" );
			vPortFree( block );
			return;
		}
	}
	vPortFree( block );

	again = pvPortMalloc( HEAP_BLOCK );
	if( again == 0 ) {
		board_case_fail( "re-allocation after free failed" );
		return;
	}
	vPortFree( again );
}

BOARD_TEST( "tlsf-heap", tlsf_heap );
