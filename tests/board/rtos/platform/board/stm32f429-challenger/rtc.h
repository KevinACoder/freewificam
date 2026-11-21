/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rtc.h - the STM32F429 RTC: LSE calendar, LSI fallback, backup registers.
 *
 * The board carries a 32.768 kHz crystal on OSC32_IN/OSC32_OUT and a CR1220
 * holder on VBAT, so the RTC is the platform's one battery-backed wall clock.
 * The driver is first-party register code (the F429 board layer's rule: only
 * the wired-Ethernet HAL is imported) and names no RTOS type: it runs before
 * the scheduler from main() and later from a thread.
 *
 * Time convention: the calendar registers hold UTC.  The local-time offset is
 * the system time service's (app/rtos_time.h), not the RTC's.
 *
 * Backup-register schema (the backup domain keeps these across a pin reset and,
 * with the battery fitted, across power-off):
 *   DR0  magic 0x52544331 ("RTC1"), "this firmware has configured the calendar"
 *   DR1  boot counter, incremented once per rtc_board_init()
 *   DR2  epoch of the last successful set (written by the time service)
 *
 * The magic is deliberately not the vendor example's 0x32F2: a foreign
 * calendar (the factory/vendor demo left one running on this bench board) must
 * be distinguishable from one this firmware wrote, so the first boot can take
 * ownership instead of silently keeping a source this board did not choose.
 */

#ifndef FREEWIFICAM_STM32F429_RTC_H
#define FREEWIFICAM_STM32F429_RTC_H

#include <stdint.h>

#define RTC_BKP_MAGIC		0x52544331UL	/* "RTC1" */
#define RTC_BKP_DR_MAGIC	0U
#define RTC_BKP_DR_BOOTS	1U
#define RTC_BKP_DR_LAST_SET	2U
#define RTC_BKP_COUNT		20U

enum rtc_clock_source {
	RTC_CLOCK_NONE = 0,
	RTC_CLOCK_LSE,
	RTC_CLOCK_LSI,
};

struct rtc_calendar {
	uint16_t year;		/* 2000..2099 */
	uint8_t month;		/* 1..12 */
	uint8_t day;		/* 1..31 */
	uint8_t wday;		/* 1 = Monday .. 7 = Sunday */
	uint8_t hour;		/* 0..23 */
	uint8_t minute;		/* 0..59 */
	uint8_t second;		/* 0..59 */
};

struct rtc_status {
	int ready;
	int source;		/* enum rtc_clock_source */
	int lse_ready;
	int lsi_ready;
	int configured;		/* DR0 magic */
	int took_ownership;	/* this init reset a foreign backup domain */
	uint32_t boots;		/* DR1 */
	uint32_t last_set;	/* DR2 */
	uint32_t bdcr;		/* raw RCC_BDCR */
	uint32_t csr;		/* raw RCC_CSR (reset flags + LSI) */
	uint32_t cr;		/* raw RTC_CR */
	uint32_t tr;		/* raw RTC_TR */
	uint32_t dr;		/* raw RTC_DR */
	uint32_t isr;		/* raw RTC_ISR */
	uint32_t prer;		/* raw RTC_PRER */
};

/*
 * Bring the RTC up.  LSE is tried first (default drive for 1.5 s, then high
 * drive for another 1.5 s); LSI is the fallback.  An existing RTCEN/RTCSEL is
 * kept when the backup magic is ours; when the domain carries a foreign
 * calendar (magic mismatch, e.g. the factory demo's 0x32F2) and
 * CONFIG_F429_RTC_TAKE_OWNERSHIP is set, the backup domain is reset once so
 * this board's source can be selected.  Idempotent: the first call does the
 * work and later calls return its result.  No calendar value is written here;
 * the time service decides whether the stored one is usable.
 *
 * Returns 0 when the RTC is running (LSE or LSI), a negative named error
 * otherwise:
 *   -2 no clock source, -3 no source could be started, -4 RTC never
 *   synchronised, -5 prescaler/init-mode failure, -6 12-hour format.
 */
int rtc_board_init(void);

/* 1 when rtc_board_init() has run and the RTC is running. */
int rtc_board_ready(void);

/* Stable TR/DR read: a second live between the two register reads is retried.
 * Returns 0 and a validated calendar, -2 on an invalid calendar, -3 when no
 * stable read was obtained. */
int rtc_board_get(struct rtc_calendar *out);

/* Write the calendar in init mode (WPR unlock, INIT/INITF, TR/DR, lock).
 * Returns 0, -1 on an invalid calendar, -2 when the RTC is not ready. */
int rtc_board_set(const struct rtc_calendar *in);

/* Field ranges, including the month length for the year. */
int rtc_board_valid(const struct rtc_calendar *in);

/* Raw snapshot for the boot line's evidence and the acceptance case. */
void rtc_board_status(struct rtc_status *out);

/* DR0 magic accessors; the time service owns the policy. */
int rtc_board_configured(void);
void rtc_board_mark_configured(void);

/* Backup register 0..19.  Out-of-range indices are ignored / read as 0. */
uint32_t rtc_board_bkp_read(unsigned int idx);
void rtc_board_bkp_write(unsigned int idx, uint32_t value);

#endif /* FREEWIFICAM_STM32F429_RTC_H */
