/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the production adapter with a controller that checks USB packet
 * sequence state. Fresh transfer objects must retain endpoint state.
 * Reference: USB 2.0 sections 8.6 and 9.4.5; CherryUSB EHCI's URB toggle
 * input/output contract (port/ehci/usb_hc_ehci.c:512,724).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "components/linuxkpi/usb/lkpi_usb_backend.h"
#include "ports/cherryusb/lkpi_usb_cherryusb.h"
#undef DIV_ROUND_CLOSEST
#include "usbh_core.h"
#include <linux/usb.h>
#include "os/cmsis2/cmsis2.h"

void lkpi_usb_cherryusb_init(void);
static unsigned int expected[256], next_toggle;
static int reject, mismatch, callbacks, control_error, completion_error, fault;

int usbh_submit_urb(struct usbh_urb *urb)
{
    unsigned int address = urb->ep->bEndpointAddress;
    if (urb->data_toggle != expected[address]) {
        printf("ADAPTER FAIL ep=%02x expected DATA%u got DATA%u\n",
            address, expected[address], urb->data_toggle);
        mismatch++;
    }
    if (reject)
        return -LKPI_USB_BE_ERR_BUSY;
    expected[address] = next_toggle;
    urb->data_toggle = next_toggle;
    urb->actual_length = urb->transfer_buffer_length;
    urb->errorcode = completion_error;
    if (urb->complete)
        urb->complete(urb->arg, completion_error ? completion_error : (int)urb->actual_length);
    return completion_error && urb->timeout ? completion_error : 0;
}

int usbh_kill_urb(struct usbh_urb *urb)
{
    (void)urb;
    return -LKPI_USB_BE_ERR_INVAL;
}

int usbh_control_transfer(struct usbh_hubport *port,
    struct usb_setup_packet *setup, uint8_t *buffer)
{
    (void)port;
    (void)buffer;
    if (setup->bmRequestType != 2 || setup->bRequest != 1 ||
        setup->wValue != 0 || setup->wLength != 0)
        mismatch++;
    if (!control_error)
        expected[setup->wIndex] = 0;
    return control_error;
}

static void completed(void *arg, int nbytes)
{
    (void)arg;
    if (nbytes == 64)
        callbacks++;
}

static void run(void *arg)
{
    struct usbh_hubport native_port = {0};
    struct lkpi_usb_be_hubport port = {.hcpriv = &native_port};
    struct usb_endpoint_descriptor desc[3] = {
        {.bEndpointAddress = 0x01, .bmAttributes = 2, .wMaxPacketSize = 512},
        {.bEndpointAddress = 0x81, .bmAttributes = 2, .wMaxPacketSize = 512},
        {.bEndpointAddress = 0x02, .bmAttributes = 3, .wMaxPacketSize = 64}
    };
    struct lkpi_usb_be_ep eps[3] = {
        {.hcpriv = &desc[0]}, {.hcpriv = &desc[1]}, {.hcpriv = &desc[2]}
    };
    unsigned char data[64] = {0};
    unsigned int i;
    unsigned int stage_sz = 0;
    (void)arg;
    lkpi_usb_cherryusb_init();
    /* Odd/even packet results are supplied by the HCD, never guessed from
     * buffer size. Mix sync/async and endpoints, recreating the URB each time. */
    for (i = 0; i < 24; i++) {
        unsigned int ep = i % 3;
        struct lkpi_usb_be_urb urb = {
            .hport = &port, .ep = &eps[ep], .transfer_buffer = data,
            .transfer_buffer_length = sizeof(data),
            .timeout = (i % 2) ? 100 : 0, .complete = completed
        };
        next_toggle = ((i / 3) % 3) != 2;
        if (fault)
            eps[ep].data_toggle = 0;
        if (lkpi_usb_be_submit_urb(&urb) != 0 || urb.actual_length != 64)
            exit(2);
    }
    reject = 1;
    for (i = 0; i < 3; i++) {
        struct lkpi_usb_be_urb urb = {
            .hport = &port, .ep = &eps[0], .transfer_buffer = data,
            .transfer_buffer_length = sizeof(data)
        };
        if (lkpi_usb_be_submit_urb(&urb) != -LKPI_USB_BE_ERR_BUSY)
            exit(2);
    }
    reject = 0;
    /* Error completions still publish the HCD's resulting state. */
    completion_error = -LKPI_USB_BE_ERR_IO;
    for (i = 0; i < 2; i++) {
        struct lkpi_usb_be_urb urb = {
            .hport = &port, .ep = &eps[0], .transfer_buffer = data,
            .transfer_buffer_length = sizeof(data), .timeout = i ? 100 : 0,
            .complete = completed
        };
        next_toggle = i ? 0 : 1;
        if (lkpi_usb_be_submit_urb(&urb) != 0 || urb.errorcode != completion_error)
            exit(2);
    }
    completion_error = 0;
    /* Successful clear_halt resets exactly the selected address/direction.
     * A failed request preserves state. Use the real Linux API and engine. */
    {
        struct usb_host_endpoint hep[3] = {0};
        struct usb_device dev = {0};
        struct usb_host_interface alt = {.endpoint = hep, .num_endpoints = 3};
        struct usb_interface intf = {.cur_altsetting = &alt};
        for (i = 0; i < 3; i++) {
            hep[i].desc = desc[i];
            hep[i].hcpriv = &eps[i];
        }
        dev._hport = &port;
        dev._intf = &intf;
        for (i = 0; i < 3; i++)
            expected[desc[i].bEndpointAddress] = eps[i].data_toggle = 1;
        if (usb_clear_halt(&dev, usb_sndbulkpipe(&dev, 1)) != 0 ||
            eps[0].data_toggle != 0 || eps[1].data_toggle != 1 ||
            eps[2].data_toggle != 1)
            mismatch++;
        control_error = -LKPI_USB_BE_ERR_STALL;
        if (usb_clear_halt(&dev, usb_rcvbulkpipe(&dev, 1)) != -EPIPE ||
            eps[1].data_toggle != 1)
            mismatch++;
        control_error = 0;
        if (usb_clear_halt(&dev, usb_rcvbulkpipe(&dev, 1)) != 0 ||
            eps[1].data_toggle != 0 || eps[2].data_toggle != 1)
            mismatch++;
    }
    /*
     * The transfer stage's boundary. A request that fits one stage slot is
     * staged and served; one byte more is refused for good (the engine would
     * park and retry it forever, so the driver must never see a length the image
     * cannot stage). The image's slot size is what makes this safe per driver -
     * check-usb-stage-size.sh compares it with the driver's widest receive
     * transfer at build time, and this case pins the adapter's side of that
     * contract: the published geometry is the compiled one, and the refusal is
     * the named counter, not a silent drop.
     */
    {
        struct lkpi_usb_chusb_stats before, after;
        unsigned char *big;
        unsigned int sz;

        lkpi_usb_cherryusb_stats(&before);
        sz = before.stage_size;
        stage_sz = sz;
        if (sz == 0 || before.stage_slots == 0) {
            printf("ADAPTER FAIL stage geometry not published\n");
            mismatch++;
        }
        big = malloc(sz + 1);
        if (big == NULL)
            exit(2);
        memset(big, 0, sz + 1);

        expected[0x01] = eps[0].data_toggle = 0;
        next_toggle = 0;
        {
            struct lkpi_usb_be_urb urb = {
                .hport = &port, .ep = &eps[0], .transfer_buffer = big,
                .transfer_buffer_length = sz, .timeout = 100
            };
            if (lkpi_usb_be_submit_urb(&urb) != 0) {
                printf("ADAPTER FAIL a transfer of exactly one stage slot was refused\n");
                mismatch++;
            }
        }
        lkpi_usb_cherryusb_stats(&after);
        if (after.stage_overflow != before.stage_overflow) {
            printf("ADAPTER FAIL a fitting transfer counted as an overflow\n");
            mismatch++;
        }

        expected[0x01] = eps[0].data_toggle = 0;
        next_toggle = 0;
        {
            struct lkpi_usb_be_urb urb = {
                .hport = &port, .ep = &eps[0], .transfer_buffer = big,
                .transfer_buffer_length = sz + 1, .timeout = 100
            };
            if (lkpi_usb_be_submit_urb(&urb) != -LKPI_USB_BE_ERR_NOMEM) {
                printf("ADAPTER FAIL an oversized transfer was not refused\n");
                mismatch++;
            }
        }
        lkpi_usb_cherryusb_stats(&after);
        if (after.stage_overflow != before.stage_overflow + 1) {
            printf("ADAPTER FAIL the oversized transfer did not count as overflow\n");
            mismatch++;
        }
        free(big);
    }
    printf("ADAPTER %s transfers=24 callbacks=%d rejected=3 stage=%u\n",
        mismatch || callbacks != 12 ? "FAIL" : "PASS", callbacks, stage_sz);
    exit(mismatch || callbacks != 12);
}

int main(int argc, char **argv)
{
    osThreadAttr_t attr = {.name = "adapter-case", .stack_size = 16384};
    fault = argc == 2 && strcmp(argv[1], "--fault-data0") == 0;
    if (argc > 1 && !fault)
        return 2;
    osKernelInitialize();
    osThreadNew(run, NULL, &attr);
    osKernelStart();
    return 2;
}
