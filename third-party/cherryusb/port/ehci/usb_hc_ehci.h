/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _USB_EHCI_PRIV_H
#define _USB_EHCI_PRIV_H

#include "usbh_core.h"
#include "usbh_hub.h"
#include "usb_ehci_reg.h"

#define EHCI_HCCR ((struct ehci_hccr *)(uintptr_t)(bus->hcd.reg_base + CONFIG_USB_EHCI_HCCR_OFFSET))
#define EHCI_HCOR ((struct ehci_hcor *)(uintptr_t)(bus->hcd.reg_base + CONFIG_USB_EHCI_HCCR_OFFSET + g_ehci_hcd[bus->hcd.hcd_id].hcor_offset))

#define EHCI_PTR2ADDR(x) ((uint32_t)(uintptr_t)(x) & ~0x1F)
#define EHCI_ADDR2QH(x)  ((struct ehci_qh_hw *)(uintptr_t)((uint32_t)(x) & ~0x1F))
#define EHCI_ADDR2QTD(x) ((struct ehci_qtd_hw *)(uintptr_t)((uint32_t)(x) & ~0x1F))
#define EHCI_ADDR2ITD(x) ((struct ehci_itd_hw *)(uintptr_t)((uint32_t)(x) & ~0x1F))

#ifndef CONFIG_USB_EHCI_QH_NUM
#define CONFIG_USB_EHCI_QH_NUM 10
#endif
#ifndef CONFIG_USB_EHCI_QTD_NUM
#define CONFIG_USB_EHCI_QTD_NUM (CONFIG_USB_EHCI_QH_NUM * 3)
#endif
#ifndef CONFIG_USB_EHCI_ITD_NUM
#define CONFIG_USB_EHCI_ITD_NUM 5
#endif
#ifndef CONFIG_USB_EHCI_ISO_NUM
#define CONFIG_USB_EHCI_ISO_NUM 4
#endif

#if CONFIG_USB_ALIGN_SIZE <= 32
#define CONFIG_USB_EHCI_ALIGN_SIZE 32
#elif CONFIG_USB_ALIGN_SIZE <= 64
#define CONFIG_USB_EHCI_ALIGN_SIZE 64
#else
#error "CONFIG_USB_ALIGN_SIZE must be 32 or 64"
#endif

#if CONFIG_USB_EHCI_QTD_NUM < 9
#error CONFIG_USB_EHCI_QTD_NUM is too small, recommand CONFIG_USB_EHCI_QH_NUM * 3
#endif

struct ehci_qtd_hw {
    struct ehci_qtd hw;
#if defined(CONFIG_USB_EHCI_DESC_DCACHE_ENABLE) && (CONFIG_USB_ALIGN_SIZE == 64)
    uint8_t pad[32];
#endif
    bool inuse;
    struct usbh_urb *urb;
    uintptr_t bufaddr;
    uint32_t length;
} __attribute__((aligned(CONFIG_USB_EHCI_ALIGN_SIZE)));

struct ehci_qh_hw {
    struct ehci_qh hw;
#if defined(CONFIG_USB_EHCI_DESC_DCACHE_ENABLE)
    uint16_t pad[16];
#endif
    bool inuse;
    uint32_t first_qtd;
    struct usbh_urb *urb;
    usb_osal_sem_t waitsem;
    uint8_t remove_in_iaad;
    volatile uint8_t killed;
} __attribute__((aligned(CONFIG_USB_EHCI_ALIGN_SIZE)));

/*
 * One row of the asynchronous schedule, for a board that shows "one transfer
 * never completes and everything behind it waits".  `token` is the transfer
 * overlay's token word: bit 7 still set means the controller has not retired
 * that qTD, i.e. the endpoint answered NAK (or nothing) and the transfer is
 * with the device, not with this port.  A row whose `active` is clear but which
 * is still linked says the opposite: the descriptor finished and this port
 * failed to retire it.
 */
struct usbh_ehci_qh_row {
    uint32_t epchar;
    uint32_t curr_qtd;
    uint32_t token;
    uint32_t qtd_next;
    uint32_t hlp;
    void *urb;
    uint8_t active;
    uint8_t iaad;
    uint8_t killed;
    uint8_t inuse;
};

#define USBH_EHCI_SNAPSHOT_ROWS 16

struct usbh_ehci_snapshot {
    uint32_t usbcmd;
    uint32_t usbsts;
    uint32_t usbintr;
    uint32_t frindex;
    uint32_t listaddr;
    unsigned int rows;
    struct usbh_ehci_qh_row row[USBH_EHCI_SNAPSHOT_ROWS];
};

void usbh_ehci_async_snapshot(uint8_t busid, struct usbh_ehci_snapshot *out);
void usbh_ehci_async_report(uint8_t busid);
int usbh_ehci_async_rearm(uint8_t busid);

struct ehci_itd_hw {
    struct ehci_itd hw;
    struct usbh_urb *urb;
    uint16_t start_frame;
    uint8_t mf_unmask;
    uint8_t mf_valid;
    uint32_t pkt_idx[8];
    bool dir_in;
} __attribute__((aligned(CONFIG_USB_EHCI_ALIGN_SIZE)));

struct ehci_iso_hw {
    struct ehci_itd_hw itd_pool[CONFIG_USB_EHCI_ITD_NUM];
    uint32_t itd_num;
};

struct ehci_hcd {
    bool ehci_iso_used[CONFIG_USB_EHCI_ISO_NUM];
    uint16_t iso_frame_next; /* first unreserved frame of the isochronous tiling */
    uint16_t iso_frame_base; /* the free frame the reservation was taken at */
    bool ppc;      /* Port Power Control */
    bool has_tt;   /* if use tt instead of Companion Controller */
    uint8_t n_cc;  /* Number of Companion Controller */
    uint8_t n_pcc; /* Number of ports supported per companion host controller */
    uint8_t n_ports;
    uint8_t hcor_offset;
};

extern struct ehci_hcd g_ehci_hcd[CONFIG_USBHOST_MAX_BUS];
extern uint32_t g_framelist[CONFIG_USBHOST_MAX_BUS][USB_ALIGN_UP(CONFIG_USB_EHCI_FRAME_LIST_SIZE, 1024)];
extern uint8_t usbh_get_port_speed(struct usbh_bus *bus, const uint8_t port);

int ehci_iso_urb_init(struct usbh_bus *bus, struct usbh_urb *urb);
void ehci_kill_iso_urb(struct usbh_bus *bus, struct usbh_urb *urb);
void ehci_scan_isochronous_list(struct usbh_bus *bus);

#endif
