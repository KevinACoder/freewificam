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
#define BA_SLOTS	32
#define BA_FRAME_MAX	1600
#define BA_FLUSH_MS	50

#define BA_WIN		32	/* how far ahead of `head` a frame may sit */

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
	unsigned int		hole;
	unsigned int		dropped;
	unsigned int		frames;
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

	for (t = 0; t < BA_TIDS; t++) {
		if (!ba.tids[t].active)
			continue;
		for (;;) {
			struct ba_slot *best = NULL;
			uint16_t bestd = 0;
			int i;

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
			}
			n += ba_drain_locked((uint8_t)t);
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
	if (sn == ba.tids[tid].head) {
		ba_deliver_locked(frame, len, m);
		ba.tids[tid].head = (uint16_t)((ba.tids[tid].head + 1) & 0x0fff);
		(void)ba_drain_locked(tid);
	} else if (ba_after(sn, ba.tids[tid].head) &&
	    ba_dist(sn, ba.tids[tid].head) <= BA_WIN) {
		s = ba_slot_find(tid, sn);
		if (s != NULL) {
			ba.dup++;		/* the same SN already held */
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
			} else {
				s->used = 1;
				s->tid = tid;
				s->sn = sn;
				s->len = (uint16_t)len;
				s->meta = *m;
				memcpy(s->data, frame, len);
				ba.held++;
				(void)schedule_delayed_work(&ba.flush_work,
				    msecs_to_jiffies(BA_FLUSH_MS));
			}
		}
	} else {
		ba.dup++;	/* behind the head: a retry we already saw */
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

void
wlan_rx_ba_dump(void)
{

	wlan_osal_log("wlan: ba addba_req=%u addba_rsp=%u delba=%u held=%u "
	    "flushed=%u dup=%u hole=%u dropped=%u frames=%u\n",
	    ba.addba_req, ba.addba_rsp, ba.delba, ba.held, ba.flushed,
	    ba.dup, ba.hole, ba.dropped, ba.frames);
}

#endif /* CONFIG_WLAN_RX_BA */
