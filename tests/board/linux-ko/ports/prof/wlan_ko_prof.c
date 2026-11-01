/*
 * @file
 * @brief Per-frame instrumentation for the kernelmod line (see the header).
 *
 * Armed at runtime with prof_enable=1; prof_reset drops the counters and
 * prof_dump prints one line per stage/queue/event.  The link fingerprint
 * (HT/MCS/WMM/A-MPDU) is read on the AP side: the ko line's WEXT face has no
 * rate, and the generic bridge link dump is not compiled into this line.
 */
#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/types.h>

#include "wlan_ko_prof.h"

#ifdef CONFIG_WLAN_KO_PROF

static bool prof_enable;

static atomic64_t prof_count[WLAN_KO_PROF_STAGE_NR];
static atomic64_t prof_sum_ns[WLAN_KO_PROF_STAGE_NR];
static atomic64_t prof_max_ns[WLAN_KO_PROF_STAGE_NR];
static atomic_t prof_hist[WLAN_KO_PROF_STAGE_NR][32];

static atomic_t prof_q_cur[WLAN_KO_PROF_Q_NR];
static atomic_t prof_q_max[WLAN_KO_PROF_Q_NR];

static atomic64_t prof_evt[WLAN_KO_PROF_EVT_NR];

static u64 prof_start_ns;

static const char *const prof_stage_name[WLAN_KO_PROF_STAGE_NR] = {
	[WLAN_KO_PROF_TX_XMIT] = "tx_xmit",
	[WLAN_KO_PROF_TX_WORK] = "tx_work",
	[WLAN_KO_PROF_TX_DEFER] = "tx_defer",
	[WLAN_KO_PROF_TX_SEAM] = "tx_seam",
	[WLAN_KO_PROF_TX_STATUS] = "tx_status",
	[WLAN_KO_PROF_TX_STATUS_WORK] = "tx_status_w",
	[WLAN_KO_PROF_RX_IRQ] = "rx_irq",
	[WLAN_KO_PROF_RX_WORK] = "rx_work",
	[WLAN_KO_PROF_RX_NETDEV] = "rx_netdev",
	[WLAN_KO_PROF_RX_DEFER] = "rx_defer",
};

static const char *const prof_q_name[WLAN_KO_PROF_Q_NR] = {
	[WLAN_KO_PROF_Q_NETDEV_TX] = "netdev_tx",
	[WLAN_KO_PROF_Q_RX] = "rx",
	[WLAN_KO_PROF_Q_TX_STATUS] = "tx_status",
};

static const char *const prof_evt_name[WLAN_KO_PROF_EVT_NR] = {
	[WLAN_KO_PROF_EVT_TX_ENQ] = "tx_enq",
	[WLAN_KO_PROF_EVT_TX_WORK] = "tx_work",
	[WLAN_KO_PROF_EVT_TX_SEAM] = "tx_seam",
	[WLAN_KO_PROF_EVT_TX_STATUS] = "tx_status",
	[WLAN_KO_PROF_EVT_TX_BUSY] = "tx_busy",
	[WLAN_KO_PROF_EVT_TX_LEN_DROP] = "tx_len_drop",
	[WLAN_KO_PROF_EVT_TX_BRIDGE_FULL] = "tx_bridge_full",
	[WLAN_KO_PROF_EVT_RX_FRAME] = "rx_frame",
	[WLAN_KO_PROF_EVT_RX_WORK] = "rx_work",
	[WLAN_KO_PROF_EVT_RX_DROP] = "rx_drop",
};

bool wlan_ko_prof_enabled(void)
{
	return READ_ONCE(prof_enable);
}

u64 wlan_ko_prof_begin(void)
{
	if (!READ_ONCE(prof_enable))
		return 0;
	return ktime_get_ns();
}

static void prof_reset_all(void)
{
	int i;

	for (i = 0; i < WLAN_KO_PROF_STAGE_NR; i++) {
		atomic64_set(&prof_count[i], 0);
		atomic64_set(&prof_sum_ns[i], 0);
		atomic64_set(&prof_max_ns[i], 0);
		memset(prof_hist[i], 0, sizeof(prof_hist[i]));
	}
	for (i = 0; i < WLAN_KO_PROF_Q_NR; i++) {
		atomic_set(&prof_q_cur[i], 0);
		atomic_set(&prof_q_max[i], 0);
	}
	for (i = 0; i < WLAN_KO_PROF_EVT_NR; i++)
		atomic64_set(&prof_evt[i], 0);
	prof_start_ns = ktime_get_ns();
}

void wlan_ko_prof_value(int stage, u64 ns)
{
	u64 old;
	int bucket, i;
	u64 v;

	if (!READ_ONCE(prof_enable) || stage < 0 ||
	    stage >= WLAN_KO_PROF_STAGE_NR)
		return;
	atomic64_inc(&prof_count[stage]);
	atomic64_add(ns, &prof_sum_ns[stage]);
	old = atomic64_read(&prof_max_ns[stage]);
	while (ns > old &&
	       atomic64_cmpxchg(&prof_max_ns[stage], old, ns) != old)
		old = atomic64_read(&prof_max_ns[stage]);
	for (i = 0, v = ns; v > 1 && i < 31; i++)
		v >>= 1;
	bucket = i;
	atomic_inc(&prof_hist[stage][bucket]);
}

void wlan_ko_prof_end(int stage, u64 t0)
{
	u64 ns, old;
	int bucket, i;
	u64 v;

	if (t0 == 0 || stage < 0 || stage >= WLAN_KO_PROF_STAGE_NR)
		return;
	ns = ktime_get_ns() - t0;

	atomic64_inc(&prof_count[stage]);
	atomic64_add(ns, &prof_sum_ns[stage]);

	old = atomic64_read(&prof_max_ns[stage]);
	while (ns > old &&
	       atomic64_cmpxchg(&prof_max_ns[stage], old, ns) != old)
		old = atomic64_read(&prof_max_ns[stage]);

	bucket = 0;
	for (i = 0, v = ns; v > 1 && i < 31; i++)
		v >>= 1;
	bucket = i;
	atomic_inc(&prof_hist[stage][bucket]);
}

void wlan_ko_prof_depth(int q, int delta)
{
	int cur, old;

	if (!READ_ONCE(prof_enable) || q < 0 || q >= WLAN_KO_PROF_Q_NR)
		return;
	cur = atomic_add_return(delta, &prof_q_cur[q]);
	if (delta <= 0)
		return;
	old = atomic_read(&prof_q_max[q]);
	while (cur > old && atomic_cmpxchg(&prof_q_max[q], old, cur) != old)
		old = atomic_read(&prof_q_max[q]);
}

void wlan_ko_prof_event(int evt)
{
	if (!READ_ONCE(prof_enable) || evt < 0 || evt >= WLAN_KO_PROF_EVT_NR)
		return;
	atomic64_inc(&prof_evt[evt]);
}

static int prof_enable_set(const char *val, const struct kernel_param *kp)
{
	bool on;
	int rc;

	rc = kstrtobool(val, &on);
	if (rc)
		return rc;
	if (on && !READ_ONCE(prof_enable))
		prof_reset_all();
	WRITE_ONCE(prof_enable, on);
	return 0;
}

static const struct kernel_param_ops prof_enable_ops = {
	.set = prof_enable_set,
	.get = param_get_bool,
};

module_param_cb(prof_enable, &prof_enable_ops, &prof_enable, 0644);
MODULE_PARM_DESC(prof_enable, "1 = arm the per-frame profiler (resets counters)");

static int prof_reset_set(const char *val, const struct kernel_param *kp)
{
	(void)kp;
	prof_reset_all();
	return 0;
}

static const struct kernel_param_ops prof_reset_ops = {
	.set = prof_reset_set,
};

module_param_cb(prof_reset, &prof_reset_ops, NULL, 0200);
MODULE_PARM_DESC(prof_reset, "write 1 to drop the profiler counters");

static u64 prof_pct_ns(int stage, unsigned int pct)
{
	u64 total = (u64)atomic64_read(&prof_count[stage]);
	u64 want, sum = 0;
	int i;

	if (total == 0)
		return 0;
	want = (total * pct + 99) / 100;
	for (i = 0; i < 32; i++) {
		sum += (u64)atomic_read(&prof_hist[stage][i]);
		if (sum >= want)
			return (u64)1 << i;
	}
	return (u64)1 << 31;
}

static int prof_dump_set(const char *val, const struct kernel_param *kp)
{
	u64 now = ktime_get_ns();
	int i;

	(void)kp;
	pr_info("wlan_ko_prof: enabled=%d wall_ms=%llu\n",
	    READ_ONCE(prof_enable) ? 1 : 0,
	    prof_start_ns ? (now - prof_start_ns) / 1000000ULL : 0);
	for (i = 0; i < WLAN_KO_PROF_STAGE_NR; i++) {
		u64 n = (u64)atomic64_read(&prof_count[i]);
		u64 sum = (u64)atomic64_read(&prof_sum_ns[i]);
		u64 mean = n ? sum / n : 0;

		pr_info("wlan_ko_prof: stage=%s n=%llu sum_us=%llu mean_us=%llu p50_us=%llu p99_us=%llu max_us=%llu\n",
		    prof_stage_name[i], n,
		    (sum + 500) / 1000,
		    (mean + 500) / 1000,
		    (prof_pct_ns(i, 50) + 500) / 1000,
		    (prof_pct_ns(i, 99) + 500) / 1000,
		    ((u64)atomic64_read(&prof_max_ns[i]) + 500) / 1000);
	}
	for (i = 0; i < WLAN_KO_PROF_Q_NR; i++)
		pr_info("wlan_ko_prof: q=%s cur=%d max=%d\n",
		    prof_q_name[i], atomic_read(&prof_q_cur[i]),
		    atomic_read(&prof_q_max[i]));
	for (i = 0; i < WLAN_KO_PROF_EVT_NR; i++)
		pr_info("wlan_ko_prof: evt=%s n=%llu\n", prof_evt_name[i],
		    (u64)atomic64_read(&prof_evt[i]));
	return 0;
}

static const struct kernel_param_ops prof_dump_ops = {
	.set = prof_dump_set,
};

module_param_cb(prof_dump, &prof_dump_ops, NULL, 0200);
MODULE_PARM_DESC(prof_dump, "write 1 to print the counters");

#endif /* CONFIG_WLAN_KO_PROF */
