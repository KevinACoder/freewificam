/*
 * @file    main.c
 * @brief   The console level mechanism's host contract.
 *
 * Two layers are under test. The engine (components/ulog, vendored) owns the
 * level vocabulary, the per-subscriber threshold and the compile-time gate; the
 * shell (os/baremetal/log.c, compiled here unchanged) owns the line assembly,
 * the stamp, the repeat gate and the never-filtered promotion.
 *
 * The point of the suite is the pair of promises the boards depend on: a line a
 * judge reads is never filtered away, and a line the console prints is a whole
 * line. Both are cheap to state and easy to break, so each case can be driven
 * red by the check.sh controls.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "os/baremetal/log.h"
#include "os/baremetal/log_anchors.h"

/* host_shim.c */
const char *ulog_test_wire(void);
void ulog_test_wire_reset(void);
void ulog_test_clock_set(uint64_t us);

static int failed;
static int cases;

static void case_result(const char *id, int ok, const char *detail)
{
	printf("ULOGLEVEL: %s %s %s\n", id, ok ? "PASS" : "FAIL", detail);
	cases++;
	if (!ok) {
		failed++;
	}
}

/* ---- the engine -------------------------------------------------------- */

static int sub_a, sub_b, sub_c, sub_d, sub_e, sub_f, sub_g;
static int seen_a, seen_b, seen_c, seen_d, seen_e, seen_f, seen_g;

static void h_a(ulog_level_t s, char *m) { (void)s; (void)m; seen_a++; }
static void h_b(ulog_level_t s, char *m) { (void)s; (void)m; seen_b++; }
static void h_c(ulog_level_t s, char *m) { (void)s; (void)m; seen_c++; }
static void h_d(ulog_level_t s, char *m) { (void)s; (void)m; seen_d++; }
static void h_e(ulog_level_t s, char *m) { (void)s; (void)m; seen_e++; }
static void h_f(ulog_level_t s, char *m) { (void)s; (void)m; seen_f++; }
static void h_g(ulog_level_t s, char *m) { (void)s; (void)m; seen_g++; }

static void case_engine_subscriber_cap(void)
{
	ulog_init();

	sub_a = ulog_subscribe(h_a, ULOG_TRACE_LEVEL) == ULOG_ERR_NONE;
	sub_b = ulog_subscribe(h_b, ULOG_TRACE_LEVEL) == ULOG_ERR_NONE;
	sub_c = ulog_subscribe(h_c, ULOG_TRACE_LEVEL) == ULOG_ERR_NONE;
	sub_d = ulog_subscribe(h_d, ULOG_TRACE_LEVEL) == ULOG_ERR_NONE;
	sub_e = ulog_subscribe(h_e, ULOG_TRACE_LEVEL) == ULOG_ERR_NONE;
	sub_f = ulog_subscribe(h_f, ULOG_TRACE_LEVEL) == ULOG_ERR_NONE;
	/* the seventh must be refused, not silently dropped */
	sub_g = ulog_subscribe(h_g, ULOG_TRACE_LEVEL) == ULOG_ERR_SUBSCRIBERS_EXCEEDED;

	case_result("engine-subscriber-cap",
	    sub_a && sub_b && sub_c && sub_d && sub_e && sub_f && sub_g,
	    sub_g ? "six accepted, the seventh refused" :
	    "the seventh subscriber was not refused");

	ulog_unsubscribe(h_a); ulog_unsubscribe(h_b); ulog_unsubscribe(h_c);
	ulog_unsubscribe(h_d); ulog_unsubscribe(h_e); ulog_unsubscribe(h_f);
}

static void case_engine_threshold(void)
{
	ulog_init();
	seen_a = 0;
	(void)ulog_subscribe(h_a, ULOG_WARNING_LEVEL);

	ulog_message(ULOG_DEBUG_LEVEL, "below");
	ulog_message(ULOG_INFO_LEVEL, "below");
	ulog_message(ULOG_WARNING_LEVEL, "at");
	ulog_message(ULOG_ERROR_LEVEL, "above");

	case_result("engine-threshold", seen_a == 2,
	    seen_a == 2 ? "only WARNING and above were delivered" :
	    "the threshold let the wrong count through");

	ulog_unsubscribe(h_a);
}

static void case_engine_always(void)
{
	ulog_init();
	seen_a = 0;
	(void)ulog_subscribe(h_a, ULOG_CRITICAL_LEVEL);

	ulog_message(ULOG_ALWAYS_LEVEL, "always");

	case_result("engine-always", seen_a == 1,
	    seen_a == 1 ? "ALWAYS reached a CRITICAL subscriber" :
	    "ALWAYS was filtered");

	ulog_unsubscribe(h_a);
}

static void case_engine_level_name(void)
{
	const char *n = ulog_level_name(ULOG_ERROR_LEVEL);

	case_result("engine-level-name", n != NULL && n[0] != '\0',
	    "the level vocabulary has names");
}

/* ---- the shell: never filter a judged line ----------------------------- */

static void case_wrapper_promotion(void)
{
	/* A judged line emitted at INFO with the threshold at CRITICAL: the
	 * family table has to lift it, or a silent console would be a false
	 * pass. "wlan: scan done rows=" is one of the generated families. */
	ulog_test_wire_reset();
	log_level_set(ULOG_CRITICAL_LEVEL);
	LOG_INFO("wlan: scan done rows=3 chans=1\n");

	case_result("wrapper-promotion",
	    strstr(ulog_test_wire(), "wlan: scan done rows=3") != NULL,
	    "a judged family survives the strictest threshold");

	log_level_set(ULOG_INFO_LEVEL);
}

static void case_wrapper_gate(void)
{
	/* The same strict threshold, a line no judge reads: it must be gone. */
	ulog_test_wire_reset();
	log_level_set(ULOG_CRITICAL_LEVEL);
	LOG_INFO("ulogtest: an ordinary informational line\n");

	case_result("wrapper-gate",
	    strstr(ulog_test_wire(), "ordinary informational line") == NULL,
	    "a line outside the families is filtered at the threshold");

	log_level_set(ULOG_INFO_LEVEL);
}

/* ---- the shell: one line, one stamp, one wire discipline --------------- */

static void case_outlet_line_assembly(void)
{
	ulog_test_clock_set(1000000u);
	ulog_test_wire_reset();

	/* Two fragments of one line, neither terminated. */
	LOG_ANCHOR("ulogtest: first half ");
	LOG_ANCHOR("second half\n");

	case_result("outlet-line-assembly",
	    strstr(ulog_test_wire(), "ulogtest: first half second half\n") != NULL,
	    "an unterminated fragment joins the next piece of the same line");
}

static void case_outlet_cr_lf(void)
{
	ulog_test_clock_set(1000000u);
	ulog_test_wire_reset();

	/* The writer expands '\n' to CR LF; a '\r' the caller wrote must not
	 * reach the wire, or a CR-counting parser sees a blank line. */
	LOG_ANCHOR("ulogtest: crlf\r\n");

	{
		const char *w = ulog_test_wire();

		case_result("outlet-cr-lf",
		    strstr(w, "ulogtest: crlf\n") != NULL &&
		    strstr(w, "ulogtest: crlf\r") == NULL,
		    "the caller's CR is dropped and the line is terminated once");
	}
}

static void case_outlet_repeat_fold(void)
{
	ulog_test_clock_set(1000000u);
	ulog_test_wire_reset();

	LOG_ANCHOR("ulogtest: repeated line\n");
	LOG_ANCHOR("ulogtest: repeated line\n");
	LOG_ANCHOR("ulogtest: repeated line\n");
	LOG_ANCHOR("ulogtest: a different line\n");

	{
		const char *w = ulog_test_wire();
		const char *first = strstr(w, "ulogtest: repeated line");
		const char *tail = strstr(w, "[previous line repeated 2 times]");

		/* The first copy survives (a judge may anchor it), the run's
		 * length is reported, and the run is not repeated verbatim. */
		case_result("outlet-repeat-fold",
		    first != NULL && tail != NULL &&
		    strstr(first + 1, "ulogtest: repeated line") == NULL,
		    "three copies fold to one plus the run length");
	}
}

static void case_outlet_emergency(void)
{
	ulog_test_wire_reset();

	/* The fault outlet takes no lock and carries no stamp: a stack-overflow
	 * victim must not push a message through the ordinary path. */
	board_log_emergency("ulogtest: emergency %u", 7u);

	{
		const char *w = ulog_test_wire();

		case_result("outlet-emergency",
		    strstr(w, "!!! ulogtest: emergency 7\n") != NULL &&
		    strstr(w, "[") == NULL,
		    "the emergency outlet writes unstamped and unterminated by the lock");
	}
}

/* ---- the never-filtered table itself ----------------------------------- */

static void case_anchor_table(void)
{
	unsigned int i;
	int ok = log_anchor_family_count > 0u;

	for (i = 0; ok && i < log_anchor_family_count; i++) {
		if (log_anchor_families[i] == NULL ||
		    log_anchor_families[i][0] == '\0') {
			ok = 0;
		}
	}

	printf("ULOGLEVEL: anchor-table %s %u families\n",
	    ok ? "PASS" : "FAIL", log_anchor_family_count);
	cases++;
	if (!ok) {
		failed++;
	}
}

int main(void)
{
	case_engine_subscriber_cap();
	case_engine_threshold();
	case_engine_always();
	case_engine_level_name();
	case_wrapper_promotion();
	case_wrapper_gate();
	case_outlet_line_assembly();
	case_outlet_cr_lf();
	case_outlet_repeat_fold();
	case_outlet_emergency();
	case_anchor_table();

	printf("ULOGLEVEL: summary cases=%d failed=%d\n", cases, failed);
	return failed == 0 ? 0 : 1;
}
