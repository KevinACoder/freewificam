/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rtos_time.h - the board's system wall clock.
 *
 * One clock for the image: a Unix epoch read from the board's battery-backed
 * RTC, a calendar view in UTC or in the configured local offset, and a fixed
 * "YYYY-MM-DD HH:MM:SS" formatter.  The service owns the first-boot policy
 * (write the configured default only when the backup domain carries no valid
 * calendar) and the backup-register bookkeeping; the RTC driver owns the
 * registers.
 *
 * Conventions:
 *   - the RTC holds UTC; the local view is a fixed offset, no DST;
 *   - the epoch is 32-bit, 2000-01-01 .. 2099-12-31 (the RTC's two-digit
 *     year); a conversion helper also accepts 1970 for tests;
 *   - newlib's time_t on 32-bit ARM is signed 32-bit, so time() itself ends
 *     in 2038 even though this API reaches 2099.  Documented, not worked
 *     around in this round.
 */

#ifndef FREEWIFICAM_RTOS_TIME_H
#define FREEWIFICAM_RTOS_TIME_H

#include <stddef.h>
#include <stdint.h>

#define RTOS_TIME_EPOCH_MIN	946684800UL	/* 2000-01-01 00:00:00 UTC */
#define RTOS_TIME_EPOCH_MAX	4102444799UL	/* 2099-12-31 23:59:59 UTC */

enum rtos_time_source {
	RTOS_TIME_SRC_NONE = 0,
	RTOS_TIME_SRC_LSE,
	RTOS_TIME_SRC_LSI,
};

struct rtos_tm {
	uint16_t year;		/* 1970..2099 for conversions, 2000..2099 for the RTC */
	uint8_t month;		/* 1..12 */
	uint8_t day;		/* 1..31 */
	uint8_t wday;		/* 1 = Monday .. 7 = Sunday (computed on output) */
	uint8_t hour;		/* 0..23 */
	uint8_t minute;		/* 0..59 */
	uint8_t second;		/* 0..59 */
};

struct rtos_time_info {
	int ready;
	enum rtos_time_source source;
	int lse_ready;
	int lsi_ready;
	int configured_at_init;	/* the backup magic was present at init */
	int first_boot;		/* init wrote the configured default */
	uint32_t boot_count;
	uint32_t last_set;	/* backup DR2 */
	uint32_t default_epoch;
	int tz_offset_min;
};

/*
 * Bring the RTC up and make the clock readable.  When the backup domain has
 * no marker or an invalid calendar, the configured default epoch is written
 * and marked; otherwise the stored calendar is kept.  Idempotent.
 * Returns 0 when the clock is usable, -1 otherwise.
 */
int rtos_time_init(void);

int rtos_time_ready(void);

/* Current epoch, or 0 when the clock is not ready / not readable.  The RTC's
 * range starts in 2000, so 0 is unambiguous. */
uint32_t rtos_time_epoch(void);

/* Set the clock; the RTC, backup DR2 and the marker are written on success.
 * Returns 0, or the RTC driver's negative code: -1 a value outside the RTC
 * range / not ready, -2 the RTC is not running, -3 the write/readback did not
 * agree, -4 the date did not latch. */
int rtos_time_set_epoch(uint32_t epoch);

/* Calendar in UTC (local == 0) or in the configured offset (local != 0). */
int rtos_time_get_tm(struct rtos_tm *tm, int local);
int rtos_time_set_tm(const struct rtos_tm *tm, int local);

/* "YYYY-MM-DD HH:MM:SS" in UTC, or "... +0800" for the local view.
 * Returns 0, -1 when not ready or the buffer is too small. */
int rtos_time_format_utc(char *buf, size_t len);
int rtos_time_format_local(char *buf, size_t len);

int rtos_time_info_get(struct rtos_time_info *out);
const char *rtos_time_source_name(enum rtos_time_source src);

/* Pure conversions used by the acceptance case as well.  epoch_to_tm computes
 * wday; tm_to_epoch validates the fields and ignores the caller's wday.
 * Return 0 or -1. */
int rtos_time_epoch_to_tm(uint32_t epoch, struct rtos_tm *tm);
int rtos_time_tm_to_epoch(const struct rtos_tm *tm, uint32_t *epoch);

#endif /* FREEWIFICAM_RTOS_TIME_H */
