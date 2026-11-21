/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rtc.c - the STM32F429 calendar and system date/time acceptance case.
 *
 * One boot runs the whole ladder and prints one judged line per rung:
 *
 *   rtc: init ...        RTC running, source, backup-domain state
 *   rtc: regs ...        raw BDCR/CSR/CR/TR/DR/ISR/PRER
 *   rtc: entry ...       the system date/time the image booted with
 *   rtc: persist ...     entry time against the last set, across the reset
 *   rtc: set ...         write a known calendar through rtos_time_set_tm()
 *   rtc: readback ...    read it back through the RTC driver
 *   rtc: convert ...     eight epoch<->calendar round trips, including leap days
 *   rtc: advance ...     the second counter really moves
 *   rtc: posix ...       newlib time() agrees with the system clock
 *   rtc: PASS / FAIL
 *
 * The case's own PASS gates the driver and the service.  Persistence is
 * printed but not gated here: a battery-backed board can enter the first boot
 * with an old calendar, and a first boot legitimately has nothing to compare
 * against.  The run-level judge asks for persist=required on the pin-reset
 * boot only.
 */

#include <stdint.h>
#include <string.h>
#include <time.h>

#include "cases.h"
#include "os/cmsis2/cmsis2.h"

#include "rtc.h"
#include "rtos_time.h"

#define RTC_CASE_SET_EPOCH	1767323045UL	/* 2026-01-02 03:04:05 UTC */
#define RTC_CASE_SET_YEAR	2026U
#define RTC_CASE_SET_MONTH	1U
#define RTC_CASE_SET_DAY	2U
#define RTC_CASE_SET_WDAY	5U
#define RTC_CASE_SET_HOUR	3U
#define RTC_CASE_SET_MINUTE	4U
#define RTC_CASE_SET_SECOND	5U

#define RTC_CASE_PERSIST_MAX_S	600L

struct rtc_case_conv {
	uint32_t epoch;
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t wday;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
};

static const struct rtc_case_conv rtc_conv[] = {
	{ 0UL,          1970U, 1U,  1U,  4U, 0U,  0U,  0U },
	{ 946684800UL,  2000U, 1U,  1U,  6U, 0U,  0U,  0U },
	{ 951827696UL,  2000U, 2U,  29U, 2U, 12U, 34U, 56U },
	{ 1709164800UL, 2024U, 2U,  29U, 4U, 0U,  0U,  0U },
	{ 1767225599UL, 2025U, 12U, 31U, 3U, 23U, 59U, 59U },
	{ 1767323045UL, 2026U, 1U,  2U,  5U, 3U,  4U,  5U },
	{ 2147483648UL, 2038U, 1U,  19U, 2U, 3U,  14U, 8U },
	{ 4102444799UL, 2099U, 12U, 31U, 4U, 23U, 59U, 59U },
};

static const char *rtc_source_name(int source)
{
	if (source == (int)RTC_CLOCK_LSE) {
		return "lse";
	}
	if (source == (int)RTC_CLOCK_LSI) {
		return "lsi";
	}
	return "none";
}

static int conv_matches(const struct rtos_tm *tm, const struct rtc_case_conv *c)
{
	return tm->year == c->year && tm->month == c->month &&
	       tm->day == c->day && tm->wday == c->wday &&
	       tm->hour == c->hour && tm->minute == c->minute &&
	       tm->second == c->second;
}

static int cal_matches(const struct rtc_calendar *cal)
{
	return cal->year == RTC_CASE_SET_YEAR &&
	       cal->month == RTC_CASE_SET_MONTH &&
	       cal->day == RTC_CASE_SET_DAY &&
	       cal->wday == RTC_CASE_SET_WDAY &&
	       cal->hour == RTC_CASE_SET_HOUR &&
	       cal->minute == RTC_CASE_SET_MINUTE &&
	       cal->second == RTC_CASE_SET_SECOND;
}

int case_rtc_run(void)
{
	struct rtos_time_info info;
	struct rtc_status status;
	struct rtc_calendar cal = { 0 };
	struct rtos_tm tm = { 0 };
	struct rtos_tm set_tm;
	const char *reason = NULL;
	char utc[24] = "";
	char local[32] = "";
	uint32_t entry;
	uint32_t t0;
	uint32_t t1;
	uint32_t readback_epoch;
	long delta;
	unsigned int conv_fail = 0;
	unsigned int i;
	int rc = 0;

	if (rtos_time_info_get(&info) != 0 || info.ready == 0) {
		rtc_board_status(&status);
		CASE_ANCHOR("rtc: init rc=-1 src=%s lse=%d lsi=%d boot=%lu "
			    "configured=%d owned=%d\n",
			    rtc_source_name(status.source), status.lse_ready,
			    status.lsi_ready, (unsigned long)status.boots,
			    status.configured, status.took_ownership);
		CASE_ANCHOR("rtc: FAIL reason=no-time\n");
		return 1;
	}

	rtc_board_status(&status);
	CASE_ANCHOR("rtc: init rc=0 src=%s lse=%d lsi=%d boot=%lu "
		    "configured=%d owned=%d\n",
		    rtos_time_source_name(info.source), info.lse_ready,
		    info.lsi_ready, (unsigned long)info.boot_count,
		    info.configured_at_init, status.took_ownership);

	CASE_ANCHOR("rtc: regs bdcr=%08lx csr=%08lx cr=%08lx tr=%08lx "
		    "dr=%08lx isr=%08lx prer=%08lx\n",
		    (unsigned long)status.bdcr, (unsigned long)status.csr,
		    (unsigned long)status.cr, (unsigned long)status.tr,
		    (unsigned long)status.dr, (unsigned long)status.isr,
		    (unsigned long)status.prer);

	if (info.source != RTOS_TIME_SRC_LSE || info.lse_ready == 0) {
		if (reason == NULL) {
			reason = "src-not-lse";
		}
	}

	entry = rtos_time_epoch();
	if (entry == 0U || rtos_time_get_tm(&tm, 0) != 0 ||
	    entry < RTOS_TIME_EPOCH_MIN || entry > RTOS_TIME_EPOCH_MAX) {
		if (reason == NULL) {
			reason = "bad-entry";
		}
		entry = 0U;
		memset(&tm, 0, sizeof(tm));
	}
	(void)rtos_time_format_utc(utc, sizeof(utc));
	(void)rtos_time_format_local(local, sizeof(local));
	CASE_ANCHOR("rtc: entry %s UTC epoch=%lu wday=%u local=%s\n",
		    utc, (unsigned long)entry, (unsigned int)tm.wday, local);

	if (entry != 0U) {
		struct rtos_tm local_tm = { 0 };
		uint32_t local_epoch = 0;

		if (rtos_time_get_tm(&local_tm, 1) != 0 ||
		    rtos_time_tm_to_epoch(&local_tm, &local_epoch) != 0 ||
		    (int64_t)local_epoch -
		    (int64_t)info.tz_offset_min * 60 != (int64_t)entry) {
			if (reason == NULL) {
				reason = "local";
			}
		}
	}

	if (info.first_boot != 0) {
		CASE_ANCHOR("rtc: persist boot=%lu skip=first-boot\n",
			    (unsigned long)info.boot_count);
	} else {
		uint32_t last = info.last_set;
		uint32_t now = entry;
		long dt = (last != 0U && now >= last) ?
			  (long)(now - last) : -1L;
		int ok = (last != 0U && now >= last &&
			  dt >= 0L && dt <= RTC_CASE_PERSIST_MAX_S) ? 1 : 0;

		CASE_ANCHOR("rtc: persist boot=%lu last=%lu now=%lu dt=%ld ok=%d\n",
			    (unsigned long)info.boot_count,
			    (unsigned long)last, (unsigned long)now, dt, ok);
	}

	/*
	 * Set a known calendar and read it back.  The retry covers the one bad
	 * instant: the RTC ticks to the next second between the write and the
	 * read, which would make an exact readback look like a driver error.
	 */
	{
		int set_ok = 0;
		int attempt;

		set_tm.year = RTC_CASE_SET_YEAR;
		set_tm.month = RTC_CASE_SET_MONTH;
		set_tm.day = RTC_CASE_SET_DAY;
		set_tm.wday = RTC_CASE_SET_WDAY;
		set_tm.hour = RTC_CASE_SET_HOUR;
		set_tm.minute = RTC_CASE_SET_MINUTE;
		set_tm.second = RTC_CASE_SET_SECOND;

		for (attempt = 0; attempt < 3; attempt++) {
			rc = rtos_time_set_tm(&set_tm, 0);
			if (rc != 0) {
				continue;
			}
			if (rtc_board_get(&cal) != 0) {
				continue;
			}
			if (cal_matches(&cal)) {
				set_ok = 1;
				break;
			}
		}

		CASE_ANCHOR("rtc: set %04u-%02u-%02u %02u:%02u:%02u UTC "
			    "epoch=%lu want=%lu wday=%u rc=%d\n",
			    (unsigned int)set_tm.year,
			    (unsigned int)set_tm.month,
			    (unsigned int)set_tm.day,
			    (unsigned int)set_tm.hour,
			    (unsigned int)set_tm.minute,
			    (unsigned int)set_tm.second,
			    (unsigned long)(set_ok ? RTC_CASE_SET_EPOCH :
					    rtos_time_epoch()),
			    (unsigned long)RTC_CASE_SET_EPOCH,
			    (unsigned int)set_tm.wday, rc);
		if (set_ok == 0 && reason == NULL) {
			reason = "set";
		}

		readback_epoch = 0U;
		if (set_ok != 0) {
			struct rtos_tm rb_tm;

			rb_tm.year = cal.year;
			rb_tm.month = cal.month;
			rb_tm.day = cal.day;
			rb_tm.wday = cal.wday;
			rb_tm.hour = cal.hour;
			rb_tm.minute = cal.minute;
			rb_tm.second = cal.second;
			(void)rtos_time_tm_to_epoch(&rb_tm, &readback_epoch);
		}
		CASE_ANCHOR("rtc: readback %04u-%02u-%02u %02u:%02u:%02u UTC "
			    "epoch=%lu wday=%u ok=%d\n",
			    (unsigned int)cal.year, (unsigned int)cal.month,
			    (unsigned int)cal.day, (unsigned int)cal.hour,
			    (unsigned int)cal.minute, (unsigned int)cal.second,
			    (unsigned long)readback_epoch,
			    (unsigned int)cal.wday, set_ok);
		if (set_ok == 0 && reason == NULL) {
			reason = "readback";
		}
	}

	for (i = 0U; i < (unsigned int)(sizeof(rtc_conv) /
				       sizeof(rtc_conv[0])); i++) {
		const struct rtc_case_conv *c = &rtc_conv[i];
		struct rtos_tm conv_tm;
		uint32_t back;

		if (rtos_time_epoch_to_tm(c->epoch, &conv_tm) != 0 ||
		    conv_matches(&conv_tm, c) == 0 ||
		    rtos_time_tm_to_epoch(&conv_tm, &back) != 0 ||
		    back != c->epoch) {
			conv_fail++;
		}
	}
	CASE_ANCHOR("rtc: convert cases=%u fail=%u\n",
		    (unsigned int)(sizeof(rtc_conv) / sizeof(rtc_conv[0])),
		    conv_fail);
	if (conv_fail != 0U && reason == NULL) {
		reason = "convert";
	}

	t0 = rtos_time_epoch();
	(void)osDelay(2200U);
	t1 = rtos_time_epoch();
	delta = (t1 >= t0) ? (long)(t1 - t0) : -1L;
	{
		int ok = (t1 > t0 && delta >= 2L && delta <= 3L) ? 1 : 0;

		CASE_ANCHOR("rtc: advance t0=%lu t1=%lu delta=%ld ok=%d\n",
			    (unsigned long)t0, (unsigned long)t1, delta, ok);
		if (ok == 0 && reason == NULL) {
			reason = "advance";
		}
	}

#if defined(CONFIG_F429_RTC_POSIX_TIME)
	{
		time_t now = time(NULL);
		uint32_t epoch = rtos_time_epoch();
		long diff = (long)now - (long)epoch;
		int ok = (now != (time_t)-1 && diff >= -2L && diff <= 2L) ? 1 : 0;

		CASE_ANCHOR("rtc: posix time=%ld epoch=%lu ok=%d\n",
			    (long)now, (unsigned long)epoch, ok);
		if (ok == 0 && reason == NULL) {
			reason = "posix";
		}
	}
#else
	CASE_ANCHOR("rtc: posix skip=disabled\n");
#endif

	if (reason != NULL) {
		CASE_ANCHOR("rtc: FAIL reason=%s\n", reason);
		return 1;
	}
	CASE_ANCHOR("rtc: PASS\n");
	return 0;
}
