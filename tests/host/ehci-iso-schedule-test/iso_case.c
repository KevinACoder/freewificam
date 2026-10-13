/*
 * @file    iso_case.c
 * @brief   Cases for the EHCI isochronous engine, driven against a controller
 *          that lives in RAM.
 *
 * The translation units under test are the vendored port/ehci/usb_hc_ehci.c
 * and the patched port/ehci/usb_hc_ehci_iso.c - the real driver, not doubles:
 *
 *   - the controller's register window is a plain struct ehci_hcor in .bss;
 *     the bus's reg_base points at it, so EHCI_HCOR's loads and stores are
 *     ordinary memory accesses the suite can inspect and preload (frindex
 *     decides start_frame, portsc[0] carries the connect bit the submit path
 *     checks);
 *   - the frame list, the periodic queue head and the descriptor pools are the
 *     driver's own globals, initialised here the way usb_hc_init initialises
 *     them (the entries all point at the periodic queue head) - that initial
 *     state is also the expectation for "the chain is untouched";
 *   - completion is faked exactly the way the controller writes it back:
 *     clear ACTIVE, report a length, optionally raise a status bit, then run
 *     the port's own interrupt scan (ehci_scan_isochronous_list).
 *
 * The expectations are derived from the EHCI specification's descriptor
 * encoding (iTD TSCL and buffer pointer list, paragraph 3.3), the USB 2.0
 * high-speed period encoding (chapter 9.6.6: period = 2^(bInterval-1)
 * micro-frames) and the port's declared urb contract (common/usb_hc.h) - not
 * from the implementation under test.
 *
 * Every case reports `EHCIISO: <name> PASS|FAIL`; the summary line's counts
 * are what tools/mock/ehci-iso-schedule-test/check.sh reads.
 */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "usbh_core.h"
#include "usbh_hub.h"
#include "usb_hc_ehci.h"

/* The port renames its entry points in the multi-controller build, exactly as
 * the board's usb_config.h configures it; the suite calls them under the same
 * names the core's dispatcher would. */
#if defined(CONFIG_USBHOST_MULT_HC)
extern int usbh_ehci_submit_urb(struct usbh_urb *urb);
extern int usbh_ehci_kill_urb(struct usbh_urb *urb);
#define TEST_SUBMIT usbh_ehci_submit_urb
#define TEST_KILL   usbh_ehci_kill_urb
#else
#define TEST_SUBMIT usbh_submit_urb
#define TEST_KILL   usbh_kill_urb
#endif

/* The periodic queue head is the driver's own global (no header declares it);
 * the suite needs it to assert the "chain untouched" expectation. */
extern struct ehci_qh_hw g_periodic_qh_head[CONFIG_USBHOST_MAX_BUS];

/* ------------------------------------------------------------------------
 * fixture
 * --------------------------------------------------------------------- */

static struct ehci_hcor g_regs;
static struct usbh_bus g_bus;
static struct usbh_hub g_roothub;
static struct usbh_hubport g_hport;
static uint8_t g_arena[64 * 1024] __attribute__((aligned(4096)));

#define TEST_BUS 0

static struct usb_endpoint_descriptor g_ep;

static void ep_default(uint8_t address, uint16_t mps, uint8_t binterval)
{
    memset(&g_ep, 0, sizeof(g_ep));
    g_ep.bLength = 7;
    g_ep.bDescriptorType = 5;
    g_ep.bEndpointAddress = address;
    g_ep.bmAttributes = USB_ENDPOINT_TYPE_ISOCHRONOUS;
    g_ep.wMaxPacketSize = mps;
    g_ep.bInterval = binterval;
}

static void controller_reset(uint16_t frame)
{
    memset(&g_regs, 0, sizeof(g_regs));
    memset(&g_bus, 0, sizeof(g_bus));
    g_bus.hcd.hcd_id = TEST_BUS;
    g_bus.hcd.reg_base = (uintptr_t)&g_regs;

    memset(&g_ehci_hcd[TEST_BUS], 0, sizeof(g_ehci_hcd[TEST_BUS]));
    memset(g_framelist[TEST_BUS], 0, sizeof(uint32_t) * CONFIG_USB_EHCI_FRAME_LIST_SIZE);

    memset(&g_periodic_qh_head[TEST_BUS], 0, sizeof(struct ehci_qh_hw));
    g_periodic_qh_head[TEST_BUS].hw.hlp = QH_HLP_END;
    g_periodic_qh_head[TEST_BUS].hw.epchar = QH_EPCAPS_SSMASK(1);
    g_periodic_qh_head[TEST_BUS].hw.overlay.next_qtd = QTD_LIST_END;
    g_periodic_qh_head[TEST_BUS].hw.overlay.alt_next_qtd = QTD_LIST_END;
    g_periodic_qh_head[TEST_BUS].hw.overlay.token = QTD_TOKEN_STATUS_HALTED;
    g_periodic_qh_head[TEST_BUS].first_qtd = QTD_LIST_END;

    for (uint32_t i = 0; i < CONFIG_USB_EHCI_FRAME_LIST_SIZE; i++) {
        g_framelist[TEST_BUS][i] = QH_HLP_QH(&g_periodic_qh_head[TEST_BUS]);
    }

    g_regs.frindex = (uint32_t)frame << 3;
    g_regs.usbcmd = EHCI_USBCMD_PSEN;
    g_regs.portsc[0] = EHCI_PORTSC_CCS;

    memset(&g_roothub, 0, sizeof(g_roothub));
    g_roothub.is_roothub = true;
    memset(&g_hport, 0, sizeof(g_hport));
    g_hport.parent = &g_roothub;
    g_hport.port = 1;
    g_hport.connected = 1;
    g_hport.dev_addr = 3;
    g_hport.speed = USB_SPEED_HIGH;
    g_hport.bus = &g_bus;
}

static struct usbh_urb *urb_new(uint32_t packets, uint32_t default_len)
{
    struct usbh_urb *urb = calloc(1, sizeof(struct usbh_urb) +
                                         packets * sizeof(struct usbh_iso_frame_packet));

    if (urb == NULL) {
        return NULL;
    }
    urb->hport = &g_hport;
    urb->ep = &g_ep;
    urb->num_of_iso_packets = packets;
    for (uint32_t i = 0; i < packets; i++) {
        urb->iso_packet[i].transfer_buffer = g_arena + i * 0x100;
        urb->iso_packet[i].transfer_buffer_length = default_len;
    }
    return urb;
}

static void urb_free(struct usbh_urb *urb)
{
    free(urb);
}

/* ------------------------------------------------------------------------
 * controller-side helpers (independent of the implementation)
 * --------------------------------------------------------------------- */

static bool entry_is_itd(uint32_t entry)
{
    return (entry & 0x6U) == 0U && (entry & ~0x1FU) != 0U;
}

static struct ehci_itd_hw *entry_itd(uint32_t entry)
{
    return (struct ehci_itd_hw *)(uintptr_t)(entry & ~0x1FU);
}

static struct ehci_itd_hw *linked_itd(uint32_t frame)
{
    uint32_t entry = g_framelist[TEST_BUS][frame % CONFIG_USB_EHCI_FRAME_LIST_SIZE];

    return entry_is_itd(entry) ? entry_itd(entry) : NULL;
}

/* The n-th selected micro-frame of a period mask - the suite's own reading of
 * the period encoding, so the mapping assertion is not taken from the code
 * under test. */
static uint8_t host_slot_of(uint8_t mask, uint8_t n)
{
    uint8_t seen = 0;

    for (uint8_t m = 0; m < 8; m++) {
        if (mask & (1U << m)) {
            if (seen == n) {
                return m;
            }
            seen++;
        }
    }
    return 8;
}

/* Fake a retired transaction the way the controller writes it back: ACTIVE
 * cleared, length reported, optional status bits raised. */
static void host_retire(struct ehci_itd_hw *itd, uint8_t n, uint32_t len, uint32_t status)
{
    uint8_t m = host_slot_of(itd->mf_unmask, n);
    uint32_t tscl;

    if (m >= 8) {
        return;
    }
    tscl = itd->hw.tscl[m];
    tscl &= ~(ITD_TSCL_STATUS_MASK | ITD_TSCL_LENGTH_MASK);
    tscl |= ((len << ITD_TSCL_LENGTH_SHIFT) & ITD_TSCL_LENGTH_MASK) | (status & ITD_TSCL_STATUS_MASK);
    itd->hw.tscl[m] = tscl;
}

/* ------------------------------------------------------------------------
 * completion recording
 * --------------------------------------------------------------------- */

struct completion {
    int calls;
    void *arg;
    int nbytes;
};

static void completion_cb(void *arg, int nbytes)
{
    struct completion *c = arg;

    c->calls++;
    c->arg = arg;
    c->nbytes = nbytes;
}

/* ------------------------------------------------------------------------
 * cases
 * --------------------------------------------------------------------- */

/* The period encoding: bInterval 1..4 select 8/4/2/1 transactions per frame,
 * so a 4-packet transfer needs 1/1/2/4 descriptors and the descriptor's mask
 * and count fields carry the table. */
static bool case_period_mask(char *why, size_t n)
{
    static const struct {
        uint8_t binterval;
        uint8_t mask;
        uint8_t valid;
        uint8_t itds;
    } t[] = {
        { 1, 0xff, 8, 1 },
        { 2, 0x55, 4, 1 },
        { 3, 0x44, 2, 2 },
        { 4, 0x01, 1, 4 },
    };

    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        struct usbh_urb *urb;
        struct ehci_itd_hw *itd0;
        uint16_t frame;

        controller_reset(100);
        ep_default(0x81, 0x0180, t[i].binterval);
        urb = urb_new(4, 64);
        if (urb == NULL) {
            snprintf(why, n, "alloc failed");
            return false;
        }
        if (TEST_SUBMIT(urb) != 0) {
            snprintf(why, n, "bInterval %u: submit failed", t[i].binterval);
            urb_free(urb);
            return false;
        }
        frame = urb->start_frame;
        itd0 = linked_itd(frame);
        if (itd0 == NULL) {
            snprintf(why, n, "bInterval %u: nothing linked at start frame %u", t[i].binterval, frame);
            TEST_KILL(urb);
            urb_free(urb);
            return false;
        }
        if (itd0->mf_unmask != t[i].mask) {
            snprintf(why, n, "bInterval %u: mask 0x%02x, expected 0x%02x", t[i].binterval, itd0->mf_unmask, t[i].mask);
            TEST_KILL(urb);
            urb_free(urb);
            return false;
        }
        if (itd0->mf_valid != (t[i].valid < 4 ? t[i].valid : 4)) {
            snprintf(why, n, "bInterval %u: descriptor holds %u packets, expected %u", t[i].binterval, itd0->mf_valid, t[i].valid < 4 ? t[i].valid : 4);
            TEST_KILL(urb);
            urb_free(urb);
            return false;
        }
        for (uint8_t k = 0; k < t[i].itds; k++) {
            struct ehci_itd_hw *itd = linked_itd((uint16_t)(frame + k));

            if (itd == NULL) {
                snprintf(why, n, "bInterval %u: descriptor %u not linked", t[i].binterval, k);
                TEST_KILL(urb);
                urb_free(urb);
                return false;
            }
            if (itd->mf_valid != (k + 1 == t[i].itds ? (uint8_t)(4 - k * t[i].valid) : t[i].valid)) {
                snprintf(why, n, "bInterval %u: descriptor %u carries %u packets", t[i].binterval, k, itd->mf_valid);
                TEST_KILL(urb);
                urb_free(urb);
                return false;
            }
            /* the first mf_valid selected micro-frames of this descriptor
             * carry armed packets; every other slot stays empty */
            {
                uint8_t seen = 0;

                for (uint8_t s = 0; s < 8; s++) {
                    uint32_t tscl = itd->hw.tscl[s];

                    if (!(itd->mf_unmask & (1U << s))) {
                        if (tscl != 0) {
                            snprintf(why, n, "bInterval %u: unselected micro-frame %u is armed", t[i].binterval, s);
                            TEST_KILL(urb);
                            urb_free(urb);
                            return false;
                        }
                        continue;
                    }
                    seen++;
                    if (seen <= itd->mf_valid) {
                        if (!(tscl & ITD_TSCL_STATUS_ACTIVE)) {
                            snprintf(why, n, "bInterval %u: selected micro-frame %u carries no armed packet", t[i].binterval, s);
                            TEST_KILL(urb);
                            urb_free(urb);
                            return false;
                        }
                    } else if (tscl != 0) {
                        snprintf(why, n, "bInterval %u: micro-frame %u carries a packet past the descriptor's count", t[i].binterval, s);
                        TEST_KILL(urb);
                        urb_free(urb);
                        return false;
                    }
                }
            }
        }
        if (entry_is_itd(g_framelist[TEST_BUS][(frame + t[i].itds) % CONFIG_USB_EHCI_FRAME_LIST_SIZE])) {
            snprintf(why, n, "bInterval %u: more descriptors linked than expected", t[i].binterval);
            TEST_KILL(urb);
            urb_free(urb);
            return false;
        }
        TEST_KILL(urb);
        urb_free(urb);
    }
    return true;
}

/* The wire encoding: TSCL offsets/page/length, the buffer pointer list, and
 * IOC on the last packet only. */
static bool case_tscl_encode(char *why, size_t n)
{
    struct usbh_urb *urb;
    struct ehci_itd_hw *itd;
    uint32_t base = (uint32_t)(uintptr_t)g_arena;

    controller_reset(500);
    ep_default(0x81, 0x0180, 1);
    urb = urb_new(3, 0);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed");
        return false;
    }
    urb->iso_packet[0].transfer_buffer = g_arena + 0x100;
    urb->iso_packet[0].transfer_buffer_length = 100;
    urb->iso_packet[1].transfer_buffer = g_arena + 0x1200;
    urb->iso_packet[1].transfer_buffer_length = 200;
    urb->iso_packet[2].transfer_buffer = g_arena + 0x3040;
    urb->iso_packet[2].transfer_buffer_length = 300;

    if (TEST_SUBMIT(urb) != 0) {
        snprintf(why, n, "submit failed");
        urb_free(urb);
        return false;
    }
    itd = linked_itd(urb->start_frame);
    if (itd == NULL) {
        snprintf(why, n, "no descriptor linked");
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }

    for (uint32_t i = 0; i < 7; i++) {
        /* the low bits of bpl[0..2] carry the device/endpoint, packet size and
         * direction, and multiplier fields; the page part is the window */
        if ((itd->hw.bpl[i] & 0xfffff000U) != base + i * 0x1000) {
            snprintf(why, n, "bpl[%u] = 0x%08x, expected window page 0x%08x", i, itd->hw.bpl[i], base + i * 0x1000);
            goto fail;
        }
    }
    if ((itd->hw.bpl[0] & ITD_BUFPTR0_DEVADDR_MASK) != 3 ||
        ((itd->hw.bpl[0] & ITD_BUFPTR0_ENDPT_MASK) >> ITD_BUFPTR0_ENDPT_SHIFT) != 1) {
        snprintf(why, n, "bpl[0] device/endpoint field = 0x%08x", itd->hw.bpl[0]);
        goto fail;
    }
    if ((itd->hw.bpl[1] & ITD_BUFPTR1_MAXPKT_MASK) != 0x0180 ||
        (itd->hw.bpl[1] & ITD_BUFPTR1_DIRIN) == 0) {
        snprintf(why, n, "bpl[1] = 0x%08x (maxp/direction)", itd->hw.bpl[1]);
        goto fail;
    }
    if ((itd->hw.bpl[2] & ITD_BUFPTR2_MULTI_MASK) != 1) {
        snprintf(why, n, "bpl[2] multiplier = %u, expected 1 transaction", itd->hw.bpl[2] & ITD_BUFPTR2_MULTI_MASK);
        goto fail;
    }

    {
        struct {
            uint32_t xoffs;
            uint32_t pg;
            uint32_t len;
            bool ioc;
        } exp[3] = {
            { 0x100, 0, 100, false },
            { 0x200, 1, 200, false },
            { 0x040, 3, 300, true },
        };

        for (uint8_t p = 0; p < 3; p++) {
            uint8_t m = host_slot_of(itd->mf_unmask, p);
            uint32_t tscl = itd->hw.tscl[m];

            if (!(tscl & ITD_TSCL_STATUS_ACTIVE)) {
                snprintf(why, n, "packet %u: slot %u is not active", p, m);
                goto fail;
            }
            if ((tscl & ITD_TSCL_XOFFS_MASK) != exp[p].xoffs) {
                snprintf(why, n, "packet %u: xoffs 0x%x, expected 0x%x", p, tscl & ITD_TSCL_XOFFS_MASK, exp[p].xoffs);
                goto fail;
            }
            if (((tscl & ITD_TSCL_PG_MASK) >> ITD_TSCL_PG_SHIFT) != exp[p].pg) {
                snprintf(why, n, "packet %u: page %u, expected %u", p, (tscl & ITD_TSCL_PG_MASK) >> ITD_TSCL_PG_SHIFT, exp[p].pg);
                goto fail;
            }
            if (((tscl & ITD_TSCL_LENGTH_MASK) >> ITD_TSCL_LENGTH_SHIFT) != exp[p].len) {
                snprintf(why, n, "packet %u: length %u, expected %u", p, (tscl & ITD_TSCL_LENGTH_MASK) >> ITD_TSCL_LENGTH_SHIFT, exp[p].len);
                goto fail;
            }
            if (((tscl & ITD_TSCL_IOC) != 0) != exp[p].ioc) {
                snprintf(why, n, "packet %u: IOC %u, expected %u", p, (tscl & ITD_TSCL_IOC) != 0, exp[p].ioc);
                goto fail;
            }
            if (itd->pkt_idx[m] != p) {
                snprintf(why, n, "slot %u maps to packet %u, expected %u", m, itd->pkt_idx[m], p);
                goto fail;
            }
        }
        for (uint8_t s = 3; s < 8; s++) {
            if (itd->mf_unmask & (1U << s)) {
                if (itd->hw.tscl[s] != 0) {
                    snprintf(why, n, "unused slot %u is not empty", s);
                    goto fail;
                }
            }
        }
    }
    if (!itd->dir_in) {
        snprintf(why, n, "direction not recorded as IN");
        goto fail;
    }
    TEST_KILL(urb);
    urb_free(urb);

    /* the high-bandwidth multiplier rides the same field the port's queue
     * heads use: descriptor mult bits + 1 transaction count */
    controller_reset(600);
    ep_default(0x81, 0x0b20, 1); /* mult bits = 1 -> two transactions per micro-frame */
    urb = urb_new(1, 64);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed (mult)");
        return false;
    }
    if (TEST_SUBMIT(urb) != 0) {
        snprintf(why, n, "submit failed (mult)");
        urb_free(urb);
        return false;
    }
    itd = linked_itd(urb->start_frame);
    if (itd == NULL || (itd->hw.bpl[2] & ITD_BUFPTR2_MULTI_MASK) != 2) {
        snprintf(why, n, "multiplier = %u, expected 2", itd ? (itd->hw.bpl[2] & ITD_BUFPTR2_MULTI_MASK) : 0);
        if (itd != NULL) {
            TEST_KILL(urb);
        }
        urb_free(urb);
        return false;
    }
    TEST_KILL(urb);
    urb_free(urb);
    return true;

fail:
    TEST_KILL(urb);
    urb_free(urb);
    return false;
}

/* Frame placement: consecutive descriptors on consecutive frame entries, each
 * linked in front of the periodic chain, and the frame numbering wraps. */
static bool case_frame_link(char *why, size_t n)
{
    struct usbh_urb *urb;
    struct ehci_itd_hw *itd0;
    struct ehci_itd_hw *itd1;
    uint16_t frame;
    uint32_t head = QH_HLP_QH(&g_periodic_qh_head[TEST_BUS]);

    controller_reset(1000); /* start = 1000 + lead */
    ep_default(0x81, 0x0180, 1);
    urb = urb_new(10, 64);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed");
        return false;
    }
    if (TEST_SUBMIT(urb) != 0) {
        snprintf(why, n, "submit failed");
        urb_free(urb);
        return false;
    }
    frame = urb->start_frame;
    if (frame != (uint16_t)((1000 + 2) % CONFIG_USB_EHCI_FRAME_LIST_SIZE)) {
        snprintf(why, n, "start_frame %u, expected %u", frame, (1000 + 2) % CONFIG_USB_EHCI_FRAME_LIST_SIZE);
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    itd0 = linked_itd(frame);
    itd1 = linked_itd((uint16_t)(frame + 1));
    if (itd0 == NULL || itd1 == NULL || itd0 == itd1) {
        snprintf(why, n, "ten packets did not land as two consecutive descriptors");
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    if (itd0->mf_valid != 8 || itd1->mf_valid != 2) {
        snprintf(why, n, "packet split %u/%u, expected 8/2", itd0->mf_valid, itd1->mf_valid);
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    if (itd1->pkt_idx[host_slot_of(itd1->mf_unmask, 0)] != 8) {
        snprintf(why, n, "second descriptor starts at packet %u, expected 8", itd1->pkt_idx[host_slot_of(itd1->mf_unmask, 0)]);
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    /* each descriptor sits in front of the untouched periodic chain */
    if (itd0->hw.nlp != head || itd1->hw.nlp != head) {
        snprintf(why, n, "descriptor nlp 0x%08x/0x%08x does not continue into the queue head 0x%08x",
                 itd0->hw.nlp, itd1->hw.nlp, head);
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    TEST_KILL(urb);
    urb_free(urb);

    /* frame numbering wraps through the frame list boundary */
    controller_reset((uint16_t)(CONFIG_USB_EHCI_FRAME_LIST_SIZE - 2)); /* start lands at the last entry */
    urb = urb_new(10, 64);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed (wrap)");
        return false;
    }
    if (TEST_SUBMIT(urb) != 0) {
        snprintf(why, n, "submit failed (wrap)");
        urb_free(urb);
        return false;
    }
    itd0 = linked_itd(urb->start_frame);
    itd1 = linked_itd((uint16_t)((urb->start_frame + 1) % CONFIG_USB_EHCI_FRAME_LIST_SIZE));
    if (itd0 == NULL || itd1 == NULL) {
        snprintf(why, n, "wrap: descriptors not linked across the boundary (start %u)", urb->start_frame);
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    TEST_KILL(urb);
    urb_free(urb);
    return true;
}

/* Linking and unlinking restore the frame entry exactly; a second kill is a
 * no-op; the completion path restores it too. */
static bool case_link_unlink(char *why, size_t n)
{
    struct usbh_urb *urb;
    struct ehci_itd_hw *itd;
    uint16_t frame;
    uint32_t before;
    uint32_t head = QH_HLP_QH(&g_periodic_qh_head[TEST_BUS]);

    controller_reset(200);
    ep_default(0x81, 0x0180, 1);
    urb = urb_new(4, 64);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed");
        return false;
    }
    frame = (uint16_t)((200 + 2) % CONFIG_USB_EHCI_FRAME_LIST_SIZE);
    before = g_framelist[TEST_BUS][frame];
    if (before != head) {
        snprintf(why, n, "entry was not the untouched periodic chain before the submit");
        urb_free(urb);
        return false;
    }
    if (TEST_SUBMIT(urb) != 0) {
        snprintf(why, n, "submit failed");
        urb_free(urb);
        return false;
    }
    itd = linked_itd(frame);
    if (itd == NULL) {
        snprintf(why, n, "no descriptor linked");
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    TEST_KILL(urb);
    if (g_framelist[TEST_BUS][frame] != head) {
        snprintf(why, n, "kill left the entry at 0x%08x, expected 0x%08x", g_framelist[TEST_BUS][frame], head);
        urb_free(urb);
        return false;
    }
    if (urb->errorcode != -USB_ERR_SHUTDOWN || urb->hcpriv != NULL) {
        snprintf(why, n, "kill did not mark the urb (errorcode %d, hcpriv %p)", urb->errorcode, urb->hcpriv);
        urb_free(urb);
        return false;
    }
    /* a second kill finds nothing booked and stays quiet */
    if (TEST_KILL(urb) == 0) {
        snprintf(why, n, "second kill reported success on a detached urb");
        urb_free(urb);
        return false;
    }
    urb_free(urb);

    /* and the completion path restores the entry as well */
    urb = urb_new(4, 64);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed (complete)");
        return false;
    }
    if (TEST_SUBMIT(urb) != 0) {
        snprintf(why, n, "submit failed (complete)");
        urb_free(urb);
        return false;
    }
    itd = linked_itd(urb->start_frame);
    if (itd == NULL) {
        snprintf(why, n, "no descriptor linked (complete)");
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    for (uint8_t p = 0; p < 4; p++) {
        host_retire(itd, p, 64, 0);
    }
    ehci_scan_isochronous_list(&g_bus);
    if (g_framelist[TEST_BUS][urb->start_frame] != head) {
        snprintf(why, n, "completion left the entry at 0x%08x, expected 0x%08x", g_framelist[TEST_BUS][urb->start_frame], head);
        urb_free(urb);
        return false;
    }
    if (urb->errorcode != 0) {
        snprintf(why, n, "completion reported errorcode %d", urb->errorcode);
        urb_free(urb);
        return false;
    }
    urb_free(urb);
    return true;
}

/* With every slot booked the engine refuses politely, and a completed
 * transfer hands its slot back. */
static bool case_slot_exhaustion(char *why, size_t n)
{
    struct usbh_urb *urb[CONFIG_USB_EHCI_ISO_NUM + 1];
    struct ehci_itd_hw *itd;

    controller_reset(300);
    ep_default(0x81, 0x0180, 1);
    for (uint32_t i = 0; i < CONFIG_USB_EHCI_ISO_NUM; i++) {
        urb[i] = urb_new(1, 64);
        if (urb[i] == NULL || TEST_SUBMIT(urb[i]) != 0) {
            snprintf(why, n, "submit %u of %u failed", i, CONFIG_USB_EHCI_ISO_NUM);
            return false;
        }
    }
    urb[CONFIG_USB_EHCI_ISO_NUM] = urb_new(1, 64);
    if (urb[CONFIG_USB_EHCI_ISO_NUM] == NULL) {
        snprintf(why, n, "alloc failed (overflow)");
        return false;
    }
    if (TEST_SUBMIT(urb[CONFIG_USB_EHCI_ISO_NUM]) != -USB_ERR_NOMEM) {
        snprintf(why, n, "the %u-th in-flight transfer was not refused with NOMEM", CONFIG_USB_EHCI_ISO_NUM + 1);
        return false;
    }
    urb_free(urb[CONFIG_USB_EHCI_ISO_NUM]);

    itd = linked_itd(urb[0]->start_frame);
    if (itd == NULL) {
        snprintf(why, n, "no descriptor for the first transfer");
        return false;
    }
    host_retire(itd, 0, 64, 0);
    ehci_scan_isochronous_list(&g_bus);

    urb[CONFIG_USB_EHCI_ISO_NUM] = urb_new(1, 64);
    if (urb[CONFIG_USB_EHCI_ISO_NUM] == NULL || TEST_SUBMIT(urb[CONFIG_USB_EHCI_ISO_NUM]) != 0) {
        snprintf(why, n, "the freed slot was not reusable");
        return false;
    }
    for (uint32_t i = 1; i <= CONFIG_USB_EHCI_ISO_NUM; i++) {
        TEST_KILL(urb[i]);
    }
    for (uint32_t i = 0; i <= CONFIG_USB_EHCI_ISO_NUM; i++) {
        urb_free(urb[i]);
    }
    return true;
}

/* Result mapping: nothing completes until every descriptor has retired, and
 * then the per-packet lengths and error codes land in the urb. */
static bool case_completion_map(char *why, size_t n)
{
    struct usbh_urb *urb;
    struct ehci_itd_hw *itd;
    struct completion done = { 0, NULL, 0 };

    controller_reset(400);
    ep_default(0x81, 0x0180, 1);
    urb = urb_new(5, 64);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed");
        return false;
    }
    urb->complete = completion_cb;
    urb->arg = &done;
    if (TEST_SUBMIT(urb) != 0) {
        snprintf(why, n, "submit failed");
        urb_free(urb);
        return false;
    }
    itd = linked_itd(urb->start_frame);
    if (itd == NULL) {
        snprintf(why, n, "no descriptor linked");
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }

    /* one slot retired is not a completed transfer */
    host_retire(itd, 0, 512, 0);
    ehci_scan_isochronous_list(&g_bus);
    if (done.calls != 0 || urb->errorcode != -USB_ERR_BUSY) {
        snprintf(why, n, "a partially retired transfer completed early (calls %d, errorcode %d)", done.calls, urb->errorcode);
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }

    host_retire(itd, 1, 0, ITD_TSCL_STATUS_XACTERR);
    host_retire(itd, 2, 0, ITD_TSCL_STATUS_BABBLE);
    host_retire(itd, 3, 0, ITD_TSCL_STATUS_DBERROR);
    host_retire(itd, 4, 300, 0);
    ehci_scan_isochronous_list(&g_bus);

    if (done.calls != 1) {
        snprintf(why, n, "completion callback ran %d times, expected 1", done.calls);
        urb_free(urb);
        return false;
    }
    if (urb->errorcode != -USB_ERR_IO) {
        snprintf(why, n, "urb errorcode %d, expected the first packet error (%d)", urb->errorcode, -USB_ERR_IO);
        urb_free(urb);
        return false;
    }
    if (urb->actual_length != 512 + 300) {
        snprintf(why, n, "urb actual length %u, expected %u", urb->actual_length, 512 + 300);
        urb_free(urb);
        return false;
    }
    if (urb->iso_packet[0].actual_length != 512 || urb->iso_packet[0].errorcode != 0 ||
        urb->iso_packet[1].errorcode != -USB_ERR_IO ||
        urb->iso_packet[2].errorcode != -USB_ERR_BABBLE ||
        urb->iso_packet[3].errorcode != -USB_ERR_IO ||
        urb->iso_packet[4].actual_length != 300 || urb->iso_packet[4].errorcode != 0) {
        snprintf(why, n, "per-packet results: len %u/%u, codes %d/%d/%d/%d/%d",
                 urb->iso_packet[0].actual_length, urb->iso_packet[4].actual_length,
                 urb->iso_packet[0].errorcode, urb->iso_packet[1].errorcode, urb->iso_packet[2].errorcode,
                 urb->iso_packet[3].errorcode, urb->iso_packet[4].errorcode);
        urb_free(urb);
        return false;
    }
    if (urb->hcpriv != NULL) {
        snprintf(why, n, "hcpriv still set after completion");
        urb_free(urb);
        return false;
    }
    urb_free(urb);
    return true;
}

/* The synchronous submit: the interrupt path's semaphore releases the waiter,
 * and an expiry detaches the transfer and hands the slot back. */
struct sync_worker {
    struct usbh_urb *urb;
    struct usbh_bus *bus;
};

static void *sync_worker_fn(void *p)
{
    struct sync_worker *w = p;
    struct ehci_itd_hw *itd;

    usleep(20000);
    itd = linked_itd(w->urb->start_frame);
    if (itd != NULL) {
        for (uint8_t i = 0; i < itd->mf_valid; i++) {
            host_retire(itd, i, w->urb->iso_packet[i].transfer_buffer_length, 0);
        }
        ehci_scan_isochronous_list(w->bus);
    }
    return NULL;
}

static bool case_sync_wait(char *why, size_t n)
{
    struct usbh_urb *urb;
    struct sync_worker w;
    pthread_t th;
    int ret;

    controller_reset(700);
    ep_default(0x81, 0x0180, 1);
    urb = urb_new(4, 96);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed");
        return false;
    }
    urb->timeout = 500;
    w.urb = urb;
    w.bus = &g_bus;
    if (pthread_create(&th, NULL, sync_worker_fn, &w) != 0) {
        snprintf(why, n, "worker thread did not start");
        urb_free(urb);
        return false;
    }
    ret = TEST_SUBMIT(urb);
    pthread_join(th, NULL);

    if (ret != 0 || urb->errorcode != 0) {
        snprintf(why, n, "synchronous submit returned %d (errorcode %d, expected success)", ret, urb->errorcode);
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    if (urb->actual_length != 4 * 96) {
        snprintf(why, n, "synchronous transfer length %u, expected %u", urb->actual_length, 4 * 96);
        urb_free(urb);
        return false;
    }
    if (urb->hcpriv != NULL || urb->timeout != 0) {
        snprintf(why, n, "synchronous completion left hcpriv %p / timeout %u", urb->hcpriv, urb->timeout);
        urb_free(urb);
        return false;
    }
    urb_free(urb);

    /* expiry: the transfer is detached, the slot comes back, the token a late
     * completion might hand over is dropped by the next submit's drain */
    controller_reset(800);
    urb = urb_new(2, 64);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed (expiry)");
        return false;
    }
    urb->timeout = 40;
    ret = TEST_SUBMIT(urb);
    if (ret != -USB_ERR_TIMEOUT) {
        snprintf(why, n, "expiring submit returned %d, expected timeout", ret);
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    if (urb->hcpriv != NULL || urb->errorcode != -USB_ERR_TIMEOUT) {
        snprintf(why, n, "expiry left hcpriv %p / errorcode %d", urb->hcpriv, urb->errorcode);
        urb_free(urb);
        return false;
    }
    if (entry_is_itd(g_framelist[TEST_BUS][urb->start_frame])) {
        snprintf(why, n, "expiry left the descriptor linked");
        urb_free(urb);
        return false;
    }
    urb_free(urb);
    return true;
}

/* The rejection matrix: unsupported periods and speeds, malformed packet
 * lists, and the buffer-window bound; nothing is linked, no slot is leaked. */
static bool case_reject_matrix(char *why, size_t n)
{
    struct usbh_urb *urb;
    uint32_t head = QH_HLP_QH(&g_periodic_qh_head[TEST_BUS]);

    /* a period above one frame is out of this engine's scope */
    controller_reset(100);
    ep_default(0x81, 0x0180, 5);
    urb = urb_new(1, 64);
    if (urb == NULL || TEST_SUBMIT(urb) != -USB_ERR_NOTSUPP) {
        snprintf(why, n, "bInterval 5 was not rejected as unsupported");
        return false;
    }
    urb_free(urb);

    /* full-speed isochronous needs the split-transaction descriptor */
    controller_reset(100);
    ep_default(0x81, 0x0180, 1);
    g_hport.speed = USB_SPEED_FULL;
    urb = urb_new(1, 64);
    if (urb == NULL || TEST_SUBMIT(urb) != -USB_ERR_NOTSUPP) {
        snprintf(why, n, "a full-speed endpoint was not rejected as unsupported");
        return false;
    }
    urb_free(urb);

    /* zero packets, more packets than the descriptors hold, oversized packets */
    g_hport.speed = USB_SPEED_HIGH;
    controller_reset(100);
    urb = urb_new(0, 64);
    if (urb == NULL || TEST_SUBMIT(urb) != -USB_ERR_INVAL) {
        snprintf(why, n, "a zero-packet transfer was not rejected");
        return false;
    }
    urb_free(urb);
    urb = urb_new((uint32_t)CONFIG_USB_EHCI_ITD_NUM * 8 + 1, 64);
    if (urb == NULL || TEST_SUBMIT(urb) != -USB_ERR_INVAL) {
        snprintf(why, n, "a transfer past the descriptor count was not rejected");
        return false;
    }
    urb_free(urb);
    urb = urb_new(1, 0x1000);
    if (urb == NULL || TEST_SUBMIT(urb) != -USB_ERR_INVAL) {
        snprintf(why, n, "a packet past the TSCL length field was not rejected");
        return false;
    }
    urb_free(urb);

    /* a packet that would leave the seven-page buffer window */
    urb = urb_new(2, 512);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed (window)");
        return false;
    }
    urb->iso_packet[0].transfer_buffer = g_arena + 0x100;
    urb->iso_packet[1].transfer_buffer = g_arena + 0x6e40;
    if (TEST_SUBMIT(urb) != -USB_ERR_INVAL) {
        snprintf(why, n, "a packet outside the buffer page window was not rejected");
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    urb_free(urb);

    if (g_framelist[TEST_BUS][102 % CONFIG_USB_EHCI_FRAME_LIST_SIZE] != head) {
        snprintf(why, n, "a rejected submit touched the frame list");
        return false;
    }

    /* and the engine still works after the rejections */
    urb = urb_new(1, 64);
    if (urb == NULL || TEST_SUBMIT(urb) != 0) {
        snprintf(why, n, "a valid transfer failed after the rejections");
        return false;
    }
    TEST_KILL(urb);
    urb_free(urb);
    return true;
}

/* OUT direction: no DIR bit, and the harvested length is what was submitted. */
static bool case_direction_out(char *why, size_t n)
{
    struct usbh_urb *urb;
    struct ehci_itd_hw *itd;

    controller_reset(900);
    ep_default(0x01, 0x0180, 1); /* endpoint 1 OUT */
    urb = urb_new(2, 128);
    if (urb == NULL) {
        snprintf(why, n, "alloc failed");
        return false;
    }
    if (TEST_SUBMIT(urb) != 0) {
        snprintf(why, n, "submit failed");
        urb_free(urb);
        return false;
    }
    itd = linked_itd(urb->start_frame);
    if (itd == NULL) {
        snprintf(why, n, "no descriptor linked");
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    if (itd->dir_in || (itd->hw.bpl[1] & ITD_BUFPTR1_DIRIN) != 0) {
        snprintf(why, n, "OUT endpoint recorded as IN");
        TEST_KILL(urb);
        urb_free(urb);
        return false;
    }
    host_retire(itd, 0, 128, 0);
    host_retire(itd, 1, 128, 0);
    ehci_scan_isochronous_list(&g_bus);
    if (urb->actual_length != 2 * 128 || urb->iso_packet[0].actual_length != 128) {
        snprintf(why, n, "OUT completion reported %u/%u, expected submitted lengths",
                 urb->actual_length, urb->iso_packet[0].actual_length);
        urb_free(urb);
        return false;
    }
    urb_free(urb);
    return true;
}

/* ------------------------------------------------------------------------
 * runner
 * --------------------------------------------------------------------- */

int main(void)
{
    static const struct {
        const char *name;
        bool (*fn)(char *, size_t);
    } cases[] = {
        { "period-mask", case_period_mask },
        { "tscl-encode", case_tscl_encode },
        { "frame-link", case_frame_link },
        { "link-unlink", case_link_unlink },
        { "slot-exhaustion", case_slot_exhaustion },
        { "completion-map", case_completion_map },
        { "sync-wait", case_sync_wait },
        { "reject-matrix", case_reject_matrix },
        { "direction-out", case_direction_out },
    };
    int failed = 0;

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char why[256] = "";

        if (cases[i].fn(why, sizeof(why))) {
            printf("EHCIISO: %s PASS\n", cases[i].name);
        } else {
            printf("EHCIISO: %s FAIL (%s)\n", cases[i].name, why);
            failed++;
        }
    }
    printf("EHCIISO: summary cases=%u failed=%d\n", (unsigned)(sizeof(cases) / sizeof(cases[0])), failed);
    return failed == 0 ? 0 : 1;
}
