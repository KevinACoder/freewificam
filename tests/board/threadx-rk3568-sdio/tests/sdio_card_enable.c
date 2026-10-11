/**
 * @file    sdio_card_enable.c
 * @brief   Case sdio-card-enable: the card's own power sequence, run over the
 *          single-byte path, and the extended read it is meant to unlock.
 * @author  zhugengyu
 * @date    12.10.2026
 * @design  case row of the suite's table (tests/board/common/board_suite.h).
 *
 * This is the one card-specific case in this suite, and it is card-specific on
 * purpose. The bus layer's own criteria (sdio-datapath) treat the part as an
 * SDIO device and nothing more; this case exists because the part does not: its
 * command interface answers single-byte transfers from the moment it is
 * enumerated, while its extended (multi-byte) transfers stay dead until its
 * internal power sequence has run. A stack that does only what the SDIO
 * specification asks for sees a card that accepts an extended command with a
 * clean response and then moves no bytes at all - which is exactly the symptom
 * this case was written against.
 *
 * The sequence below is the card vendor's, replayed through the seam's
 * single-byte operation, which is the only path that answers before the
 * sequence has run. The vendor's own driver states the same constraint in
 * as many words and routes every access through its single-byte fallback until
 * the power step has completed. Its permanent home is the driver line that will
 * own this part's chip-specific bring-up; it is carried here because the bus
 * line's extended criteria cannot be met without it.
 *
 * The case's claim is a conjunction, because the second half is the reason the
 * first half exists: every poll of the sequence must satisfy, and an extended
 * read of the card's register window taken afterwards must return the same
 * bytes as reading that window one byte at a time.
 *
 * Anchors (the judge checks each on PASS):
 *   sdio: card pre hci=.. ctrl=.. pwr=.. cr=....
 *   sdio: card enable polls=.. cr=....
 *   sdio: card post hci=.. ctrl=.. pwr=.. cr=....
 *   sdio: card ext read local=.. cr=..
 *   sdio: card ext read matches the byte path
 */

#include "board_suite.h"
#include "sdio_cases.h"

#include "lkpi_sdio_backend.h"

#include <string.h>

#include "log.h"

/*
 * The card's two register windows, addressed as the card's own driver addresses
 * them. Both are reached through function 1: the SDIO-local window sits at the
 * bottom of function 1's address space, and the chip's own registers sit one
 * device window above it, at 8 << 13.
 */
#define CARD_LOCAL( off )	( (off) & 0x0fffu )
#define CARD_WLAN( off )	( 0x10000u | ( (off) & 0x1fffu ) )

/* The registers the power sequence touches. */
#define CARD_REG_RSV_CTRL	0x001cu	/* unlock the ISO/clock/power control */
#define CARD_REG_HSUS_CTRL	0x0086u	/* the host-interface suspend latch */
#define CARD_REG_SYS_CTRL	0x0005u	/* the chip's power-state control */
#define CARD_REG_SYS_PWR	0x0006u	/* the chip's power-ready status */
#define CARD_REG_RF_QSEL	0x0027u	/* the crystal's quality selector */
#define CARD_REG_CR		0x0100u	/* MAC DMA / WMAC / scheduler enables */

/* The bits, by the role the sequence gives them. */
#define CARD_HSUS_SUSPEND	0x01u	/* set: the host interface is held */
#define CARD_HSUS_RESUMED	0x02u	/* set: the host interface has resumed */
#define CARD_CTRL_SW_LPS	0x04u	/* software low-power state */
#define CARD_CTRL_SUSPEND	0x08u	/* wireless suspend */
#define CARD_CTRL_UNUSED	0x10u	/* cleared alongside the suspend bit */
#define CARD_CTRL_HWPDN		0x80u	/* hardware power-down */
#define CARD_CTRL_ACTIVATE	0x01u	/* set, then polled back to zero */
#define CARD_PWR_READY		0x02u	/* set: the chip's power is up */

/* The block-enable bits the power-on step sets in the card's control register:
 * host TX/RX DMA, the MAC's TX/RX DMA, the protocol and scheduler engines, the
 * security engine and the calibration timer. */
#define CARD_CR_ENABLE_BITS	0x063fu

/* The crystal selector value the sequence leaves behind. */
#define CARD_RF_QSEL_VALUE	0x35u

/*
 * The poll budget. The vendor's parser polls every 10 us; each poll here is a
 * whole single-byte transfer on the bus, which is slower than that by itself,
 * so the count is the budget and no separate delay is inserted.
 */
#define CARD_POLL_LIMIT		5000u

static int card_rd8( unsigned int addr, uint8_t *out )
{
	return lkpi_sdio_be_io_direct( 1U, addr, false, out );
}

static int card_wr8( unsigned int addr, uint8_t value )
{
	return lkpi_sdio_be_io_direct( 1U, addr, true, &value );
}

/*
 * Read-modify-write: clear the bits `mask` selects, then set the bits `mask`
 * selects out of `value`. Every step of the sequence is written in that shape,
 * and it is the shape the vendor's parser implements.
 */
static int card_rmw8( unsigned int addr, uint8_t mask, uint8_t value )
{
	uint8_t v = 0U;
	int st;

	st = card_rd8( addr, &v );
	if( st != 0 ) {
		return st;
	}

	v = (uint8_t)( ( v & (uint8_t)~mask ) | ( value & mask ) );

	return card_wr8( addr, v );
}

/* Poll until the masked read equals the masked wanted value; returns the number
 * of reads it took, or -1 when the budget ran out or a transfer failed. */
static int card_poll8( unsigned int addr, uint8_t mask, uint8_t want )
{
	for( int i = 0; i < (int)CARD_POLL_LIMIT; i++ ) {
		uint8_t v = 0U;

		if( card_rd8( addr, &v ) != 0 ) {
			return -1;
		}
		if( ( v & mask ) == ( want & mask ) ) {
			return i;
		}
	}

	return -1;
}

/*
 * What the card says about its own power state, read over the single-byte path
 * (which answers whether or not the sequence has run). Printed before and after
 * the sequence, because the difference between those two lines is the evidence
 * that the sequence did anything at all.
 */
static void card_state( const char *when )
{
	uint8_t hci = 0xffU;
	uint8_t ctrl = 0xffU;
	uint8_t pwr = 0xffU;
	uint8_t lo = 0xffU;
	uint8_t hi = 0xffU;
	unsigned int cr = 0xffffU;

	if( card_rd8( CARD_LOCAL( CARD_REG_HSUS_CTRL ), &hci ) != 0 ||
	    card_rd8( CARD_WLAN( CARD_REG_SYS_CTRL ), &ctrl ) != 0 ||
	    card_rd8( CARD_WLAN( CARD_REG_SYS_PWR ), &pwr ) != 0 ||
	    card_rd8( CARD_WLAN( CARD_REG_CR ), &lo ) != 0 ||
	    card_rd8( CARD_WLAN( CARD_REG_CR ) + 1U, &hi ) != 0 ) {
		LOG_ANCHOR( "sdio: card %s unreadable\n", when );
		return;
	}

	cr = (unsigned int)lo | ( (unsigned int)hi << 8 );

	/* The 0x prefixes are written out rather than left to the alternate form:
	 * that form drops the prefix for a zero value, and a register that reads
	 * zero is exactly the reading these lines exist to report. */
	LOG_ANCHOR( "sdio: card %s hci=0x%02x ctrl=0x%02x pwr=0x%02x cr=0x%04x\n",
		    when, hci, ctrl, pwr, cr );
}

/*
 * The card's power sequence, in the order the vendor's table gives it. Each
 * step's result is folded into one return value; the caller reports it, and the
 * state lines say which step mattered.
 */
static int card_enable( int *poll_hci, int *poll_pwr )
{
	int st;

	/* unlock the ISO/clock/power control block */
	st = card_wr8( CARD_WLAN( CARD_REG_RSV_CTRL ), 0x00U );
	if( st != 0 ) {
		return st;
	}

	/* leave the host-interface suspend latch, then wait for it to report
	 * itself resumed */
	st = card_rmw8( CARD_LOCAL( CARD_REG_HSUS_CTRL ), CARD_HSUS_SUSPEND, 0x00U );
	if( st != 0 ) {
		return st;
	}
	*poll_hci = card_poll8( CARD_LOCAL( CARD_REG_HSUS_CTRL ), CARD_HSUS_RESUMED,
				CARD_HSUS_RESUMED );
	if( *poll_hci < 0 ) {
		return -1;
	}

	/* the card-emulation to active transition: clear the wireless-suspend
	 * and unused bits, drop the software low-power state, then wait for the
	 * chip to report its power ready */
	st = card_rmw8( CARD_WLAN( CARD_REG_SYS_CTRL ),
			(uint8_t)( CARD_CTRL_SUSPEND | CARD_CTRL_UNUSED ), 0x00U );
	if( st != 0 ) {
		return st;
	}
	st = card_rmw8( CARD_WLAN( CARD_REG_SYS_CTRL ), CARD_CTRL_SW_LPS, 0x00U );
	if( st != 0 ) {
		return st;
	}
	*poll_pwr = card_poll8( CARD_WLAN( CARD_REG_SYS_PWR ), CARD_PWR_READY,
				CARD_PWR_READY );
	if( *poll_pwr < 0 ) {
		return -1;
	}

	/* clear the hardware power-down request and the suspend bit, raise the
	 * activate bit and wait for the chip to take it back down */
	st = card_rmw8( CARD_WLAN( CARD_REG_SYS_CTRL ), CARD_CTRL_HWPDN, 0x00U );
	if( st != 0 ) {
		return st;
	}
	st = card_rmw8( CARD_WLAN( CARD_REG_SYS_CTRL ), CARD_CTRL_SUSPEND, 0x00U );
	if( st != 0 ) {
		return st;
	}
	st = card_rmw8( CARD_WLAN( CARD_REG_SYS_CTRL ), CARD_CTRL_ACTIVATE,
			CARD_CTRL_ACTIVATE );
	if( st != 0 ) {
		return st;
	}
	if( card_poll8( CARD_WLAN( CARD_REG_SYS_CTRL ), CARD_CTRL_ACTIVATE, 0x00U ) < 0 ) {
		return -1;
	}

	/* the crystal's quality selector, then the block enables */
	st = card_wr8( CARD_WLAN( CARD_REG_RF_QSEL ), CARD_RF_QSEL_VALUE );
	if( st != 0 ) {
		return st;
	}
	st = card_wr8( CARD_WLAN( CARD_REG_CR ), 0x00U );
	if( st != 0 ) {
		return st;
	}
	st = card_rmw8( CARD_WLAN( CARD_REG_CR ), (uint8_t)CARD_CR_ENABLE_BITS,
			(uint8_t)CARD_CR_ENABLE_BITS );
	if( st != 0 ) {
		return st;
	}

	return card_rmw8( CARD_WLAN( CARD_REG_CR ) + 1U,
			  (uint8_t)( CARD_CR_ENABLE_BITS >> 8 ),
			  (uint8_t)( CARD_CR_ENABLE_BITS >> 8 ) );
}

/*
 * One extended read, against the same window read one byte at a time. The
 * single-byte path answers either way, so the comparison is the whole test: an
 * extended transfer that is refused shows up as a failure or as bytes that do
 * not match.
 */
static int card_ext_read( const char *name, unsigned int addr, uint8_t *out )
{
	uint8_t byte_path[4];
	int st;

	(void)memset( out, 0, 4U );

	st = lkpi_sdio_be_io_extended( 1U, addr, out, 4U, LKPI_SDIO_BE_F_INCR );
	LOG_ANCHOR( "sdio: card ext read %s rc=%d bytes=%02x%02x%02x%02x\n", name, st,
		    out[0], out[1], out[2], out[3] );

	for( unsigned int i = 0U; i < 4U; i++ ) {
		if( card_rd8( addr + i, &byte_path[i] ) != 0 ) {
			LOG_ERROR( "sdio: card %s: the byte path failed at +%u\n", name, i );
			return -1;
		}
	}

	if( st != 0 ) {
		return st;
	}
	if( memcmp( out, byte_path, 4U ) != 0 ) {
		LOG_ERROR( "sdio: card %s: extended %02x %02x %02x %02x, byte path "
			   "%02x %02x %02x %02x\n", name, out[0], out[1], out[2], out[3],
			   byte_path[0], byte_path[1], byte_path[2], byte_path[3] );
		return -1;
	}

	return 0;
}

void sdio_card_enable_chain( void )
{
	uint8_t probe = 0U;
	uint8_t local[4];
	uint8_t cr[4];
	int poll_hci = -1;
	int poll_pwr = -1;
	int st;

	/* The card is up from the enum case; a register read that fails says so
	 * without this case having to know how the port keeps its card. */
	if( card_rd8( CARD_WLAN( CARD_REG_SYS_CTRL ), &probe ) != 0 ) {
		board_case_fail( "the card is not up (the enum case must run first)" );
		return;
	}

	card_state( "pre" );

	st = card_enable( &poll_hci, &poll_pwr );
	LOG_ANCHOR( "sdio: card enable rc=%d polls=%d/%d\n", st, poll_hci, poll_pwr );

	card_state( "post" );

	if( st != 0 ) {
		board_case_fail( "the card's power sequence did not complete" );
		return;
	}

	/* The vendor's own post-power reference point is the card's control
	 * register; the other window is the one this suite's extended ladder
	 * starts from, so both are read. */
	if( card_ext_read( "cr", CARD_WLAN( CARD_REG_CR ), cr ) != 0 ) {
		board_case_fail( "the extended read of the card's control window failed" );
		return;
	}
	if( card_ext_read( "local", CARD_LOCAL( 0x0000U ), local ) != 0 ) {
		board_case_fail( "the extended read of the card's local window failed" );
		return;
	}

	LOG_ANCHOR( "sdio: card ext read matches the byte path\n" );
}

BOARD_TEST( "sdio-card-enable", sdio_card_enable_chain );
