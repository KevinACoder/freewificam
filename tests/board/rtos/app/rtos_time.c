/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rtos_time.c - the system wall clock: RTC calendar <-> Unix epoch.
 *
 * The RTC driver (platform/board/stm32f429-challenger/rtc.c) owns the
 * registers; this file owns the calendar arithmetic, the first-boot default,
 * the local offset and the newlib time() seam.  The civil-date conversion is
 * Howard Hinnant's days_from_civil/civil_from_days pair, exact over the whole
 * 1970..2099 range this service accepts.
 *
 * One writer is assumed: this round's callers are main() before the scheduler
 * and the runner thread's acceptance case.  A future SNTP client must add its
 * own serialisation at this layer, not in the board driver.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(CONFIG_F429_RTC_POSIX_TIME)
#include <sys/time.h>
#endif

#include "rtc.h"
#include "rtos_time.h"

static struct rtos_time_info time_info;
static int time_ready;
static int time_init_done;
static int time_init_rc = -1;

static int leap_year(uint16_t year)
{
	return ((year % 4U) == 0U && (year % 100U) != 0U) ||
	       ((year % 400U) == 0U);
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

/* Days since 1970-01-01 for a proleptic Gregorian date. */
static int64_t days_from_civil(int year, unsigned int month, unsigned int day)
{
	int64_t era;
	unsigned int yoe;
	unsigned int doy;
	unsigned int doe;

	year -= (month <= 2U) ? 1 : 0;
	era = (year >= 0 ? year : year - 399) / 400;
	yoe = (unsigned int)(year - (int)(era * 400));
	doy = (153U * (month > 2U ? month - 3U : month + 9U) + 2U) / 5U +
	      day - 1U;
	doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *year, unsigned int *month,
			    unsigned int *day)
{
	int64_t era;
	unsigned int doe;
	unsigned int yoe;
	int y;
	unsigned int doy;
	unsigned int mp;
	unsigned int d;
	unsigned int m;

	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = (unsigned int)(z - era * 146097);
	yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
	y = (int)yoe + (int)(era * 400);
	doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
	mp = (5U * doy + 2U) / 153U;
	d = doy - (153U * mp + 2U) / 5U + 1U;
	m = (mp < 10U) ? (mp + 3U) : (mp - 9U);
	*year = y + ((m <= 2U) ? 1 : 0);
	*month = m;
	*day = d;
}

static enum rtos_time_source map_source(int source)
{
	if (source == (int)RTC_CLOCK_LSE) {
		return RTOS_TIME_SRC_LSE;
	}
	if (source == (int)RTC_CLOCK_LSI) {
		return RTOS_TIME_SRC_LSI;
	}
	return RTOS_TIME_SRC_NONE;
}

static int calendar_to_epoch(const struct rtc_calendar *cal, uint32_t *epoch)
{
	int64_t days;
	int64_t secs;

	if (cal == NULL || epoch == NULL) {
		return -1;
	}
	if (rtc_board_valid(cal) == 0) {
		return -1;
	}

	days = days_from_civil(cal->year, cal->month, cal->day);
	secs = days * 86400 +
	       (int64_t)cal->hour * 3600 +
	       (int64_t)cal->minute * 60 +
	       (int64_t)cal->second;
	if (secs < 0 || secs > (int64_t)RTOS_TIME_EPOCH_MAX) {
		return -1;
	}
	*epoch = (uint32_t)secs;
	return 0;
}

int rtos_time_epoch_to_tm(uint32_t epoch, struct rtos_tm *tm)
{
	int64_t days;
	int64_t rem;
	int year;
	unsigned int month;
	unsigned int day;

	if (tm == NULL) {
		return -1;
	}
	if (epoch > RTOS_TIME_EPOCH_MAX) {
		return -1;
	}

	days = (int64_t)epoch / 86400;
	rem = (int64_t)epoch % 86400;
	civil_from_days(days, &year, &month, &day);
	if (year < 1970 || year > 2099) {
		return -1;
	}

	tm->year = (uint16_t)year;
	tm->month = (uint8_t)month;
	tm->day = (uint8_t)day;
	tm->hour = (uint8_t)(rem / 3600);
	tm->minute = (uint8_t)((rem / 60) % 60);
	tm->second = (uint8_t)(rem % 60);
	tm->wday = (uint8_t)(((days + 3) % 7) + 1);
	return 0;
}

int rtos_time_tm_to_epoch(const struct rtos_tm *tm, uint32_t *epoch)
{
	int64_t days;
	int64_t secs;

	if (tm == NULL || epoch == NULL) {
		return -1;
	}
	if (tm->year < 1970U || tm->year > 2099U) {
		return -1;
	}
	if (tm->month < 1U || tm->month > 12U) {
		return -1;
	}
	if (tm->day < 1U || tm->day > month_days(tm->year, tm->month)) {
		return -1;
	}
	if (tm->hour > 23U || tm->minute > 59U || tm->second > 59U) {
		return -1;
	}

	days = days_from_civil(tm->year, tm->month, tm->day);
	secs = days * 86400 +
	       (int64_t)tm->hour * 3600 +
	       (int64_t)tm->minute * 60 +
	       (int64_t)tm->second;
	if (secs < 0 || secs > (int64_t)RTOS_TIME_EPOCH_MAX) {
		return -1;
	}
	*epoch = (uint32_t)secs;
	return 0;
}

int rtos_time_set_epoch(uint32_t epoch)
{
	struct rtos_tm tm;
	struct rtc_calendar cal;
	int rc;

	if (time_ready == 0) {
		return -1;
	}
	if (epoch < RTOS_TIME_EPOCH_MIN || epoch > RTOS_TIME_EPOCH_MAX) {
		return -1;
	}
	if (rtos_time_epoch_to_tm(epoch, &tm) != 0) {
		return -1;
	}

	cal.year = tm.year;
	cal.month = tm.month;
	cal.day = tm.day;
	cal.wday = tm.wday;
	cal.hour = tm.hour;
	cal.minute = tm.minute;
	cal.second = tm.second;

	rc = rtc_board_set(&cal);
	if (rc != 0) {
		return rc;
	}
	rtc_board_bkp_write(RTC_BKP_DR_LAST_SET, epoch);
	rtc_board_mark_configured();
	time_info.last_set = epoch;
	return 0;
}

uint32_t rtos_time_epoch(void)
{
	struct rtc_calendar cal;
	uint32_t epoch;

	if (time_ready == 0) {
		return 0U;
	}
	if (rtc_board_get(&cal) != 0) {
		return 0U;
	}
	if (calendar_to_epoch(&cal, &epoch) != 0) {
		return 0U;
	}
	return epoch;
}

int rtos_time_get_tm(struct rtos_tm *tm, int local)
{
	uint32_t epoch;
	int64_t adjusted;

	if (tm == NULL || time_ready == 0) {
		return -1;
	}
	epoch = rtos_time_epoch();
	if (epoch == 0U) {
		return -1;
	}

	adjusted = (int64_t)epoch;
	if (local != 0) {
		adjusted += (int64_t)time_info.tz_offset_min * 60;
	}
	if (adjusted < 0 || adjusted > (int64_t)RTOS_TIME_EPOCH_MAX) {
		return -1;
	}
	return rtos_time_epoch_to_tm((uint32_t)adjusted, tm);
}

int rtos_time_set_tm(const struct rtos_tm *tm, int local)
{
	uint32_t epoch;
	int64_t adjusted;

	if (tm == NULL || time_ready == 0) {
		return -1;
	}
	if (rtos_time_tm_to_epoch(tm, &epoch) != 0) {
		return -1;
	}

	adjusted = (int64_t)epoch;
	if (local != 0) {
		adjusted -= (int64_t)time_info.tz_offset_min * 60;
	}
	if (adjusted < (int64_t)RTOS_TIME_EPOCH_MIN ||
	    adjusted > (int64_t)RTOS_TIME_EPOCH_MAX) {
		return -1;
	}
	return rtos_time_set_epoch((uint32_t)adjusted);
}

int rtos_time_format_utc(char *buf, size_t len)
{
	struct rtos_tm tm;
	int n;

	if (buf == NULL || len < 20U) {
		return -1;
	}
	if (rtos_time_get_tm(&tm, 0) != 0) {
		return -1;
	}
	n = snprintf(buf, len, "%04u-%02u-%02u %02u:%02u:%02u",
		     (unsigned int)tm.year, (unsigned int)tm.month,
		     (unsigned int)tm.day, (unsigned int)tm.hour,
		     (unsigned int)tm.minute, (unsigned int)tm.second);
	return (n > 0 && (size_t)n < len) ? 0 : -1;
}

int rtos_time_format_local(char *buf, size_t len)
{
	struct rtos_tm tm;
	int offset;
	int hours;
	int minutes;
	char sign;
	int n;

	if (buf == NULL || len < 26U) {
		return -1;
	}
	if (rtos_time_get_tm(&tm, 1) != 0) {
		return -1;
	}

	offset = time_info.tz_offset_min;
	sign = (offset < 0) ? '-' : '+';
	if (offset < 0) {
		offset = -offset;
	}
	hours = offset / 60;
	minutes = offset % 60;

	n = snprintf(buf, len, "%04u-%02u-%02u %02u:%02u:%02u %c%02d%02d",
		     (unsigned int)tm.year, (unsigned int)tm.month,
		     (unsigned int)tm.day, (unsigned int)tm.hour,
		     (unsigned int)tm.minute, (unsigned int)tm.second,
		     sign, hours, minutes);
	return (n > 0 && (size_t)n < len) ? 0 : -1;
}

int rtos_time_info_get(struct rtos_time_info *out)
{
	if (out == NULL) {
		return -1;
	}
	*out = time_info;
	return 0;
}

const char *rtos_time_source_name(enum rtos_time_source src)
{
	switch (src) {
	case RTOS_TIME_SRC_LSE:
		return "lse";
	case RTOS_TIME_SRC_LSI:
		return "lsi";
	default:
		return "none";
	}
}

int rtos_time_init(void)
{
	struct rtc_status status;
	struct rtc_calendar cal;
	uint32_t default_epoch = (uint32_t)CONFIG_F429_RTC_DEFAULT_EPOCH;
	int configured;
	int rc;

	if (time_init_done != 0) {
		return time_init_rc;
	}

	memset(&time_info, 0, sizeof(time_info));
	time_info.default_epoch = default_epoch;
	time_info.tz_offset_min = CONFIG_F429_RTC_TZ_OFFSET_MIN;

	rc = rtc_board_init();
	if (rc != 0) {
		time_init_done = 1;
		time_init_rc = -1;
		return -1;
	}

	rtc_board_status(&status);
	time_info.source = map_source(status.source);
	time_info.lse_ready = status.lse_ready;
	time_info.lsi_ready = status.lsi_ready;
	time_info.boot_count = status.boots;
	time_info.last_set = status.last_set;

	configured = rtc_board_configured();
	if (configured != 0) {
		if (rtc_board_get(&cal) != 0 || rtc_board_valid(&cal) == 0) {
			configured = 0;
		}
	}
	time_info.configured_at_init = configured;

	time_ready = 1;
	if (configured == 0) {
		if (rtos_time_set_epoch(default_epoch) != 0) {
			time_ready = 0;
			time_init_done = 1;
			time_init_rc = -1;
			return -1;
		}
		time_info.first_boot = 1;
		time_info.last_set = default_epoch;
	}

	time_info.ready = 1;
	time_init_done = 1;
	time_init_rc = 0;
	return 0;
}

int rtos_time_ready(void)
{
	return (time_ready != 0) ? 1 : 0;
}

#if defined(CONFIG_F429_RTC_POSIX_TIME)
/*
 * newlib's time() -> _gettimeofday_r() -> _gettimeofday(): this definition is
 * the syscall seam.  The image links -lnosys, whose stub would otherwise claim
 * the symbol; a strong definition in the image's objects wins.  Seconds
 * resolution only.
 */
int _gettimeofday(struct timeval *tv, void *tz);

int _gettimeofday(struct timeval *tv, void *tz)
{
	uint32_t epoch;

	(void)tz;

	if (time_ready == 0) {
		return -1;
	}
	epoch = rtos_time_epoch();
	if (epoch == 0U) {
		return -1;
	}
	if (tv != NULL) {
		tv->tv_sec = (time_t)epoch;
		tv->tv_usec = 0;
	}
	return 0;
}
#endif /* CONFIG_F429_RTC_POSIX_TIME */
