/*
 * @file   usb_hc_xhci.h
 * @brief  xHCI register/TRB/context definitions for the DWC3 xHCI port,
 *         plus the per-bus driver state the platform layer and the
 *         diagnostics hooks reach.
 *
 * MMIO layout matches the xHCI spec / NetBSD xhcireg.h (BSD-2); the DWC3
 * globals follow the Synopsys DWC3 reference.  The context field layout
 * is the u-boot/Linux/NetBSD one - this DWC3 rejects the xHCI 1.1
 * alternative (board-measured 2026-08-12).  The TRB type and
 * completion-code values are the corrected set: the first attempt at
 * this port carried transfer-segment TRB types off by one and lost
 * completion codes by the same slip (board evidence 2026-09-20).
 *
 * Provenance: the register sequence is NetBSD netbsd-11 xhci.c (rev
 * 1.188.2.4, BSD-2) as board-proven on this SoC, crossed with the
 * vendored port/ehci/usb_hc_ehci.c async URB model: IRQ-driven event
 * processing, one in-flight URB per endpoint with a per-ring waitsem
 * for timeout'd submitters, completion callbacks from the interrupt
 * path.  The driver's lineage and every change against its previous
 * revision are recorded in the patch's commit message.
 *
 * @author zhugengyu
 * @date   27.09.2026
 */
#ifndef USB_HC_XHCI_H
#define USB_HC_XHCI_H

#include <stdint.h>
#include <stdbool.h>
#include "usbh_core.h"

/* ====================== capability registers ====================== */
#define XHCI_CAPLENGTH		0x00U
#define XHCI_HCIVERSION		0x02U
#define XHCI_HCSPARAMS1		0x04U
#define XHCI_HCSPARAMS2		0x08U
#define XHCI_HCSPARAMS3		0x0CU
#define XHCI_HCCPARAMS1		0x10U
#define XHCI_DBOFF		0x14U
#define XHCI_RTSOFF		0x18U

#define XHCI_HCS1_DEVSLOT_MAX(x)	((x) & 0xFFU)
#define XHCI_HCS1_MAX_INTRS(x)		(((x) >> 8) & 0x7FFU)
#define XHCI_HCS1_N_PORTS(x)		(((x) >> 24) & 0xFFU)
#define XHCI_HCS2_ERST_MAX(x)		(((x) >> 4) & 0xFU)
#define XHCI_HCS2_SPB_MAX(x)		(((((x) >> 16) & 0x3E0U)) | \
					 (((x) >> 27) & 0x1FU))
#define XHCI_HCCPARAMS1_CSZ(x)		(((x) >> 2) & 0x1U)

/* ====================== operational registers ====================== */
#define XHCI_USBCMD		0x00U
#define XHCI_CMD_RS		0x00000001U
#define XHCI_CMD_HCRST		0x00000002U
#define XHCI_CMD_INTE		0x00000004U
#define XHCI_CMD_HSEE		0x00000008U

#define XHCI_USBSTS		0x04U
#define XHCI_PAGESIZE		0x08U
#define XHCI_DNCTRL		0x14U
#define XHCI_STS_HCH		0x00000001U
#define XHCI_STS_HSE		0x00000004U
#define XHCI_STS_EINT		0x00000008U
#define XHCI_STS_PCD		0x00000010U
#define XHCI_STS_CNR		0x00000800U
#define XHCI_STS_HCE		0x00001000U
#define XHCI_STS_RSVDP0		0xFFFFE000U /* strip on the RW1C writeback */

#define XHCI_CRCR_LO		0x18U
#define XHCI_CRCR_HI		0x1CU
#define XHCI_CRCR_LO_RCS	0x00000001U
#define XHCI_CRCR_LO_CS		0x00000002U
#define XHCI_CRCR_LO_CA		0x00000004U
#define XHCI_CRCR_LO_CRR	0x00000008U

#define XHCI_DCBAAP_LO		0x30U
#define XHCI_DCBAAP_HI		0x34U

#define XHCI_CONFIG		0x38U
#define XHCI_CONFIG_SLOTS_MASK	0x000000FFU

/* Port register array: the macro takes the ONE-BASED port number (the
 * NetBSD/FreeBSD xhcireg.h shape - slot 0 at op+0x3F0 is unused, port 1
 * lands at op+0x400).  The first board round passed (n-1) here and every
 * port access landed in the reserved word at op+0x3F0: reads returned 0,
 * the PP write never stuck, and the port never saw the live device
 * (measured 2026-09-27: fcc00410=0 while fcc00420=00000e03
 * CCS=1/PED=1/speed=HS under U-Boot's working enumeration). */
#define XHCI_PORTSC(n)		(0x3F0U + (0x10U * (n)))
#define XHCI_PS_CCS		0x00000001U
#define XHCI_PS_PED		0x00000002U
#define XHCI_PS_OCA		0x00000008U
#define XHCI_PS_PR		0x00000010U
#define XHCI_PS_PLS_GET(x)	(((x) >> 5) & 0xFU)
#define XHCI_PS_PLS_SET(x)	(((x) & 0xFU) << 5)
#define XHCI_PS_PP		0x00000200U
#define XHCI_PS_SPEED_GET(x)	(((x) >> 10) & 0xFU)
#define XHCI_PS_SPEED_FULL	0x1U
#define XHCI_PS_SPEED_LOW	0x2U
#define XHCI_PS_SPEED_HIGH	0x3U
#define XHCI_PS_SPEED_SS	0x4U
#define XHCI_PS_LWS		0x00010000U
#define XHCI_PS_CSC		0x00020000U
#define XHCI_PS_PEC		0x00040000U
#define XHCI_PS_WRC		0x00080000U
#define XHCI_PS_OCC		0x00100000U
#define XHCI_PS_PRC		0x00200000U
#define XHCI_PS_PLC		0x00400000U
#define XHCI_PS_CEC		0x00800000U
#define XHCI_PS_DR		0x40000000U

/* ====================== runtime registers ====================== */
#define XHCI_MFINDEX		0x0000U
#define XHCI_IMAN(n)		(0x0020U + (0x20U * (n)))
#define XHCI_IMAN_INTR_PEND	0x00000001U
#define XHCI_IMAN_INTR_ENA	0x00000002U
#define XHCI_IMOD(n)		(0x0024U + (0x20U * (n)))
#define XHCI_ERSTSZ(n)		(0x0028U + (0x20U * (n)))
#define XHCI_ERSTS_SET(x)	((x) & 0xFFFFU)
#define XHCI_ERSTBA_LO(n)	(0x0030U + (0x20U * (n)))
#define XHCI_ERSTBA_HI(n)	(0x0034U + (0x20U * (n)))
#define XHCI_ERDP_LO(n)		(0x0038U + (0x20U * (n)))
#define XHCI_ERDP_HI(n)		(0x003CU + (0x20U * (n)))
#define XHCI_ERDP_BUSY		0x00000008U /* EHB, NetBSD's usage */

/* ====================== doorbell ====================== */
#define XHCI_DB_TARGET(x)	((x) & 0xFFU)

/* ====================== TRB ====================== */
/* Naming per xHCI spec 6.4 / NetBSD xhcireg.h: dw0/dw1 = parameter,
 * dw2 = status, dw3 = control. */
#define TRB_SIZE		16U

#define TRB3_CYCLE		(1U << 0)
#define TRB3_ENT		(1U << 1) /* on a LINK TRB this is TC */
#define TRB3_ISP		(1U << 2) /* interrupt on short packet */
#define TRB3_CHAIN		(1U << 4)
#define TRB3_IOC		(1U << 5)
#define TRB3_IDT		(1U << 6) /* immediate data */
#define TRB3_BSR		(1U << 9) /* Address Device: Block Set Address */
#define TRB3_TYPE_SET(x)	(((x) & 0x3FU) << 10)
#define TRB3_TYPE_GET(x)	(((x) >> 10) & 0x3FU)
#define TRB3_TRT_NONE		(0U << 16)
#define TRB3_TRT_OUT		(2U << 16)
#define TRB3_TRT_IN		(3U << 16)
#define TRB3_DIR_IN		(1U << 16)
#define TRB3_SLOT_ID(x)		(((x) & 0xFFU) << 24)
#define TRB3_SLOT_ID_GET(x)	(((x) >> 24) & 0xFFU)
#define TRB3_EP_ID(x)		(((x) & 0x1FU) << 16)
#define TRB3_EP_ID_GET(x)	(((x) >> 16) & 0x1FU)

#define TRB2_IRQ(x)		(((x) & 0x3FFU) << 22)
#define TRB2_LEN(x)		((x) & 0x1FFFFU)
#define TRB2_REM_GET(x)		((x) & 0xFFFFFFU)
#define TRB2_CODE_GET(x)	(((x) >> 24) & 0xFFU)

/* urb->transfer_flags: OUT at an exact max-packet multiple asks the HCD
 * for an explicit zero-length delimiter.  common/usb_hc.h does not
 * define the flag at this revision; an integrator that translates a
 * host stack's zero-packet request into this bit gets the delimiter
 * below, and no other transfer_flags bit is interpreted. */
#ifndef USBH_URB_ZERO_PACKET
#define USBH_URB_ZERO_PACKET	(1u << 0)
#endif

/* TRB types (spec 6.4.1 Table 131; the transfer-segment values were the
 * first port's off-by-one, board-measured 2026-09-20: a SETUP written as 1
 * executes as NORMAL and the control transfer never completes) */
#define TRB_TYPE_NORMAL		1U
#define TRB_TYPE_SETUP		2U
#define TRB_TYPE_DATA		3U
#define TRB_TYPE_STATUS		4U
#define TRB_TYPE_ISOCH		5U
#define TRB_TYPE_LINK		6U
#define TRB_TYPE_EVENT_DATA	7U
#define TRB_TYPE_NOOP		8U
#define TRB_TYPE_ENABLE_SLOT	9U
#define TRB_TYPE_DISABLE_SLOT	10U
#define TRB_TYPE_ADDRESS_DEV	11U
#define TRB_TYPE_CONFIGURE_EP	12U
#define TRB_TYPE_EVALUATE_CTX	13U
#define TRB_TYPE_RESET_EP	14U
#define TRB_TYPE_STOP_EP	15U
#define TRB_TYPE_SET_TR_DEQUEUE	16U
#define TRB_TYPE_RESET_DEVICE	17U
#define TRB_TYPE_TRANSFER	32U
#define TRB_TYPE_CMD_COMPLETE	33U
#define TRB_TYPE_PORT_STATUS	34U
#define TRB_TYPE_HC_EVENT	37U

/* Completion codes (spec 6.4.5).  The first port carried a +2 slide -
 * 19 was labelled Parameter and 21 Context State; the 2026-09-28 board
 * evidence (a retry code=19 on an already-addressed slot) pinned the
 * spec values: 17=Parameter, 19=Context State, 21=Event Lost. */
#define TRB_CODE_SUCCESS	1U
#define TRB_CODE_DATA_BUFFER_ERR 2U
#define TRB_CODE_BABBLE		3U
#define TRB_CODE_TRANSACTION_ERR 4U
#define TRB_CODE_TRB_ERR	5U
#define TRB_CODE_STALL		6U
#define TRB_CODE_SHORT_PACKET	13U
#define TRB_CODE_RING_UNDERRUN	14U
#define TRB_CODE_RING_OVERRUN	15U
#define TRB_CODE_PARAMETER_ERR	17U
#define TRB_CODE_CONTEXT_STATE	19U
#define TRB_CODE_EVENT_LOST	21U
#define TRB_CODE_CMD_RING_STOPPED 24U
#define TRB_CODE_CMD_ABORTED	25U
#define TRB_CODE_STOPPED	26U
#define TRB_CODE_STOPPED_INVAL	27U
#define TRB_CODE_STOPPED_SHORT	28U

/* ====================== contexts ====================== */
/* Layout per xHCI spec 6.2 (u-boot/Linux/NetBSD agree).  CSZ=1
 * (HCCPARAMS1 bit2, measured) -> 64 bytes per context (16 u32); input
 * contexts u-boot style: the control context owns slot 0, ctx(dci) lives
 * at 64*(dci+1). */
#define SLOT_CTX_DEV_INFO_ROUTE(x)	((x) & 0xFFFFFU)
#define SLOT_CTX_DEV_INFO_SPEED(x)	(((x) & 0xFU) << 20)
#define SLOT_CTX_DEV_INFO_MTT		(1U << 25)
#define SLOT_CTX_DEV_INFO_HUB		(1U << 26)
#define SLOT_CTX_DEV_INFO_LAST_CTX(x)	(((x) & 0x1FU) << 27)
#define SLOT_CTX_SPEED_FS		1U
#define SLOT_CTX_SPEED_LS		2U
#define SLOT_CTX_SPEED_HS		3U
#define SLOT_CTX_DEV_INFO2_MAX_EXIT(x)	((x) & 0xFFFFU)
#define SLOT_CTX_DEV_INFO2_PORT(x)	(((x) & 0xFFU) << 16)
#define SLOT_CTX_DEV_INFO2_NPORTS(x)	(((x) & 0xFFU) << 24)
#define SLOT_CTX_TT_HUB_SLOT(x)		((x) & 0xFU)
#define SLOT_CTX_TT_PORT(x)		(((x) & 0xFU) << 16)
#define SLOT_CTX_TT_THINK(x)		(((x) & 0x3U) << 20)
#define SLOT_CTX_DEV_STATE_ADDR(x)	((x) & 0xFFU)
#define SLOT_CTX_DEV_STATE_SLOT_STATE(x) (((x) & 0x1FU) << 27)
#define SLOT_CTX_STATE_DEFAULT		1U
#define SLOT_CTX_STATE_ADDRESSED	2U
#define SLOT_CTX_STATE_CONFIGURED	3U

/* Endpoint Context (spec 6.2.3; the first port had avg_trb_len in the
 * wrong word - the controller fetched its ring at 0x8_xxxxxxxx and
 * silently did nothing, board-read of the output context 2026-09-20) */
#define EP_CTX_0_INTERVAL_SET(x)	(((x) & 0xFFU) << 16)
#define EP_CTX_1_CERR_SET(x)		(((x) & 0x3U) << 1)
#define EP_CTX_1_EP_TYPE_SET(x)		(((x) & 0x7U) << 3)
#define EP_CTX_1_MAX_BURST_SET(x)	(((x) & 0xFFU) << 8)
#define EP_CTX_1_MAX_PACKET_SET(x)	(((x) & 0xFFFFU) << 16)
#define EP_CTX_2_TR_DEQUEUE_LO(x)	((x) & 0xFFFFFFF0U)
#define EP_CTX_2_CYCLE			(1U << 0)
#define EP_CTX_4_AVG_TRB_LEN(x)		((x) & 0xFFFFU)

/* EP Type field values (Endpoint Context, offset 04h, bits 5:3), per
 * xHCI spec Table 6-9 and cross-checked value for value against it.
 * This is not the USB bmAttributes code; the decode is in
 * xhci_ep_type_of(). */
#define EP_TYPE_CONTROL		4U
#define EP_TYPE_ISOCH_OUT	1U
#define EP_TYPE_BULK_OUT	2U
#define EP_TYPE_INTR_OUT	3U
#define EP_TYPE_ISOCH_IN	5U
#define EP_TYPE_BULK_IN		6U
#define EP_TYPE_INTR_IN		7U

/* input control context (u-boot struct xhci_input_control_ctx):
 * drop in dw0, add in dw1 */
#define ICC_ADD_SLOT		(1U << 0)
#define ICC_ADD_EP0		(1U << 1)

/* ====================== internal structures ====================== */

struct xhci_trb {
	uint32_t dw0;
	uint32_t dw1;
	uint32_t dw2;
	uint32_t dw3;
};

struct xhci_erst_seg {
	uint32_t seg_base_lo;
	uint32_t seg_base_hi;
	uint32_t seg_size;
	uint32_t rsvd;
};

/* TD bookkeeping: with the CherryUSB contract of one URB in flight per
 * endpoint, a ring owns exactly one active TD; transfer events locate it
 * through the ring, never through a per-TRB table. */
struct xhci_td {
	struct usbh_urb *urb;
	uint8_t kind;		/* 0 = normal (bulk/intr), 1 = control */
	uint8_t state;		/* 0 = pending, 1 = done ok, 2 = done err */
	uint8_t short_seen;
	uint8_t killed;		/* ehci's killed dance: 1 = killed, 2 = reaped */
	uint32_t first_idx;
	uint32_t last_idx;
	uint32_t actual;
	int error;
};

struct xhci_ring {
	struct xhci_trb *trbs;
	uint32_t num_trbs;
	uint32_t ep;		/* enqueue index */
	uint8_t cs;		/* producer cycle state */
	uint32_t dequeue;	/* consumer estimate, ring-space accounting */
	usb_osal_sem_t waitsem;
	struct xhci_td td;	/* the single in-flight TD */
	uint8_t inuse;
	uint32_t dbw;		/* doorbells rung on this ring (forensics) */
};

struct xhci_dev {
	uint8_t in_use;
	uint8_t slot_id;
	uint8_t usb_addr;	/* address the pending Address Device sets */
	struct usbh_hubport *hport;
	uint16_t ep0_mps_hw;
	uint32_t ep_configured;	/* dci bitmap with a programmed EP context */
	struct xhci_ring *ep_rings[32];
	uint8_t *input_ctx;	/* 64B aligned, (2+31) context slots */
	uint8_t *dev_ctx;	/* 64B aligned, (1+31) context slots */
	uint32_t ctxsz;
};

struct xhci_hcd {
	struct usbh_bus *bus;
	uintptr_t capbase;
	uintptr_t opregs;
	uintptr_t rts;
	uintptr_t db;
	uint8_t num_ports;
	uint8_t max_slots;
	uint8_t running;
	uint32_t pgsz;
	uint32_t ctxsz_cache;
	/* event ring */
	struct xhci_trb *event_trbs;
	struct xhci_erst_seg *erst;
	uint32_t event_ep;
	uint8_t event_cs;
	/* the command timeout path drains the ring from thread context; the
	 * ISR may preempt that drain and must not double-process */
	volatile uint8_t evt_busy;
	/* command ring: one command in flight, waitsem completion */
	struct xhci_ring cmd_ring;
	usb_osal_sem_t cmd_waitsem;
	volatile uint8_t cmd_busy;
	uint32_t cmd_code;
	uint8_t cmd_slot_id;
	uint32_t cmd_trb_addr;
	/* DCBAA + slots */
	uint64_t *dcbaa;
	struct xhci_dev devs[CONFIG_USBHOST_XHCI_MAX_SLOTS];
	/* transfer rings */
	struct xhci_ring rings[CONFIG_USBHOST_XHCI_RINGS];
	/* forensics 2026-09-28: driver-path ep0 control transfers time out
	 * (-14) while the same-ring enumeration transfers pass; the counters
	 * name the stage that swallows the completion, the timeout dump
	 * prints the controller's view at the moment of the wait failure */
	volatile uint32_t dbg_irq_n;	/* IRQ entries */
	volatile uint32_t dbg_irq_todo_n; /* entries past the nothing-to-do bail */
	volatile uint32_t dbg_tevt_n;	/* transfer events seen */
	volatile uint32_t dbg_drop_urb;	/* events with td.urb == NULL */
	volatile uint32_t dbg_drop_dev;	/* events with no dev/ring */
	volatile uint32_t dbg_ring_ev;	/* ring underrun/overrun events */
	volatile uint32_t dbg_bad_idx;	/* transfer events off the TD's IOC TRB */
	uint32_t dbg_ctrl_n;		/* ep0 control submissions */
	uint32_t dbg_tmo_n;		/* ep0 wait timeouts */
};

/* the multi-HC dispatcher reads this through usbh_register_hc_driver() */
extern const struct usbh_hc_driver xhci_hc_driver;

/* platform hooks: the port power pass (the hub class never issues
 * SetFeature(PORT_POWER) to a roothub, and a device already plugged in at
 * boot reads CCS=1 with its connect-change edge lost) and the diagnostics
 * dumps.  Callers live on the integrator's side. */
void usbh_xhci_post_init(struct usbh_bus *bus);
bool usbh_xhci_bus_ready(uint8_t busid);
void usbh_xhci_dump(struct usbh_bus *bus, bool raw);
void usbh_xhci_stall_dump(struct usbh_bus *bus);
void usbh_xhci_stop_probe(struct usbh_bus *bus);

/* stop and forget the slot still attached to (busid, hub, port): the
 * platform layer calls this from its DEVICE_DISCONNECTED handler, before
 * the hub layer re-initializes the child in place - otherwise the next
 * enumerate reuses a possibly-HALTED slot and every transfer times out
 * (the RTL8821CU modeswitch re-enum, 2026-09-29). */
void usbh_xhci_forget_hport(uint8_t busid, uint8_t hub_index,
			    uint8_t hub_port);

#endif /* USB_HC_XHCI_H */
