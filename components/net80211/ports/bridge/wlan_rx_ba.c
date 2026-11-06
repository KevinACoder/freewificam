/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   wlan_rx_ba.c
 * @brief  Downlink RX block-ack for the RTOS arm: ADDBA answer + reorder.
 *
 * The full rationale is in wlan_rx_ba.h.  The mechanics, and the order they
 * matter in:
 *
 *   - the AP's ADDBA Request (action category 3, action 0) is answered with a
 *     Response that echoes its dialog token and parameter set, through the
 *     bridge's management window (wlan_bridge_mgmt_tx), which is the same
 *     transport the stack's own association frames use;
 *   - the request's starting sequence control seeds the TID's expected
 *     sequence number (`head`);
 *   - a QoS data frame whose SN is `head` is delivered and the contiguous run
 *     already held behind it follows; one that is ahead is copied into a
 *     slot; one that is behind is a duplicate and dropped;
 *   - a 50 ms delayed work flushes the held run when the missing frame never
 *     comes, counting the hole as lost.  Holding forever would turn one lost
 *     MPDU into a dead receive path;
 *   - all deliveries (immediate, reordered, flushed) run under one mutex.  The
 *     receive worker and the flush work are different threads, and the
 *     imported net80211 input is not re-entrant: without this lock a flush can
 *     run inside a beacon's delivery.
 *
 * The module copies the frames it holds.  See the header for why holding the
 * skbs would starve the driver's receive pool.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "../osal/wlan_osal.h"
#include "wlan_bridge.h"

#include "wlan_rx_ba.h"

#if defined(CONFIG_WLAN_RX_BA) && CONFIG_WLAN_RX_BA

#define BA_TIDS		16
#define BA_SLOTS	128
#define BA_FRAME_MAX	1600
#define BA_FLUSH_MS	50

#define BA_WIN		128	/* how far ahead of `head` a frame may sit */

struct ba_slot {
	int	used;
	uint8_t	tid;
	uint16_t	sn;
	uint16_t	len;
	struct wlan_rx_ba_meta meta;
	uint8_t	data[BA_FRAME_MAX];
};

struct ba_tid {
	int	active;
	uint16_t head;		/* the next SN the stack must see */
	uint16_t newest;	/* the highest SN the window has seen */
	int	newest_valid;
	unsigned int lag;	/* how far `head` trails `newest`, in SNs */
};

static struct {
	struct mutex		lock;
	int			attached;
	wlan_rx_ba_deliver_fn	fn;
	void			*arg;
	struct ba_tid		tids[BA_TIDS];
	struct ba_slot		slots[BA_SLOTS];
	struct delayed_work	flush_work;
	/* counters, cumulative since attach */
	unsigned int		addba_req;
	unsigned int		addba_rsp;
	unsigned int		delba;
	unsigned int		held;
	unsigned int		flushed;
	unsigned int		dup;
	unsigned int		dup_behind;	/* SN the head already passed */
	unsigned int		dup_far;	/* ahead, but outside the window */
	unsigned int		dup_slot;	/* the same SN already held */
	unsigned int		drop_window;	/* window full even after a flush */
	unsigned int		drop_stale;	/* slot the head had already passed */
	unsigned int		behind_hist[6];	/* how far behind, in SNs */
	unsigned int		hole;
	unsigned int		dropped;
	unsigned int		frames;
	/* Window recovery: `resync` counts the times the head had to jump to
	 * the newest frame's window instead of the oldest held one (a gap the
	 * AP is not going to fill), `arm_fail` the times the flush timer could
	 * not be armed at all, `armed` the timer's pending state - the timer
	 * is armed on the first held frame and cleared when it fires, never
	 * re-armed per arrival. */
	unsigned int		resync;
	unsigned int		arm_fail;
	int			armed;
} ba;

/* 12-bit serial number comparison (IEEE 802.11's modulo-4096 numbering). */
static int
ba_after(uint16_t a, uint16_t b)
{
	uint16_t d = (uint16_t)((a - b) & 0x0fff);

	return d != 0 && d < 0x800;
}

static uint16_t
ba_dist(uint16_t a, uint16_t b)
{
	return (uint16_t)((a - b) & 0x0fff);
}

/*
 * How far the window has fallen behind, with no limb at half the sequence
 * space.
 *
 * ba_after() answers "is a ahead of b", and by construction it can only be
 * trusted while the gap is under 2048: past that the ordering wraps and it
 * reads false for a head that is hopelessly behind.  That is not a corner
 * case here - it is the state the window lands in, and it is the state a
 * recovery test must be able to name (W7-10: dupfar grew to 11827 for one
 * boot and 12539 for another while the recovery counter never moved again).
 *
 * `lag` is the distance the head is behind the newest SN the TID has seen,
 * tracked as an unsigned count instead of being recomputed from two 12-bit
 * numbers: a frame that advances the newest SN while the head stands still
 * *creates* lag, and only a head advance consumes it.  Nothing wraps until
 * 4096 frames have been swallowed in one go, which a live link cannot do
 * (the flush timer bounds the burst at BA_FLUSH_MS of traffic).
 */
static unsigned int
ba_lag(const struct ba_tid *t)
{

	return t->lag;
}

static void
ba_lag_note_newest(struct ba_tid *t, uint16_t sn)
{
	unsigned int step;

	if (!t->newest_valid) {
		t->newest = sn;
		t->newest_valid = 1;
		t->lag = 0;
		return;
	}
	step = ba_dist(sn, t->newest);
	/* a repeat or an out-of-order copy does not move the newest SN, and a
	 * wrap of the 12-bit space is a normal (small) step */
	if (step == 0 || step >= 0x800)
		return;
	t->newest = sn;
	t->lag += step;
}

/* The head moved forward by `n` sequence numbers; consume the lag with it. */
static void
ba_lag_consume(struct ba_tid *t, unsigned int n)
{

	t->lag = t->lag > n ? t->lag - n : 0;
}

static void
ba_deliver_locked(const uint8_t *frame, size_t len,
    const struct wlan_rx_ba_meta *m)
{

	if (ba.fn != NULL)
		ba.fn(ba.arg, frame, len, m);
	ba.frames++;
}

static struct ba_slot *
ba_slot_find(uint8_t tid, uint16_t sn)
{
	int i;

	for (i = 0; i < BA_SLOTS; i++) {
		if (ba.slots[i].used && ba.slots[i].tid == tid &&
		    ba.slots[i].sn == sn)
			return &ba.slots[i];
	}
	return NULL;
}

static struct ba_slot *
ba_slot_free(void)
{
	int i;

	for (i = 0; i < BA_SLOTS; i++) {
		if (!ba.slots[i].used)
			return &ba.slots[i];
	}
	return NULL;
}

/* Deliver the slot and release it. */
static void
ba_slot_deliver(struct ba_slot *s)
{

	ba_deliver_locked(s->data, s->len, &s->meta);
	s->used = 0;
}

/*
 * Deliver whatever contiguous run starts at the TID's head.  Returns the
 * number of frames delivered.
 */
static int
ba_drain_locked(uint8_t tid)
{
	int n = 0;

	for (;;) {
		struct ba_slot *s = ba_slot_find(tid, ba.tids[tid].head);

		if (s == NULL)
			break;
		ba_slot_deliver(s);
		ba.tids[tid].head = (uint16_t)((ba.tids[tid].head + 1) & 0x0fff);
		ba_lag_consume(&ba.tids[tid], 1u);
		n++;
	}
	return n;
}

/*
 * The timeout (or a full window) path: jump the head to the oldest frame the
 * window still holds, count the frames that never arrived as lost, then drain.
 */
static int
ba_flush_locked(void)
{
	int t, n = 0;
	int i;

	for (t = 0; t < BA_TIDS; t++) {
		if (!ba.tids[t].active)
			continue;
		/*
		 * A gap the AP is not going to fill must not hold the window for
		 * ever.  The test is on `lag`, never on ba_after(): the whole
		 * point is to fire when the window is hopelessly behind, and that
		 * is exactly the state in which the modular comparison has
		 * already wrapped and reads "not ahead" - a recovery test that
		 * ba_after() can silently disable makes the state permanent,
		 * which is what W7-10 measured (two boots, dupfar 11827 and
		 * 12539, recovery counter frozen at 2 and 11).
		 *
		 * Where the head lands depends on what is still worth keeping:
		 *   - the oldest frame the window holds is real data, so jump
		 *     there and let the drain below deliver it;
		 *   - with nothing held, the window cannot reconstruct the run at
		 *     all, so resynchronise to the live sequence (newest + 1) and
		 *     count everything skipped as lost.  Jumping only
		 *     `newest - BA_WIN + 1` in that case leaves every arriving
		 *     frame outside the window again - the window would resync on
		 *     each frame and never deliver one.
		 */
		if (ba.tids[t].newest_valid && ba_lag(&ba.tids[t]) > BA_WIN) {
			struct ba_slot *oldest = NULL;
			uint16_t oldest_d = 0;
			unsigned int lag = ba_lag(&ba.tids[t]);
			int i;

			for (i = 0; i < BA_SLOTS; i++) {
				struct ba_slot *s = &ba.slots[i];
				uint16_t d;

				if (!s->used || s->tid != (uint8_t)t)
					continue;
				d = ba_dist(s->sn, ba.tids[t].head);
				if (d == 0 || d >= 0x800)
					continue;	/* behind the head */
				if (oldest == NULL || d < oldest_d) {
					oldest = s;
					oldest_d = d;
				}
			}
			if (oldest != NULL) {
				ba.hole += oldest_d;
				ba.tids[t].head = oldest->sn;
				ba_lag_consume(&ba.tids[t],
					       oldest_d < lag ? oldest_d : lag);
			} else {
				/* Nothing held: the window cannot reconstruct the
				 * run, so it keeps the widest admission it can and
				 * lets the head sit one window behind the live
				 * sequence.  Jumping to newest + 1 instead looks
				 * tidier and is wrong: a frame that is still in
				 * the host's receive path has not been lost, and
				 * declaring it lost turns one late arrival into a
				 * window's worth of holes (W7-10 fix arm: jumping
				 * to newest + 1 produced 8252 `dupbehind` and a
				 * 33 % client-side loss).  The skipped SNs are the
				 * ones that really never came. */
				unsigned int keep = BA_WIN - 1u;

				if (lag > keep)
					ba.hole += lag - keep;
				ba.tids[t].head = (uint16_t)
				    ((ba.tids[t].newest - keep) & 0x0fff);
				ba.tids[t].lag = keep;
			}
			ba.resync++;
		}
		for (;;) {
			struct ba_slot *best = NULL;
			uint16_t bestd = 0;

			for (i = 0; i < BA_SLOTS; i++) {
				struct ba_slot *s = &ba.slots[i];
				uint16_t d;

				if (!s->used || s->tid != (uint8_t)t)
					continue;
				if (!ba_after(s->sn, ba.tids[t].head))
					continue;	/* behind the head */
				d = ba_dist(s->sn, ba.tids[t].head);
				if (d > BA_WIN)
					continue;
				if (best == NULL || d < bestd) {
					best = s;
					bestd = d;
				}
			}
			if (best == NULL)
				break;
			if (bestd != 0) {
				ba.hole += bestd;
				ba.tids[t].head = best->sn;
				/* the head moved, so the lag it had does too;
				 * without this the next flush would see the old
				 * lag and count the same SNs as holes twice */
				ba_lag_consume(&ba.tids[t], bestd);
			}
			n += ba_drain_locked((uint8_t)t);
		}
		/*
		 * A slot the head has passed can never be delivered: the drain
		 * only walks forward from the head, and a frame that fell behind
		 * it is stale by definition.  Left alone it would hold its slot
		 * for the life of the session (there is no other path that frees
		 * slots), and a window that leaks slots eventually cannot admit
		 * anything.  Dropping it here is the only place that can.
		 */
		for (i = 0; i < BA_SLOTS; i++) {
			struct ba_slot *s = &ba.slots[i];

			if (!s->used || s->tid != (uint8_t)t)
				continue;
			if (s->sn == ba.tids[t].head)
				continue;
			if (ba_after(s->sn, ba.tids[t].head))
				continue;
			s->used = 0;
			ba.dropped++;
			ba.drop_stale++;
		}
	}
	ba.flushed += (unsigned int)n;
	return n;
}

static void
ba_flush_work(struct work_struct *w)
{

	(void)w;
	mutex_lock(&ba.lock);
	ba.armed = 0;
	(void)ba_flush_locked();
	mutex_unlock(&ba.lock);
}

static void
ba_answer_addba(const uint8_t *req, size_t len)
{
	uint8_t rsp[33];
	uint16_t params;

	if (len < 33)
		return;
	memset(rsp, 0, sizeof(rsp));
	rsp[0] = 0xd0;			/* action */
	rsp[1] = 0;
	/* receiver = the request's transmitter (the AP), transmitter = us */
	memcpy(rsp + 4, req + 10, 6);
	memcpy(rsp + 10, req + 4, 6);
	memcpy(rsp + 16, req + 16, 6);
	rsp[24] = 3;			/* Block Ack category */
	rsp[25] = 1;			/* ADDBA Response */
	rsp[26] = req[26];		/* dialog token */
	rsp[27] = 0;			/* status: success */
	rsp[28] = 0;
	rsp[29] = req[27];		/* parameter set, echoed */
	rsp[30] = req[28];
	rsp[31] = 0;			/* timeout */
	rsp[32] = 0;
	params = (uint16_t)(req[27] | (req[28] << 8));
	wlan_osal_log("wlan: ba addba rsp tid=%u buf=%u dialog=%u\n",
	    (unsigned)((params >> 2) & 0x0f), (unsigned)((params >> 6) & 0x03ff),
	    (unsigned)req[26]);
	if (wlan_bridge_mgmt_tx(rsp, sizeof(rsp)) < 0) {
		wlan_osal_log("wlan: ba addba rsp tx failed\n");
		return;
	}
	ba.addba_rsp++;
	wlan_bridge_ba_note(WLAN_BRIDGE_BA_RX_START);
}

/*
 * Management action frames.  Returns 1 when the frame was consumed (it is the
 * block-ack family), 0 when it is some other action frame the stack may see.
 */
static int
ba_action(const struct wlan_rx_ba_meta *m, const uint8_t *frame, size_t len)
{
	uint16_t params, tid;

	(void)m;
	if (len < 26 || frame[24] != 3)		/* Block Ack category */
		return 0;
	switch (frame[25]) {
	case 0:					/* ADDBA Request */
		if (len < 33)
			return 1;
		params = (uint16_t)(frame[27] | (frame[28] << 8));
		tid = (uint16_t)((params >> 2) & 0x0f);
		if (tid >= BA_TIDS)
			return 1;
		mutex_lock(&ba.lock);
		ba.addba_req++;
		/* A fresh request seeds the window: any frame held for the old
		 * one is stale, so it is dropped rather than delivered under a
		 * sequence number that will not follow. */
		{
			int i;

			for (i = 0; i < BA_SLOTS; i++) {
				if (ba.slots[i].used &&
				    ba.slots[i].tid == (uint8_t)tid) {
					ba.slots[i].used = 0;
					ba.dropped++;
				}
			}
		}
		ba.tids[tid].active = 1;
		ba.tids[tid].head = (uint16_t)(((uint16_t)(frame[31] |
		    (frame[32] << 8))) >> 4);
		/* The new session's counter is the only SN this TID can trust:
		 * the window restarts from the request, so the previous session's
		 * newest SN is stale. */
		ba.tids[tid].newest = ba.tids[tid].head;
		ba.tids[tid].newest_valid = 0;
		mutex_unlock(&ba.lock);
		wlan_osal_log("wlan: ba addba req tid=%u ssn=%u buf=%u\n",
		    (unsigned)tid, (unsigned)ba.tids[tid].head,
		    (unsigned)((params >> 6) & 0x03ff));
		/* The response takes the port serializer and sleeps: outside
		 * the lock, like every other bridge call that may block. */
		ba_answer_addba(frame, len);
		return 1;
	case 1:					/* ADDBA Response (our TX side) */
		return 1;
	case 2:					/* DELBA */
		if (len < 28)
			return 1;
		tid = (uint16_t)((frame[26] | (frame[27] << 8)) & 0x0f);
		if (tid >= BA_TIDS)
			return 1;
		mutex_lock(&ba.lock);
		ba.delba++;
		ba.tids[tid].active = 0;
		(void)ba_flush_locked();
		mutex_unlock(&ba.lock);
		wlan_bridge_ba_note(WLAN_BRIDGE_BA_STOP);
		return 1;
	default:
		return 1;			/* other BA action: no consumer */
	}
}

static int
ba_qos_data(const struct wlan_rx_ba_meta *m, const uint8_t *frame, size_t len)
{
	uint8_t tid;
	uint16_t sn;
	struct ba_slot *s;

	if (len < 26)
		return 0;
	tid = (uint8_t)(frame[24] & 0x0f);
	if (tid >= BA_TIDS || !ba.tids[tid].active)
		return 0;
	if (len > BA_FRAME_MAX) {
		/* Cannot hold a frame larger than the window: deliver it in
		 * arrival order and let the sequence check cope. */
		mutex_lock(&ba.lock);
		ba_deliver_locked(frame, len, m);
		mutex_unlock(&ba.lock);
		return 1;
	}
	sn = (uint16_t)(((uint16_t)(frame[22] | (frame[23] << 8))) >> 4);

	mutex_lock(&ba.lock);
	ba_lag_note_newest(&ba.tids[tid], sn);
	if (sn == ba.tids[tid].head) {
		ba_deliver_locked(frame, len, m);
		ba.tids[tid].head = (uint16_t)((ba.tids[tid].head + 1) & 0x0fff);
		ba_lag_consume(&ba.tids[tid], 1u);
		(void)ba_drain_locked(tid);
	} else if (ba_after(sn, ba.tids[tid].head) &&
	    ba_dist(sn, ba.tids[tid].head) <= BA_WIN) {
		s = ba_slot_find(tid, sn);
		if (s != NULL) {
			ba.dup++;		/* the same SN already held */
			ba.dup_slot++;
		} else {
			s = ba_slot_free();
			if (s == NULL) {
				/* Window full: make room by flushing the
				 * held run (the oldest hole is counted lost). */
				(void)ba_flush_locked();
				s = ba_slot_free();
			}
			if (s == NULL) {
				ba.dropped++;
				ba.drop_window++;
			} else {
				s->used = 1;
				s->tid = tid;
				s->sn = sn;
				s->len = (uint16_t)len;
				s->meta = *m;
				memcpy(s->data, frame, len);
				ba.held++;
				/* Arm on the first held frame, not on every arrival:
				 * schedule_delayed_work() re-arms the timer, so a
				 * continuous hole stream pushed the flush out for ever
				 * and the window only drained when its slots filled. */
				if (!ba.armed) {
					ba.armed = 1;
					if (!schedule_delayed_work(&ba.flush_work,
					    msecs_to_jiffies(BA_FLUSH_MS))) {
						ba.arm_fail++;
						ba.armed = 0;
						(void)ba_flush_locked();
					}
				}
			}
		}
	} else if (!ba_after(sn, ba.tids[tid].head)) {
		/* Behind the head: a retry we already passed.  How far behind
		 * says whether it is a copy of a frame just delivered or a
		 * frame from an older wrap of the 12-bit SN space. */
		unsigned int d = ba_dist(ba.tids[tid].head, sn);

		ba.dup++;
		ba.dup_behind++;
		if (d <= 2)
			ba.behind_hist[0]++;
		else if (d <= 4)
			ba.behind_hist[1]++;
		else if (d <= 8)
			ba.behind_hist[2]++;
		else if (d <= 32)
			ba.behind_hist[3]++;
		else if (d <= 512)
			ba.behind_hist[4]++;
		else
			ba.behind_hist[5]++;
	} else if (ba_lag(&ba.tids[tid]) > BA_WIN) {
		/*
		 * Not in the window, and the window is lost: the head trails the
		 * live sequence by more than a window, so this frame cannot be
		 * admitted however it is classified.  Recover here rather than
		 * waiting for the flush timer, which has no reason to be armed -
		 * it is armed only when a frame enters a slot, and once the
		 * window is lost no frame does (W7-10's permanent state).
		 *
		 * Losing the race to the 12-bit compare is normal (this branch
		 * fires whenever a burst outruns the window), so this is not an
		 * error: the frame is counted, the window resynchronises, and the
		 * next one lands.
		 */
		ba.dup++;
		ba.dup_far++;
		(void)ba_flush_locked();
	} else {
		/* Ahead of the head, past the window, and the window is sane:
		 * the head is about to advance and this SN is too old a burst
		 * mate to hold. */
		ba.dup++;
		ba.dup_far++;
	}
	mutex_unlock(&ba.lock);
	return 1;
}

int
wlan_rx_ba_attach(wlan_rx_ba_deliver_fn fn, void *arg)
{

	if (ba.attached)
		return 0;
	memset(&ba, 0, sizeof(ba));
	mutex_init(&ba.lock);
	INIT_DELAYED_WORK(&ba.flush_work, ba_flush_work);
	ba.fn = fn;
	ba.arg = arg;
	ba.attached = 1;
	wlan_osal_log("wlan: ba attached\n");
	return 0;
}

void
wlan_rx_ba_detach(void)
{

	if (!ba.attached)
		return;
	ba.attached = 0;
	cancel_delayed_work_sync(&ba.flush_work);
	mutex_lock(&ba.lock);
	memset(ba.slots, 0, sizeof(ba.slots));
	memset(ba.tids, 0, sizeof(ba.tids));
	ba.fn = NULL;
	ba.arg = NULL;
	mutex_unlock(&ba.lock);
	mutex_destroy(&ba.lock);
}

int
wlan_rx_ba_rx(const struct wlan_rx_ba_meta *m, const uint8_t *frame,
    size_t len)
{
	uint8_t type, subtype;

	if (!ba.attached || frame == NULL || len < 24)
		return 0;
	type = (uint8_t)((frame[0] >> 2) & 0x03);
	subtype = (uint8_t)((frame[0] >> 4) & 0x0f);
	/* Action frames first: they are rare and must not be delivered. */
	if (type == 0 && subtype == 0x0d)
		return ba_action(m, frame, len);
	if (type == 2 && (subtype & 0x08) != 0)
		return ba_qos_data(m, frame, len);
	/* Everything else (beacons, probe responses, non-QoS data) is
	 * delivered here too, under the same lock, so a flush can never run
	 * inside another frame's delivery. */
	mutex_lock(&ba.lock);
	ba_deliver_locked(frame, len, m);
	mutex_unlock(&ba.lock);
	return 1;
}

static unsigned int
ba_used_slots(void)
{
	int i, n = 0;

	for (i = 0; i < BA_SLOTS; i++) {
		if (ba.slots[i].used)
			n++;
	}
	return (unsigned int)n;
}

void
wlan_rx_ba_dump(void)
{

	wlan_osal_log("wlan: ba addba_req=%u addba_rsp=%u delba=%u held=%u "
	    "flushed=%u dup=%u hole=%u dropped=%u (win=%u stale=%u) "
	    "frames=%u\n",
	    ba.addba_req, ba.addba_rsp, ba.delba, ba.held, ba.flushed,
	    ba.dup, ba.hole, ba.dropped, ba.drop_window, ba.drop_stale,
	    ba.frames);
	/* The reorder verdict, split by reason: a duplicate copy the head has
	 * already passed (behind), a copy of a frame still held (slot), a frame
	 * ahead of the window (far), or a window that stayed full after a
	 * flush.  `behind` buckets the distance in SNs: <=2, <=4, <=8, <=32,
	 * <=512, larger.  `head` is what the next expected SN was. */
	wlan_osal_log("wlan: ba reorder head=%u newest=%u heldnow=%u deliv=%u "
	    "dupslot=%u dupbehind=%u dupfar=%u dropwin=%u resync=%u "
	    "armfail=%u behind=%u/%u/%u/%u/%u/%u\n",
	    ba.tids[0].head, ba.tids[0].newest, ba_used_slots(), ba.frames,
	    ba.dup_slot, ba.dup_behind, ba.dup_far, ba.drop_window, ba.resync,
	    ba.arm_fail, ba.behind_hist[0], ba.behind_hist[1],
	    ba.behind_hist[2], ba.behind_hist[3], ba.behind_hist[4],
	    ba.behind_hist[5]);
}

#endif /* CONFIG_WLAN_RX_BA */
