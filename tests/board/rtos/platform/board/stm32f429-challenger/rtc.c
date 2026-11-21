/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rtc.c - the STM32F429 RTC: LSE 32.768 kHz calendar with an LSI fallback.
 *
 * Register-level, first-party, no RTOS header: main() calls rtc_board_init()
 * before the scheduler, and the system time service calls the calendar
 * accessors later.  The sequence follows RM0090's RTC chapter:
 *
 *   - PWREN + DBP, then RCC_BDCR owns LSEON/LSEDRV/RTCSEL/RTCEN;
 *   - LSE is tried at the default drive, then at high drive, then LSI;
 *   - 1 Hz from 32.768 kHz is (127+1)*(255+1); the LSI fallback assumes its
 *     32 kHz nominal, (127+1)*(249+1), and is documented as imprecise;
 *   - PRER and the 24-hour format are written in init mode under WPR;
 *   - TR/DR are read with the TR/DR/TR stable-read loop and written back in
 *     init mode.
 *
 * Timeouts use the DWT cycle counter, which the board's rtos_clock_init()
 * already enables before main() calls this driver; this file enables it too so
 * the driver stands alone.  At 168 MHz a two-second timeout is ~336 M cycles,
 * inside the counter's 32-bit range and far below its ~25.6 s wrap.
 */

#include <stdint.h>

#include "board.h"
#include "rtc.h"

#define RCC_BASE	0x40023800UL
#define RCC_APB1ENR	(*(volatile uint32_t *)(RCC_BASE + 0x40))
#define RCC_BDCR	(*(volatile uint32_t *)(RCC_BASE + 0x70))
#define RCC_CSR		(*(volatile uint32_t *)(RCC_BASE + 0x74))

#define PWR_BASE	0x40007000UL
#define PWR_CR		(*(volatile uint32_t *)(PWR_BASE + 0x00))

#define RTC_BASE	0x40002800UL
#define RTC_TR		(*(volatile uint32_t *)(RTC_BASE + 0x00))
#define RTC_DR		(*(volatile uint32_t *)(RTC_BASE + 0x04))
#define RTC_CR		(*(volatile uint32_t *)(RTC_BASE + 0x08))
#define RTC_ISR		(*(volatile uint32_t *)(RTC_BASE + 0x0C))
#define RTC_PRER	(*(volatile uint32_t *)(RTC_BASE + 0x10))
#define RTC_WPR		(*(volatile uint32_t *)(RTC_BASE + 0x24))
#define RTC_BKP_BASE	(RTC_BASE + 0x50UL)

#define RCC_APB1ENR_PWREN	(1UL << 28)
#define PWR_CR_DBP		(1UL << 8)

#define RCC_BDCR_LSEON		(1UL << 0)
#define RCC_BDCR_LSERDY		(1UL << 1)
#define RCC_BDCR_LSEDRV_MASK	(3UL << 3)
#define RCC_BDCR_RTCSEL_MASK	(3UL << 8)
#define RCC_BDCR_RTCSEL_LSE	(1UL << 8)
#define RCC_BDCR_RTCSEL_LSI	(2UL << 8)
#define RCC_BDCR_RTCEN		(1UL << 15)
#define RCC_BDCR_BDRST		(1UL << 16)

#define RCC_CSR_LSION		(1UL << 0)
#define RCC_CSR_LSIRDY		(1UL << 1)

#define RTC_CR_FMT		(1UL << 6)
#define RTC_ISR_INIT		(1UL << 7)
#define RTC_ISR_INITF		(1UL << 6)
#define RTC_ISR_RSF		(1UL << 5)

#define RTC_WPR_UNLOCK1		0xCAUL
#define RTC_WPR_UNLOCK2		0x53UL
#define RTC_WPR_LOCK		0xFFUL

#define RTC_PREDIV_A		127U
#define RTC_PREDIV_S_LSE	255U
#define RTC_PREDIV_S_LSI	249U

#define DWT_DEMCR		(*(volatile uint32_t *)0xE000EDFCUL)
#define DWT_CTRL		(*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT		(*(volatile uint32_t *)0xE0001004UL)
#define DEMCR_TRCENA		(1UL << 24)
#define DWT_CTRL_CYCCNTENA	(1UL << 0)

static int rtc_init_done;
static int rtc_init_rc = -1;
static int rtc_source = RTC_CLOCK_NONE;
static int rtc_took_ownership;

static void dwt_enable(void)
{
	DWT_DEMCR |= DEMCR_TRCENA;
	DWT_CTRL |= DWT_CTRL_CYCCNTENA;
}

/* Busy-wait microseconds on the DWT counter; the spin cap is the same
 * anti-hang guard as wait_set/wait_clear for a dead counter. */
static void rtc_delay_us(uint32_t us)
{
	uint32_t hz = (SystemCoreClock != 0U) ? SystemCoreClock : 16000000UL;
	uint32_t start = DWT_CYCCNT;
	uint32_t limit = (hz / 1000000U) * us;
	uint32_t spins = 0U;

	if (limit == 0U) {
		limit = 1U;
	}
	while ((uint32_t)(DWT_CYCCNT - start) < limit &&
	       ++spins < us * 1000U) {
	}
}

static int backup_domain_reset(void)
{
	RCC_BDCR |= RCC_BDCR_BDRST;
	rtc_delay_us(10U);
	RCC_BDCR &= ~RCC_BDCR_BDRST;
	rtc_delay_us(100U);
	return 0;
}

static uint32_t timeout_cycles(uint32_t ms)
{
	uint32_t hz = (SystemCoreClock != 0U) ? SystemCoreClock : 16000000UL;
	uint64_t cycles = ((uint64_t)(hz / 1000U)) * ms;

	if (cycles == 0ULL) {
		cycles = 1ULL;
	}
	if (cycles > 0xFFFFFFFFULL) {
		cycles = 0xFFFFFFFFULL;
	}
	return (uint32_t)cycles;
}

/*
 * The cycle comparison is the timeout; the spin cap is the anti-hang guard for
 * a core whose DWT cycle counter was never enabled (a counter that does not
 * move would otherwise make the loop infinite).  It is far above every cycle
 * limit this file asks for, so it only fires on that broken-counter path.
 */
#define RTC_WAIT_SPIN_CAP	0x0FFFFFFFUL

static int wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t ms)
{
	uint32_t start = DWT_CYCCNT;
	uint32_t limit = timeout_cycles(ms);
	uint32_t spins = 0U;

	while ((*reg & mask) != mask) {
		if ((uint32_t)(DWT_CYCCNT - start) >= limit ||
		    ++spins >= RTC_WAIT_SPIN_CAP) {
			return -1;
		}
	}
	return 0;
}

static int wait_clear(volatile uint32_t *reg, uint32_t mask, uint32_t ms)
{
	uint32_t start = DWT_CYCCNT;
	uint32_t limit = timeout_cycles(ms);
	uint32_t spins = 0U;

	while ((*reg & mask) != 0U) {
		if ((uint32_t)(DWT_CYCCNT - start) >= limit ||
		    ++spins >= RTC_WAIT_SPIN_CAP) {
			return -1;
		}
	}
	return 0;
}

static int lse_start(uint32_t high_drive, uint32_t ms)
{
	uint32_t bdcr = RCC_BDCR;

	bdcr &= ~RCC_BDCR_LSEDRV_MASK;
	bdcr |= RCC_BDCR_LSEON;
	if (high_drive != 0U) {
		bdcr |= RCC_BDCR_LSEDRV_MASK;
	}
	RCC_BDCR = bdcr;

	return wait_set(&RCC_BDCR, RCC_BDCR_LSERDY, ms);
}

static int lsi_start(uint32_t ms)
{
	RCC_CSR |= RCC_CSR_LSION;
	return wait_set(&RCC_CSR, RCC_CSR_LSIRDY, ms);
}

static int rtc_write_unlock(void)
{
	RTC_WPR = RTC_WPR_UNLOCK1;
	RTC_WPR = RTC_WPR_UNLOCK2;
	return 0;
}

static void rtc_write_lock(void)
{
	RTC_WPR = RTC_WPR_LOCK;
}

static int rtc_enter_init(void)
{
	RTC_ISR = RTC_ISR_INIT;
	return wait_set(&RTC_ISR, RTC_ISR_INITF, 10U);
}

static int rtc_exit_init(void)
{
	RTC_ISR = 0U;
	return wait_clear(&RTC_ISR, RTC_ISR_INITF, 10U);
}

static int rtc_program_prescaler(int source)
{
	uint32_t prediv_s = (source == RTC_CLOCK_LSI) ?
			    RTC_PREDIV_S_LSI : RTC_PREDIV_S_LSE;
	int rc;

	rtc_write_unlock();
	rc = rtc_enter_init();
	if (rc == 0) {
		RTC_PRER = (RTC_PREDIV_A << 16) | prediv_s;
		RTC_CR &= ~RTC_CR_FMT;		/* 24-hour format */
		rc = rtc_exit_init();
	}
	if (rc == 0 && wait_set(&RTC_ISR, RTC_ISR_RSF, 100U) != 0) {
		rc = -1;
	}
	rtc_write_lock();
	return rc;
}

static uint8_t bcd_to_bin(uint32_t value)
{
	return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

static uint32_t bin_to_bcd(uint32_t value)
{
	return ((value / 10U) << 4) | (value % 10U);
}

static int leap_year(uint16_t year)
{
	return ((year % 4U) == 0U && (year % 100U) != 0U) || ((year % 400U) == 0U);
}

static uint8_t month_days(uint16_t year, uint8_t month)
{
	static const uint8_t days[12] = {
		31U, 28U, 31U, 30U, 31U, 30U,
		31U, 31U, 30U, 31U, 30U, 31U,
	};

	if (month < 1U || month > 12U) {
		return 0U;
	}
	if (month == 2U && leap_year(year)) {
		return 29U;
	}
	return days[month - 1U];
}

static int rtc_fail(int rc)
{
	rtc_init_done = 1;
	rtc_init_rc = rc;
	return rc;
}

static int rtc_configure_fresh(void)
{
	RCC_BDCR &= ~RCC_BDCR_RTCEN;

	if (lse_start(0U, 1500U) == 0 || lse_start(1U, 1500U) == 0) {
		RCC_BDCR = (RCC_BDCR & ~RCC_BDCR_RTCSEL_MASK) |
			   RCC_BDCR_RTCSEL_LSE;
		RCC_CSR &= ~RCC_CSR_LSION;
		rtc_source = RTC_CLOCK_LSE;
	} else {
		RCC_BDCR &= ~RCC_BDCR_LSEON;
		if (lsi_start(500U) != 0) {
			return -3;
		}
		RCC_BDCR = (RCC_BDCR & ~RCC_BDCR_RTCSEL_MASK) |
			   RCC_BDCR_RTCSEL_LSI;
		rtc_source = RTC_CLOCK_LSI;
	}

	RCC_BDCR |= RCC_BDCR_RTCEN;
	if (wait_set(&RTC_ISR, RTC_ISR_RSF, 100U) != 0) {
		return -4;
	}
	if (rtc_program_prescaler(rtc_source) != 0) {
		return -5;
	}
	return 0;
}

int rtc_board_init(void)
{
	uint32_t bdcr;
	uint32_t sel;
	int have_source;
	int ours;
	int rc;

	if (rtc_init_done != 0) {
		return rtc_init_rc;
	}

	dwt_enable();
	RCC_APB1ENR |= RCC_APB1ENR_PWREN;
	PWR_CR |= PWR_CR_DBP;

	bdcr = RCC_BDCR;
	sel = (bdcr >> 8) & 3U;
	have_source = ((bdcr & RCC_BDCR_RTCEN) != 0U &&
		       (sel == 1U || sel == 2U)) ? 1 : 0;
	ours = rtc_board_configured();

	if (have_source != 0 && ours != 0) {
		/*
		 * Our own calendar (pin reset, or the battery behind a power
		 * cut).  Keep the source and confirm its oscillator; changing
		 * RTCSEL would need a backup-domain reset and discard time
		 * this firmware is responsible for.
		 */
		if (sel == 1U && (bdcr & RCC_BDCR_LSERDY) == 0U) {
			if (lse_start(0U, 1500U) != 0) {
				return rtc_fail(-2);
			}
		}
		if (sel == 2U && (RCC_CSR & RCC_CSR_LSIRDY) == 0U) {
			if (lsi_start(500U) != 0) {
				return rtc_fail(-2);
			}
		}
		rtc_source = (sel == 1U) ? RTC_CLOCK_LSE : RTC_CLOCK_LSI;
	} else if (have_source != 0 && ours == 0) {
		/*
		 * A calendar this firmware did not write: the bench board's
		 * factory demo left one running on LSI with the vendor's
		 * magic.  Keeping it would keep the vendor's source; taking
		 * ownership resets the backup domain once and configures this
		 * board's LSE.  With the option off, preserve the foreign
		 * calendar and report its source (the acceptance gate then
		 * sees a non-LSE source and fails rather than pretending).
		 */
#if defined(CONFIG_F429_RTC_TAKE_OWNERSHIP)
		(void)backup_domain_reset();
		rtc_took_ownership = 1;
		rc = rtc_configure_fresh();
		if (rc != 0) {
			return rtc_fail(rc);
		}
#else
		if (sel == 1U && (bdcr & RCC_BDCR_LSERDY) == 0U) {
			if (lse_start(0U, 1500U) != 0) {
				return rtc_fail(-2);
			}
		}
		if (sel == 2U && (RCC_CSR & RCC_CSR_LSIRDY) == 0U) {
			if (lsi_start(500U) != 0) {
				return rtc_fail(-2);
			}
		}
		rtc_source = (sel == 1U) ? RTC_CLOCK_LSE : RTC_CLOCK_LSI;
#endif
	} else {
		/* No source at all: a fresh backup domain. */
		if (sel == 3U) {
#if defined(CONFIG_F429_RTC_TAKE_OWNERSHIP)
			(void)backup_domain_reset();
			rtc_took_ownership = 1;
#else
			return rtc_fail(-3);
#endif
		}
		rc = rtc_configure_fresh();
		if (rc != 0) {
			return rtc_fail(rc);
		}
	}

	if ((RTC_CR & RTC_CR_FMT) != 0U) {
		/* 12-hour interpretation would silently mangle the calendar. */
		return rtc_fail(-6);
	}
	if (wait_set(&RTC_ISR, RTC_ISR_RSF, 100U) != 0) {
		return rtc_fail(-4);
	}

	/*
	 * Boot counter: one increment per init, in the backup domain.  A
	 * cold boot without the battery restarts it at 1; a pin reset or a
	 * battery-backed cold boot continues it.  It is evidence, not a gate.
	 */
	{
		uint32_t boots = rtc_board_bkp_read(RTC_BKP_DR_BOOTS);

		if (boots == 0xFFFFFFFFU) {
			boots = 0U;
		}
		rtc_board_bkp_write(RTC_BKP_DR_BOOTS, boots + 1U);
	}

	rc = 0;
	rtc_init_done = 1;
	rtc_init_rc = rc;
	return rc;
}

int rtc_board_ready(void)
{
	return (rtc_init_done != 0 && rtc_init_rc == 0) ? 1 : 0;
}

int rtc_board_valid(const struct rtc_calendar *in)
{
	if (in == NULL) {
		return 0;
	}
	if (in->year < 2000U || in->year > 2099U) {
		return 0;
	}
	if (in->month < 1U || in->month > 12U) {
		return 0;
	}
	if (in->day < 1U || in->day > month_days(in->year, in->month)) {
		return 0;
	}
	if (in->wday < 1U || in->wday > 7U) {
		return 0;
	}
	if (in->hour > 23U || in->minute > 59U || in->second > 59U) {
		return 0;
	}
	return 1;
}

int rtc_board_get(struct rtc_calendar *out)
{
	uint32_t tr1;
	uint32_t tr2;
	uint32_t dr;
	int tries;

	if (out == NULL || rtc_board_ready() == 0) {
		return -1;
	}

	for (tries = 0; tries < 4; tries++) {
		tr1 = RTC_TR;
		dr = RTC_DR;
		tr2 = RTC_TR;
		if (tr1 != tr2) {
			continue;	/* the second advanced between reads */
		}

		out->hour = bcd_to_bin((tr1 >> 16) & 0x3FU);
		out->minute = bcd_to_bin((tr1 >> 8) & 0x7FU);
		out->second = bcd_to_bin(tr1 & 0x7FU);
		out->year = (uint16_t)(2000U + bcd_to_bin((dr >> 16) & 0xFFU));
		out->month = bcd_to_bin((dr >> 8) & 0x1FU);
		out->day = bcd_to_bin(dr & 0x3FU);
		out->wday = (uint8_t)((dr >> 13) & 0x7U);

		return rtc_board_valid(out) ? 0 : -2;
	}

	return -3;
}

static int rtc_write_dr(uint32_t dr)
{
	int rc;

	rtc_write_unlock();
	rc = rtc_enter_init();
	if (rc == 0) {
		RTC_DR = dr;
		/*
		 * The APB write is synchronised into the RTCCLK domain; a clear
		 * of INIT immediately after the last write dropped the date
		 * on the bench board (the time took, the date did not).  Three
		 * RTCCLK cycles at 32 kHz are ~92 us.
		 */
		rtc_delay_us(100U);
		rc = rtc_exit_init();
	}
	if (rc == 0 && wait_set(&RTC_ISR, RTC_ISR_RSF, 100U) != 0) {
		rc = -1;
	}
	rtc_write_lock();
	return rc;
}

static int rtc_write_tr(uint32_t tr)
{
	int rc;

	rtc_write_unlock();
	rc = rtc_enter_init();
	if (rc == 0) {
		RTC_TR = tr;
		rtc_delay_us(100U);
		rc = rtc_exit_init();
	}
	if (rc == 0 && wait_set(&RTC_ISR, RTC_ISR_RSF, 100U) != 0) {
		rc = -1;
	}
	rtc_write_lock();
	return rc;
}

static int rtc_calendar_eq(const struct rtc_calendar *a,
			   const struct rtc_calendar *b)
{
	return a->year == b->year && a->month == b->month &&
	       a->day == b->day && a->wday == b->wday &&
	       a->hour == b->hour && a->minute == b->minute &&
	       a->second == b->second;
}

int rtc_board_set(const struct rtc_calendar *in)
{
	struct rtc_calendar check;
	uint32_t tr;
	uint32_t dr;
	int rc;
	int tries;
	int date_ok = 0;

	if (rtc_board_valid(in) == 0) {
		return -1;
	}
	if (rtc_board_ready() == 0) {
		return -2;
	}

	tr = (bin_to_bcd(in->hour) << 16) |
	     (bin_to_bcd(in->minute) << 8) |
	     bin_to_bcd(in->second);
	dr = (bin_to_bcd((uint32_t)(in->year - 2000U)) << 16) |
	     (bin_to_bcd(in->month) << 8) |
	     bin_to_bcd(in->day) |
	     ((uint32_t)(in->wday & 0x7U) << 13);

	/*
	 * The date first, then wait for it to take, then the time.  On this
	 * part the DR write latches at the next calendar update, not at INIT
	 * exit: an immediate readback still shows the old date (measured on
	 * the bench: 65 ms after the write the date was old, 2.2 s later it
	 * was the written one).  The poll watches only the date fields; the
	 * time is written last so it is exact, and the final readback can
	 * compare the whole calendar.
	 */
	rc = rtc_write_dr(dr);
	if (rc != 0) {
		return rc;
	}

	for (tries = 0; tries < 150; tries++) {
		if (rtc_board_get(&check) == 0 &&
		    check.year == in->year && check.month == in->month &&
		    check.day == in->day && check.wday == in->wday) {
			date_ok = 1;
			break;
		}
		rtc_delay_us(10000U);	/* 10 ms; the latch is <= one second */
	}
	if (date_ok == 0) {
		return -4;
	}

	rc = rtc_write_tr(tr);
	if (rc != 0) {
		return rc;
	}

	/*
	 * Verify through the shadow registers rather than trusting the write:
	 * a readback that disagrees is a named failure for the caller's retry
	 * (the only legitimate one is the RTC having ticked a second between
	 * the time write and the read).
	 */
	if (rtc_board_get(&check) != 0) {
		return -3;
	}
	return rtc_calendar_eq(&check, in) ? 0 : -3;
}


void rtc_board_status(struct rtc_status *out)
{
	uint32_t bdcr;
	uint32_t sel;

	if (out == NULL) {
		return;
	}

	bdcr = RCC_BDCR;
	sel = (bdcr >> 8) & 3U;
	out->ready = rtc_board_ready();
	if (rtc_source != RTC_CLOCK_NONE) {
		out->source = rtc_source;
	} else if (sel == 1U) {
		out->source = RTC_CLOCK_LSE;
	} else if (sel == 2U) {
		out->source = RTC_CLOCK_LSI;
	} else {
		out->source = RTC_CLOCK_NONE;
	}
	out->lse_ready = ((bdcr & RCC_BDCR_LSERDY) != 0U) ? 1 : 0;
	out->lsi_ready = ((RCC_CSR & RCC_CSR_LSIRDY) != 0U) ? 1 : 0;
	out->configured = rtc_board_configured();
	out->took_ownership = rtc_took_ownership;
	out->boots = rtc_board_bkp_read(RTC_BKP_DR_BOOTS);
	out->last_set = rtc_board_bkp_read(RTC_BKP_DR_LAST_SET);
	out->bdcr = bdcr;
	out->csr = RCC_CSR;
	out->cr = RTC_CR;
	out->tr = RTC_TR;
	out->dr = RTC_DR;
	out->isr = RTC_ISR;
	out->prer = RTC_PRER;
}

int rtc_board_configured(void)
{
	return (rtc_board_bkp_read(RTC_BKP_DR_MAGIC) == RTC_BKP_MAGIC) ? 1 : 0;
}

void rtc_board_mark_configured(void)
{
	rtc_board_bkp_write(RTC_BKP_DR_MAGIC, RTC_BKP_MAGIC);
}

uint32_t rtc_board_bkp_read(unsigned int idx)
{
	if (idx >= RTC_BKP_COUNT) {
		return 0U;
	}
	return ((volatile uint32_t *)RTC_BKP_BASE)[idx];
}

void rtc_board_bkp_write(unsigned int idx, uint32_t value)
{
	if (idx >= RTC_BKP_COUNT) {
		return;
	}
	PWR_CR |= PWR_CR_DBP;
	((volatile uint32_t *)RTC_BKP_BASE)[idx] = value;
}
