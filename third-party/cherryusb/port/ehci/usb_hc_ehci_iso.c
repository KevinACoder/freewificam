/*
 * @file   usb_hc_ehci_iso.c
 * @brief  High-speed isochronous transfer engine for the EHCI host port: the
 *         definitions of ehci_iso_urb_init / ehci_kill_iso_urb /
 *         ehci_scan_isochronous_list, which the port declares and calls
 *         under CONFIG_USB_EHCI_ISO (usb_hc_ehci.h, usb_hc_ehci.c).
 *
 * An iTD spans the eight micro-frames of one frame: its per-micro-frame TSCL
 * slots carry one transfer each, and its nlp links in front of whatever the
 * frame list entry already carries, so the interrupt schedule that hangs off
 * the periodic head is untouched.  Scheduling is frame-granular: the k-th
 * iTD of a transfer goes into the frame list entry of (start_frame + k).
 *
 * The transfer is the urb's iso_packet[] array: one micro-frame scheduled
 * transaction per packet, placed on the micro-frames the endpoint's
 * high-speed period selects (bInterval exponent, period 2^(bInterval-1)
 * micro-frames, USB 2.0 chapter 9.6.6).  Completion comes from the interrupt
 * scan: a slot still carrying the ACTIVE status bit has not retired; once
 * every slot of every iTD has retired, the per-packet results (actual
 * length, error code) are mapped back and the urb completes - through its
 * callback, or through the semaphore a synchronous submit waits on.
 *
 * The buffer page pointer window of an iTD is seven consecutive pages
 * anchored at its first packet, so the packets of one iTD must start in -
 * and finish inside - that window.
 *
 * The file is split so its first half can run on a host: the scheduling core
 * (period decode, descriptor fill, retirement test, result mapping) touches
 * no controller register, and the frame list, cache discipline and
 * signalling work lives in the second half.
 *
 * Limits: high-speed endpoints only (full/low-speed isochronous needs the
 * split-transaction descriptor), and periods up to one frame - longer
 * periods need frame-scheduled descriptors.
 *
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usb_hc_ehci.h"

#ifdef CONFIG_USB_EHCI_ISO

/* The period encoding of a high-speed endpoint (USB 2.0 chapter 9.6.6):
 * bInterval is an exponent, the polling period is 2^(bInterval-1)
 * micro-frames.  One frame has eight micro-frames, so a period up to eight
 * micro-frames is expressible as a per-frame micro-frame mask; longer periods
 * span frames and are not scheduled by this engine. */
#define EHCI_ISO_MF_MASK_EVERY_UF  0xff /* period 1 micro-frame  */
#define EHCI_ISO_MF_MASK_EVERY_2UF 0x55 /* period 2 micro-frames */
#define EHCI_ISO_MF_MASK_EVERY_4UF 0x44 /* period 4 micro-frames */
#define EHCI_ISO_MF_MASK_EVERY_8UF 0x01 /* period 8 micro-frames */

/* The buffer page pointer list describes seven consecutive pages, so every
 * transaction of a descriptor must start in - and finish inside - the window
 * anchored at the first packet's page of that descriptor. */
#define EHCI_ISO_BPL_WINDOW        (7 * 4096)

/* The specification's per-transaction limit (the TSCL field encodes up to
 * 0xfff, but 0xc00 is the maximum a transaction may carry). */
#define EHCI_ISO_TSCL_LENGTH_MAX   0xc00

/* The first scheduled frame is placed a couple of frames after the frame the
 * submission lands in, so the descriptors are visible before the controller
 * reaches them. */
#define EHCI_ISO_START_FRAME_LEAD  2

/* Transfers are tiled on the frame list: a submission starts in the frame
 * after every transfer already scheduled on that bus, because two transfers
 * of one endpoint cannot share a micro-frame - the endpoint answers a single
 * transaction per micro-frame, so a second descriptor armed for the same
 * micro-frame only collects a NAK and the data lands in the first one.  The
 * reservation this makes is a frame number per bus; a reservation further
 * ahead than this horizon is stale (left behind by a stream that stopped) and
 * is not honoured. */
#define EHCI_ISO_FRAME_HORIZON     32

/* Descriptor lifetime: a retired transfer keeps its descriptors chained in the
 * frame list, and only the next user of the slot unlinks them - after waiting
 * for the frames they were scheduled in to lie this far behind the
 * controller.  Neither unlinking nor refilling descriptor memory while the
 * controller may still traverse or have cached the schedule of a frame that
 * just passed is safe; Linux's scheduler refuses to reuse an iTD whose frame
 * is the current one and frees finished ones only after a delay, which is the
 * same rule with the same reason. */
#define EHCI_ISO_FRAME_MARGIN      2

/* Two transfers in flight, each with CONFIG_USB_EHCI_ITD_NUM descriptors,
 * are the port's declared dimension (CONFIG_USB_EHCI_ISO_NUM slots). */
USB_NOCACHE_RAM_SECTION static struct ehci_iso_hw ehci_iso_pool[CONFIG_USBHOST_MAX_BUS][CONFIG_USB_EHCI_ISO_NUM];
static usb_osal_sem_t ehci_iso_sem[CONFIG_USBHOST_MAX_BUS][CONFIG_USB_EHCI_ISO_NUM];

/* Per slot: the frame its last schedule ends in, and whether its descriptors
 * are still chained in (see EHCI_ISO_FRAME_MARGIN). */
static uint16_t ehci_iso_slot_end[CONFIG_USBHOST_MAX_BUS][CONFIG_USB_EHCI_ISO_NUM];
static bool ehci_iso_slot_linked[CONFIG_USBHOST_MAX_BUS][CONFIG_USB_EHCI_ISO_NUM];

/* ------------------------------------------------------------------------
 * Scheduling core: descriptor structures only (host-testable)
 * --------------------------------------------------------------------- */

static int ehci_iso_period_of(uint8_t binterval, uint8_t *mf_unmask, uint8_t *mf_valid)
{
    switch (binterval) {
        case 0x01: /* every micro-frame */
            *mf_unmask = EHCI_ISO_MF_MASK_EVERY_UF;
            *mf_valid = 8;
            break;
        case 0x02: /* every second micro-frame */
            *mf_unmask = EHCI_ISO_MF_MASK_EVERY_2UF;
            *mf_valid = 4;
            break;
        case 0x03: /* every fourth micro-frame */
            *mf_unmask = EHCI_ISO_MF_MASK_EVERY_4UF;
            *mf_valid = 2;
            break;
        case 0x04: /* every eighth micro-frame (once per frame) */
            *mf_unmask = EHCI_ISO_MF_MASK_EVERY_8UF;
            *mf_valid = 1;
            break;
        default:
            /* periods above one frame need frame-scheduled descriptors */
            return -USB_ERR_NOTSUPP;
    }
    return 0;
}

/* The n-th micro-frame of a descriptor that carries a packet (n counts only
 * the selected micro-frames of mf_unmask).  Returns 8 when n is past the
 * descriptor's packet count. */
static uint8_t ehci_iso_itd_slot_of(uint8_t mf_unmask, uint8_t n)
{
    uint8_t seen = 0;

    for (uint8_t m = 0; m < 8; m++) {
        if (mf_unmask & (1U << m)) {
            if (seen == n) {
                return m;
            }
            seen++;
        }
    }
    return 8;
}

/* Fill one iTD with up to mf_valid packets starting at packet first_pkt.
 * Pure function of the urb: no registers, no cache, no locks. */
static int ehci_iso_itd_fill(struct ehci_itd_hw *itd,
                             struct usbh_urb *urb,
                             uint32_t first_pkt,
                             uint8_t pkt_count,
                             uint8_t mf_unmask)
{
    uint32_t base = (uint32_t)(uintptr_t)urb->iso_packet[first_pkt].transfer_buffer & ~0xfffU;

    memset(itd, 0, sizeof(*itd));
    for (uint32_t i = 0; i < 7; i++) {
        itd->hw.bpl[i] = base + i * 0x1000;
    }
    itd->hw.bpl[0] |= ((uint32_t)urb->hport->dev_addr << ITD_BUFPTR0_DEVADDR_SHIFT) |
                      ((uint32_t)(urb->ep->bEndpointAddress & 0x0f) << ITD_BUFPTR0_ENDPT_SHIFT);
    itd->hw.bpl[1] |= ((uint32_t)USB_GET_MAXPACKETSIZE(urb->ep->wMaxPacketSize) << ITD_BUFPTR1_MAXPKT_SHIFT) |
                      ((urb->ep->bEndpointAddress & 0x80) ? ITD_BUFPTR1_DIRIN : ITD_BUFPTR1_DIROUT);
    /* transactions per micro-frame (1..3); the same field the queue heads use */
    itd->hw.bpl[2] |= (uint32_t)(USB_GET_MULT(urb->ep->wMaxPacketSize) + 1);

    for (uint8_t n = 0; n < pkt_count; n++) {
        struct usbh_iso_frame_packet *packet = &urb->iso_packet[first_pkt + n];
        uint32_t addr = (uint32_t)(uintptr_t)packet->transfer_buffer;
        uint32_t len = packet->transfer_buffer_length;
        uint32_t rel;
        uint8_t m = ehci_iso_itd_slot_of(mf_unmask, n);
        uint32_t tscl;

        if (m >= 8) {
            /* the descriptor would not hold the packets the period promises */
            return -USB_ERR_INVAL;
        }
        /* the field's encoding cap is 12 bits, but the specification limits a
         * transaction to 0xc00 bytes */
        if (len > EHCI_ISO_TSCL_LENGTH_MAX) {
            return -USB_ERR_INVAL;
        }
        if (addr < base) {
            return -USB_ERR_INVAL;
        }
        rel = addr - base;
        if ((rel + len) > EHCI_ISO_BPL_WINDOW) {
            return -USB_ERR_INVAL;
        }
        /* a transaction starting in the window's last page must not wrap it */
        if (((rel >> 12) == 6) && (((addr & 0xfffU) + len) > 0x1000U)) {
            return -USB_ERR_INVAL;
        }

        tscl = (addr & ITD_TSCL_XOFFS_MASK) |
               (((rel >> 12) << ITD_TSCL_PG_SHIFT) & ITD_TSCL_PG_MASK) |
               ((len << ITD_TSCL_LENGTH_SHIFT) & ITD_TSCL_LENGTH_MASK) |
               ITD_TSCL_STATUS_ACTIVE;
        if ((first_pkt + n) == (urb->num_of_iso_packets - 1)) {
            /* one interrupt per transfer, on the last scheduled transaction */
            tscl |= ITD_TSCL_IOC;
        }

        itd->hw.tscl[m] = tscl;
        itd->pkt_idx[m] = first_pkt + n;
    }

    itd->urb = urb;
    itd->start_frame = 0; /* placed by the frame list link */
    itd->mf_unmask = mf_unmask;
    itd->mf_valid = pkt_count;
    itd->dir_in = (urb->ep->bEndpointAddress & 0x80) ? true : false;
    return 0;
}

/* Has every packet of this descriptor retired? */
static bool ehci_iso_itd_retired(struct ehci_itd_hw *itd)
{
    for (uint8_t n = 0; n < itd->mf_valid; n++) {
        uint8_t m = ehci_iso_itd_slot_of(itd->mf_unmask, n);

        if (m >= 8) {
            return true; /* nothing was programmed for this packet */
        }
        if (itd->hw.tscl[m] & ITD_TSCL_STATUS_ACTIVE) {
            return false;
        }
    }
    return true;
}

/* Map the retired slots of this descriptor back to their packets.  The urb's
 * errorcode/actual_length are accumulated by the caller, once every descriptor
 * of the transfer has retired. */
static void ehci_iso_itd_harvest(struct ehci_itd_hw *itd)
{
    struct usbh_urb *urb = itd->urb;

    for (uint8_t n = 0; n < itd->mf_valid; n++) {
        uint8_t m = ehci_iso_itd_slot_of(itd->mf_unmask, n);
        struct usbh_iso_frame_packet *packet;
        uint32_t tscl;

        if (m >= 8) {
            break;
        }
        packet = &urb->iso_packet[itd->pkt_idx[m]];
        tscl = itd->hw.tscl[m];
        uint32_t len = (tscl & ITD_TSCL_LENGTH_MASK) >> ITD_TSCL_LENGTH_SHIFT;

        if (tscl & ITD_TSCL_STATUS_BABBLE) {
            packet->errorcode = -USB_ERR_BABBLE;
        } else if (tscl & (ITD_TSCL_STATUS_XACTERR | ITD_TSCL_STATUS_DBERROR)) {
            packet->errorcode = -USB_ERR_IO;
        } else {
            packet->errorcode = 0;
        }
        packet->actual_length = itd->dir_in ? len : packet->transfer_buffer_length;

        urb->actual_length += packet->actual_length;
        if (packet->errorcode < 0 && urb->errorcode == 0) {
            urb->errorcode = packet->errorcode;
        }
    }
}

/* ------------------------------------------------------------------------
 * Controller half: frame list, cache discipline, completion signalling
 * --------------------------------------------------------------------- */

static uint16_t ehci_iso_frame_number(struct usbh_bus *bus)
{
    (void)bus;
    return (uint16_t)(((EHCI_HCOR->frindex & EHCI_FRINDEX_MASK) >> 3) & 0x3ff);
}

/* True when the frame `end` lies at least `margin` frames behind `now`. */
static bool ehci_iso_frame_passed(uint16_t end, uint16_t now, uint16_t margin)
{
    uint16_t behind = (uint16_t)((now - end) & (CONFIG_USB_EHCI_FRAME_LIST_SIZE - 1));

    return behind >= margin;
}

/* Wait - best effort, bounded - until the frames the given slot was last
 * scheduled in are behind the controller, so its descriptor memory may be
 * unlinked and reused.  Called before the submit's critical section. */
static void ehci_iso_slot_settle(struct usbh_bus *bus, uint32_t index)
{
    for (uint32_t spin = 0; spin < 16; spin++) {
        if (ehci_iso_frame_passed(ehci_iso_slot_end[bus->hcd.hcd_id][index],
                                  ehci_iso_frame_number(bus), EHCI_ISO_FRAME_MARGIN)) {
            return;
        }
        usb_osal_msleep(1);
    }
}

/* Reserve the frames a transfer of itd_num descriptors needs and return the
 * first one.  Runs under the caller's critical section, like the linking.
 *
 * The cursor's two fields are read modulo the frame list: the reservation
 * counts as live only while it lies inside the horizon ahead of the free
 * frame AND the cursor was advanced no longer than the horizon ago - the
 * second test is what tells a live reservation apart from a stream's stale
 * one, whose distance aliases into the same window after a frame-count
 * wrap. */
static uint16_t ehci_iso_frame_take(struct usbh_bus *bus, uint32_t itd_num)
{
    struct ehci_hcd *hcd = &g_ehci_hcd[bus->hcd.hcd_id];
    uint16_t free_frame = (uint16_t)((ehci_iso_frame_number(bus) + EHCI_ISO_START_FRAME_LEAD) %
                                     CONFIG_USB_EHCI_FRAME_LIST_SIZE);
    uint16_t reserved = hcd->iso_frame_next;
    uint16_t ahead = (uint16_t)((reserved - free_frame) & (CONFIG_USB_EHCI_FRAME_LIST_SIZE - 1));
    uint16_t base_age = (uint16_t)((free_frame - hcd->iso_frame_base) & (CONFIG_USB_EHCI_FRAME_LIST_SIZE - 1));
    uint16_t start;

    if ((ahead > 0) && (ahead < EHCI_ISO_FRAME_HORIZON) &&
        (base_age < EHCI_ISO_FRAME_HORIZON)) {
        start = reserved;
    } else {
        start = free_frame;
    }
    hcd->iso_frame_base = free_frame;
    hcd->iso_frame_next = (uint16_t)((start + itd_num) % CONFIG_USB_EHCI_FRAME_LIST_SIZE);
    return start;
}

static void ehci_iso_itd_flush(struct ehci_itd_hw *itd)
{
#if defined(CONFIG_USB_EHCI_DESC_DCACHE_ENABLE)
    usb_dcache_clean((uintptr_t)&itd->hw, CONFIG_USB_EHCI_ALIGN_SIZE);
#else
    (void)itd;
#endif
}

static void ehci_iso_itd_refresh(struct ehci_itd_hw *itd)
{
#if defined(CONFIG_USB_EHCI_DESC_DCACHE_ENABLE)
    usb_dcache_invalidate((uintptr_t)&itd->hw, CONFIG_USB_EHCI_ALIGN_SIZE);
#else
    (void)itd;
#endif
}

/* Chain the descriptor in front of whatever the frame entry carries (normally
 * the periodic queue head of the interrupt schedule) and hand the entry over
 * once the descriptor is written back. */
static void ehci_iso_framelist_link(struct usbh_bus *bus, struct ehci_itd_hw *itd, uint16_t frame)
{
    uint32_t *entry = &g_framelist[bus->hcd.hcd_id][frame % CONFIG_USB_EHCI_FRAME_LIST_SIZE];

    itd->start_frame = (uint16_t)(frame % CONFIG_USB_EHCI_FRAME_LIST_SIZE);
    itd->hw.nlp = *entry;
    ehci_iso_itd_flush(itd);
    *entry = ITD_NLP_ITD(itd);
#if defined(CONFIG_USB_EHCI_DESC_DCACHE_ENABLE)
    usb_dcache_clean((uintptr_t)entry, sizeof(uint32_t));
#endif
}

/* Take the descriptor back out of the chain it was linked into. */
static void ehci_iso_framelist_unlink(struct usbh_bus *bus, struct ehci_itd_hw *itd)
{
    uint32_t *entry = &g_framelist[bus->hcd.hcd_id][itd->start_frame % CONFIG_USB_EHCI_FRAME_LIST_SIZE];
    uint32_t cur = *entry;

    if (EHCI_ADDR2ITD(cur) == itd) {
        *entry = itd->hw.nlp;
#if defined(CONFIG_USB_EHCI_DESC_DCACHE_ENABLE)
        usb_dcache_clean((uintptr_t)entry, sizeof(uint32_t));
#endif
        return;
    }

    /* walk only entries typed as iTD (bits 2:1 == 00); a queue head or the
     * terminate marker ends the walk */
    while ((cur & 0x6U) == 0U) {
        struct ehci_itd_hw *prev = EHCI_ADDR2ITD(cur);
        uint32_t next;

        if (prev == NULL) {
            return;
        }
        ehci_iso_itd_refresh(prev);
        next = prev->hw.nlp;
        if (EHCI_ADDR2ITD(next) == itd) {
            prev->hw.nlp = itd->hw.nlp;
            ehci_iso_itd_flush(prev);
            return;
        }
        cur = next;
    }
}

static struct ehci_iso_hw *ehci_iso_slot_of(struct usbh_bus *bus, struct usbh_urb *urb, uint32_t *index)
{
    for (uint32_t i = 0; i < CONFIG_USB_EHCI_ISO_NUM; i++) {
        if (g_ehci_hcd[bus->hcd.hcd_id].ehci_iso_used[i] &&
            (ehci_iso_pool[bus->hcd.hcd_id][i].itd_pool[0].urb == urb)) {
            *index = i;
            return &ehci_iso_pool[bus->hcd.hcd_id][i];
        }
    }
    return NULL;
}

static void ehci_iso_slot_release(struct usbh_bus *bus, uint32_t index)
{
    struct ehci_iso_hw *slot = &ehci_iso_pool[bus->hcd.hcd_id][index];

    /* itd_num is kept: a retired transfer's descriptors remain chained in the
     * frame list until the next user of the slot unlinks them (see
     * EHCI_ISO_FRAME_MARGIN) */
    slot->itd_pool[0].urb = NULL;
    g_ehci_hcd[bus->hcd.hcd_id].ehci_iso_used[index] = false;
}

/* Detach an in-flight transfer: unlink its descriptors and free its slot.
 * Runs under the caller's critical section; the callers have stopped the
 * periodic schedule, and the frame boundary is awaited here before the
 * descriptors a controller walk may still be inside are touched (a bounded
 * spin on the frame counter, the same idiom the port's iaad wait uses). */
static void ehci_iso_urb_detach(struct usbh_bus *bus, struct usbh_urb *urb)
{
    uint32_t index;
    struct ehci_iso_hw *slot = ehci_iso_slot_of(bus, urb, &index);
    uint16_t frame;

    if (slot == NULL) {
        return;
    }
    frame = ehci_iso_frame_number(bus);
    for (volatile uint32_t spin = 0;
         (ehci_iso_frame_number(bus) == frame) && (spin < 20000U); spin++) {
    }
    for (uint32_t k = 0; k < slot->itd_num; k++) {
        ehci_iso_framelist_unlink(bus, &slot->itd_pool[k]);
    }
    ehci_iso_slot_linked[bus->hcd.hcd_id][index] = false;
    ehci_iso_slot_release(bus, index);
}

/* The transfer retired: collect every packet and complete the urb. */
static void ehci_iso_urb_complete(struct usbh_bus *bus, uint32_t index)
{
    struct ehci_iso_hw *slot = &ehci_iso_pool[bus->hcd.hcd_id][index];
    struct usbh_urb *urb = slot->itd_pool[0].urb;
    size_t flags;

    flags = usb_osal_enter_critical_section();

    /* the descriptors stay chained: all their transactions are retired (the
     * scan proved it), so the controller skips them, and the next user of the
     * slot unlinks them once their frames are safely behind (see
     * EHCI_ISO_FRAME_MARGIN) */

    urb->actual_length = 0;
    urb->errorcode = 0;
    for (uint32_t k = 0; k < slot->itd_num; k++) {
        ehci_iso_itd_refresh(&slot->itd_pool[k]);
        ehci_iso_itd_harvest(&slot->itd_pool[k]);
    }

#if defined(CONFIG_USB_DCACHE_ENABLE)
    if (urb->ep->bEndpointAddress & 0x80) {
        for (uint32_t p = 0; p < urb->num_of_iso_packets; p++) {
            struct usbh_iso_frame_packet *packet = &urb->iso_packet[p];

            if (packet->actual_length) {
                usb_dcache_invalidate((uintptr_t)packet->transfer_buffer,
                                      USB_ALIGN_UP(packet->actual_length, CONFIG_USB_ALIGN_SIZE));
            }
        }
    }
#endif

    urb->hcpriv = NULL;
    ehci_iso_slot_release(bus, index);

    if (urb->timeout) {
        usb_osal_sem_give(ehci_iso_sem[bus->hcd.hcd_id][index]);
    }

    usb_osal_leave_critical_section(flags);

    if (urb->complete) {
        if (urb->errorcode < 0) {
            urb->complete(urb->arg, urb->errorcode);
        } else {
            urb->complete(urb->arg, (int)urb->actual_length);
        }
    }
}

/* ------------------------------------------------------------------------
 * Entry points called by the port (see usb_hc_ehci.h)
 * --------------------------------------------------------------------- */

int ehci_iso_urb_init(struct usbh_bus *bus, struct usbh_urb *urb)
{
    struct ehci_iso_hw *slot;
    uint32_t slot_index = 0;
    uint32_t packets = urb->num_of_iso_packets;
    uint16_t start_frame;
    uint8_t mf_unmask = 0;
    uint8_t mf_valid = 0;
    uint32_t itd_num;
    size_t flags;
    int ret;

    if ((urb->ep == NULL) ||
        (USB_GET_ENDPOINT_TYPE(urb->ep->bmAttributes) != USB_ENDPOINT_TYPE_ISOCHRONOUS)) {
        return -USB_ERR_INVAL;
    }
    if (urb->hport->speed != USB_SPEED_HIGH) {
        /* full/low-speed isochronous needs the split-transaction descriptor */
        return -USB_ERR_NOTSUPP;
    }
    ret = ehci_iso_period_of(urb->ep->bInterval, &mf_unmask, &mf_valid);
    if (ret < 0) {
        return ret;
    }
    if ((packets == 0) || (packets > (uint32_t)CONFIG_USB_EHCI_ITD_NUM * mf_valid)) {
        return -USB_ERR_INVAL;
    }
    itd_num = (packets + mf_valid - 1) / mf_valid;

    for (uint32_t p = 0; p < packets; p++) {
        if (urb->iso_packet[p].transfer_buffer == NULL) {
            return -USB_ERR_INVAL;
        }
#if defined(CONFIG_USB_DCACHE_ENABLE)
        if ((uintptr_t)urb->iso_packet[p].transfer_buffer % CONFIG_USB_ALIGN_SIZE) {
            return -USB_ERR_INVAL;
        }
#endif
    }

    /* Pick the slot the claim below will most likely take and let its
     * previous schedule pass before anything touches its memory. */
    for (uint32_t i = 0; i < CONFIG_USB_EHCI_ISO_NUM; i++) {
        if (!g_ehci_hcd[bus->hcd.hcd_id].ehci_iso_used[i]) {
            ehci_iso_slot_settle(bus, i);
            break;
        }
    }

    flags = usb_osal_enter_critical_section();

    /* the per-slot semaphore exists before the transfer can complete */
    for (uint32_t i = 0; i < CONFIG_USB_EHCI_ISO_NUM; i++) {
        if (ehci_iso_sem[bus->hcd.hcd_id][i] == NULL) {
            ehci_iso_sem[bus->hcd.hcd_id][i] = usb_osal_sem_create(0);
            if (ehci_iso_sem[bus->hcd.hcd_id][i] == NULL) {
                usb_osal_leave_critical_section(flags);
                return -USB_ERR_NOMEM;
            }
        }
    }

    slot = NULL;
    for (uint32_t i = 0; i < CONFIG_USB_EHCI_ISO_NUM; i++) {
        if (!g_ehci_hcd[bus->hcd.hcd_id].ehci_iso_used[i]) {
            g_ehci_hcd[bus->hcd.hcd_id].ehci_iso_used[i] = true;
            slot = &ehci_iso_pool[bus->hcd.hcd_id][i];
            slot_index = i;
            break;
        }
    }
    if (slot == NULL) {
        usb_osal_leave_critical_section(flags);
        return -USB_ERR_NOMEM;
    }
    /* the previous owner's descriptors are still chained in; unlink them
     * before their memory is reused */
    if (ehci_iso_slot_linked[bus->hcd.hcd_id][slot_index]) {
        for (uint32_t k = 0; k < slot->itd_num; k++) {
            ehci_iso_framelist_unlink(bus, &slot->itd_pool[k]);
        }
        ehci_iso_slot_linked[bus->hcd.hcd_id][slot_index] = false;
    }
    /* invisible to a concurrent interrupt scan until the descriptors below
     * are filled and the slot is published */
    memset(slot->itd_pool, 0, sizeof(slot->itd_pool));
    slot->itd_num = 0;

    usb_osal_leave_critical_section(flags);

    /* the buffers: an IN endpoint wants stale lines dropped before the device
     * writes, an OUT endpoint wants the submitted bytes written back */
    for (uint32_t p = 0; p < packets; p++) {
        struct usbh_iso_frame_packet *packet = &urb->iso_packet[p];
        uint32_t len = packet->transfer_buffer_length;

        if (len == 0) {
            continue;
        }
        if (urb->ep->bEndpointAddress & 0x80) {
            usb_dcache_invalidate((uintptr_t)packet->transfer_buffer,
                                  USB_ALIGN_UP(len, CONFIG_USB_ALIGN_SIZE));
        } else {
            usb_dcache_clean((uintptr_t)packet->transfer_buffer,
                             USB_ALIGN_UP(len, CONFIG_USB_ALIGN_SIZE));
        }
    }

    /* a late completion of the previous owner of this slot may have left a
     * token behind; nothing may be waiting on it now */
    while (usb_osal_sem_take(ehci_iso_sem[bus->hcd.hcd_id][slot_index], 0) == 0) {
    }

    flags = usb_osal_enter_critical_section();

    ret = 0;
    for (uint32_t k = 0; k < itd_num; k++) {
        uint32_t first_pkt = k * mf_valid;
        uint8_t pkt_count = (uint8_t)((packets - first_pkt) > mf_valid ? mf_valid : (packets - first_pkt));

        ret = ehci_iso_itd_fill(&slot->itd_pool[k], urb, first_pkt, pkt_count, mf_unmask);
        if (ret < 0) {
            break;
        }
    }

    if (ret == 0) {
        start_frame = ehci_iso_frame_take(bus, itd_num);

        slot->itd_num = itd_num;
        for (uint32_t k = 0; k < itd_num; k++) {
            ehci_iso_framelist_link(bus, &slot->itd_pool[k],
                                    (uint16_t)((start_frame + k) % CONFIG_USB_EHCI_FRAME_LIST_SIZE));
        }
        ehci_iso_slot_linked[bus->hcd.hcd_id][slot_index] = true;
        ehci_iso_slot_end[bus->hcd.hcd_id][slot_index] =
            (uint16_t)((start_frame + itd_num) % CONFIG_USB_EHCI_FRAME_LIST_SIZE);

        urb->start_frame = start_frame;
        urb->hcpriv = &slot->itd_pool[0];

        EHCI_HCOR->usbcmd |= EHCI_USBCMD_PSEN;
    } else {
        ehci_iso_slot_release(bus, slot_index);
        urb->errorcode = ret;
    }

    usb_osal_leave_critical_section(flags);

    if (ret < 0) {
        return ret;
    }

    if (urb->timeout == 0) {
        return 0;
    }

    /* synchronous submit: wait for the completion the interrupt path signals */
    ret = usb_osal_sem_take(ehci_iso_sem[bus->hcd.hcd_id][slot_index], urb->timeout);
    if (ret < 0) {
        /* stop a late completion from handing the slot's token over */
        urb->timeout = 0;
        flags = usb_osal_enter_critical_section();
        EHCI_HCOR->usbcmd &= ~EHCI_USBCMD_PSEN;
        ehci_iso_urb_detach(bus, urb);
        EHCI_HCOR->usbcmd |= EHCI_USBCMD_PSEN;
        usb_osal_leave_critical_section(flags);
        urb->hcpriv = NULL;
        urb->errorcode = ret;
        return ret;
    }
    urb->timeout = 0;
    return urb->errorcode;
}

void ehci_kill_iso_urb(struct usbh_bus *bus, struct usbh_urb *urb)
{
    if ((urb == NULL) || (urb->hcpriv == NULL)) {
        return;
    }

    ehci_iso_urb_detach(bus, urb);

    urb->hcpriv = NULL;
    urb->actual_length = 0;
    urb->errorcode = -USB_ERR_SHUTDOWN;
}

void ehci_scan_isochronous_list(struct usbh_bus *bus)
{
    for (uint32_t i = 0; i < CONFIG_USB_EHCI_ISO_NUM; i++) {
        struct ehci_iso_hw *slot;
        struct usbh_urb *urb;
        bool pending = false;

        if (!g_ehci_hcd[bus->hcd.hcd_id].ehci_iso_used[i]) {
            continue;
        }
        slot = &ehci_iso_pool[bus->hcd.hcd_id][i];
        urb = slot->itd_pool[0].urb;
        if (urb == NULL) {
            continue;
        }
        for (uint32_t k = 0; k < slot->itd_num; k++) {
            ehci_iso_itd_refresh(&slot->itd_pool[k]);
            if (!ehci_iso_itd_retired(&slot->itd_pool[k])) {
                pending = true;
                break;
            }
        }
        if (pending) {
            continue;
        }
        ehci_iso_urb_complete(bus, i);
    }
}

#endif /* CONFIG_USB_EHCI_ISO */
