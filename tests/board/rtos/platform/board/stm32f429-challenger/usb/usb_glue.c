/*
 * @file   usb_glue.c
 * @brief  F429 board glue for the imported CherryUSB DWC2 host port.
 *
 * The board facts are the V2 base schematic's sheet 8: the USB HOST socket is
 * P2 (USB-A) with VBUS tied to 5V, HOST_DN/HOST_DP are PB14/PB15 selected by
 * J74-J76 in the HOS position, and the controller is OTG_HS in full-speed
 * mode with 48 MHz from PLLQ.  The core runs without a data cache, so the
 * stack's cache-maintenance contract is compiled out; the only DMA rule that
 * remains is 4-byte buffer alignment.
 *
 * The imported DWC2 port expects three things from the board:
 *   - usb_hc_low_level_init()/deinit(), which are plain externs in this pin;
 *   - dwc2_get_user_params(), the FIFO/PHY/GCCFG table;
 *   - usb_osal_timer_service_start(), supplied by the shared CMSIS-RTOS2 OSAL.
 * It supplies usb_hc_init()/usbh_submit_urb()/USBH_IRQHandler() itself, because
 * this image is single-controller and does not define CONFIG_USBHOST_MULT_HC.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_conf.h"
#include "log.h"

#include "os/cmsis2/cmsis2.h"

#include "usbh_core.h"
#include "usbh_hub.h"
#include "usb_dwc2_param.h"

#include "usb_glue.h"

#define RCC_BASE		0x40023800UL
#define RCC_AHB1ENR		(*(volatile uint32_t *)(RCC_BASE + 0x30UL))

#define GPIOB_BASE		0x40020400UL
#define GPIOB_MODER		(*(volatile uint32_t *)(GPIOB_BASE + 0x00UL))
#define GPIOB_OSPEEDR		(*(volatile uint32_t *)(GPIOB_BASE + 0x08UL))
#define GPIOB_PUPDR		(*(volatile uint32_t *)(GPIOB_BASE + 0x0CUL))
#define GPIOB_AFR		((volatile uint32_t *)(GPIOB_BASE + 0x20UL))

#define USB_OTG_HS_BASE		0x40040000UL
#define OTG_HS_IRQN		77U
#define OTG_HS_IRQ_REG		(OTG_HS_IRQN / 32U)	/* NVIC index 2 */
#define OTG_HS_IRQ_BIT		(1UL << (OTG_HS_IRQN % 32U))

#define NVIC_ISER(n)		(*(volatile uint32_t *)(0xE000E100UL + 4UL * (n)))
#define NVIC_ICER(n)		(*(volatile uint32_t *)(0xE000E180UL + 4UL * (n)))
#define NVIC_ICPR(n)		(*(volatile uint32_t *)(0xE000E280UL + 4UL * (n)))
#define NVIC_IPR(n)		(*(volatile uint8_t *)(0xE000E400UL + (n)))

#define RCC_AHB1ENR_GPIOB	(1UL << 1)
/* F429: OTGHSEN is bit 29; bit 30 is the separate OTG_HS ULPI clock. */
#define RCC_AHB1ENR_OTGHS	(1UL << 29)

static volatile unsigned int usb_board_irqs;
static volatile int usb_board_started;

/* Enable the OTG_HS and GPIOB clocks, pin PB14/PB15 to AF12 and bind the
 * controller's line.  Idempotent: a second call rewrites the same values. */
static void usb_board_hw_up(void)
{
	RCC_AHB1ENR |= RCC_AHB1ENR_GPIOB | RCC_AHB1ENR_OTGHS;
	(void)RCC_AHB1ENR;

	GPIOB_MODER &= ~((3UL << 28) | (3UL << 30));
	GPIOB_MODER |= ((2UL << 28) | (2UL << 30));
	GPIOB_OSPEEDR |= ((3UL << 28) | (3UL << 30));
	GPIOB_PUPDR &= ~((3UL << 28) | (3UL << 30));
	GPIOB_AFR[1] &= ~((0xFUL << 24) | (0xFUL << 28));
	GPIOB_AFR[1] |= ((12UL << 24) | (12UL << 28));

	NVIC_ICER(OTG_HS_IRQ_REG) = OTG_HS_IRQ_BIT;
	NVIC_ICPR(OTG_HS_IRQ_REG) = OTG_HS_IRQ_BIT;
	NVIC_IPR(OTG_HS_IRQN) = (uint8_t)BOARD_IRQ_PRIORITY_API_CALL_RAW;
	NVIC_ISER(OTG_HS_IRQ_REG) = OTG_HS_IRQ_BIT;

	__asm__ __volatile__("dsb" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

int usb_board_init(unsigned busid)
{
	if (busid != 0U) {
		return -1;
	}

	usb_board_hw_up();
	usb_osal_timer_service_start();
	usb_board_started = 1;

	return 0;
}

/* --- the linuxkpi URB engine's board seam --------------------------------- */

int
usb_board_bus_ready(unsigned int busid)
{
	return (busid == 0U && usb_board_started) ? 1 : 0;
}

int
usb_board_bus_is_xhci(unsigned int busid)
{
	(void)busid;
	return 0;
}

/* The single-controller DWC2 port's handler takes no bus id; the caller (the
 * engine's drain thread or the board's own ISR) holds the stack critical
 * section.  On this board the NVIC line stays enabled, so the ISR path runs
 * the handler too; this entry is the engine's poll fallback. */
void
usb_board_irq_dispatch(unsigned int busid)
{
	if (busid == 0U)
		USBH_IRQHandler(0);
}

void usb_board_after_start(unsigned busid)
{
	if (busid != 0U) {
		return;
	}

	/* The hub thread is created by usbh_initialize() and calls usb_hc_init()
	 * before it receives messages.  Seeding the root port's connect-change
	 * bit here makes the first pass read the port even when the disk was
	 * already attached at power-on and DWC2's drivebus produced no new edge. */
	g_usbhost_bus[0].hcd.roothub.int_buffer[0] |= (1U << 1);
	usbh_hub_thread_wakeup(&g_usbhost_bus[0].hcd.roothub);
}

uintptr_t usb_board_reg_base(unsigned busid)
{
	return (busid == 0U) ? (uintptr_t)USB_OTG_HS_BASE : (uintptr_t)0;
}

unsigned int usb_board_irq_count(unsigned int busid)
{
	return (busid == 0U) ? usb_board_irqs : 0U;
}

uint32_t usb_board_port_status(void)
{
	return *(volatile uint32_t *)(USB_OTG_HS_BASE + 0x440UL);
}

static const char *usbh_event_name(uint8_t event)
{
	switch (event) {
	case USBH_EVENT_INIT:
		return "init";
	case USBH_EVENT_DEINIT:
		return "deinit";
	case USBH_EVENT_DEVICE_CONFIGURED:
		return "device-configured";
	case USBH_EVENT_DEVICE_DISCONNECTED:
		return "device-disconnected";
	case USBH_EVENT_INTERFACE_START:
		return "interface-start";
	case USBH_EVENT_INTERFACE_STOP:
		return "interface-stop";
	case USBH_EVENT_INTERFACE_UNSUPPORTED:
		return "interface-unsupported";
	default:
		return "other";
	}
}

void usb_board_event_handler(uint8_t busid, uint8_t hub_index, uint8_t hub_port,
			     uint8_t intf, uint8_t event)
{
	LOG_INFO("usbh: bus%u event %s hub=%u port=%u intf=%u\n",
		 (unsigned)busid, usbh_event_name(event), (unsigned)hub_index,
		 (unsigned)hub_port, (unsigned)intf);
}

/* The stack's console sink: format one piece and let the log sink assemble the
 * line.  The USB formatter writes a header fragment without a newline and then
 * the message; appending a newline here would split every USB line in two. */
void usbh_console_printf(const char *fmt, ...)
{
	char line[192];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);

	LOG_INFO("%s", line);
}

/* --- imported DWC2 port hooks -------------------------------------------- */

void usb_hc_low_level_init(struct usbh_bus *bus)
{
	(void)bus;
	usb_board_hw_up();
}

void usb_hc_low_level_deinit(struct usbh_bus *bus)
{
	(void)bus;
	NVIC_ICER(OTG_HS_IRQ_REG) = OTG_HS_IRQ_BIT;
}

/* F429 OTG_HS, full-speed host: the same FIFO/GCCFG table the upstream ST glue
 * carries for PB14/PB15 in the F4 branch (1024-word FIFO: 622 + 128 + 256). */
void dwc2_get_user_params(uint32_t reg_base, struct dwc2_user_params *params)
{
	(void)reg_base;

	memset(params, 0, sizeof(*params));
	params->phy_type = DWC2_PHY_TYPE_PARAM_FS;
	params->host_dma_desc_enable = false;
	params->host_rx_fifo_size = 622;
	params->host_nperio_tx_fifo_size = 128;
	params->host_perio_tx_fifo_size = 256;
	/* USB_OTG_GCCFG_PWRDWN | USB_OTG_GCCFG_NOVBUSSENS: P2's VBUS is hardwired
	 * to 5V, so the core must not wait for a session-valid indication. */
	params->host_gccfg = (1U << 16) | (1U << 21);
	params->total_fifo_size = 1024;
}

/*
 * Interrupt-storm guard.  The OTG_HS line is level-triggered: a channel whose
 * HCINT/HAINT is not acknowledged re-arms it immediately and the handler
 * re-enters forever, so no thread runs again and the console stops mid-line.
 * That is the shape of the 2026-11-15 data-plane data-frame wedge.  Past a ceiling
 * that no legitimate full-speed transfer can produce in one tick, the glue
 * masks the line and records why, so the failure is named and the threads
 * (the case's watchdog among them) can print the channel state.
 */
volatile unsigned int usb_board_irq_storm;
unsigned int usb_board_irq_storm_get(void)
{
	return usb_board_irq_storm;
}

static volatile uint32_t usb_irq_burst_tick;
static volatile uint32_t usb_irq_burst;

#define USB_IRQ_STORM_BURST	2000U

/* The DWC2 host registers this dump reads, by the controller's own offsets
 * (RM0090 35.6): GINTSTS 0x14, HAINT 0x518, HAINTMSK 0x51C, HCCHAR 0x500,
 * HCINT 0x508, HCINTMSK 0x50C, HCTSIZ 0x510 (0x20 per channel). */
void usb_board_dwc2_dump(void)
{
	volatile uint32_t *base = (volatile uint32_t *)USB_OTG_HS_BASE;
	unsigned int i;

	LOG_ANCHOR("f429: dwc2 gintsts=%08x haint=%08x haintmsk=%08x storm=%u irqs=%u\n",
		   (unsigned int)base[0x14 / 4], (unsigned int)base[0x518 / 4],
		   (unsigned int)base[0x51C / 4], usb_board_irq_storm,
		   usb_board_irqs);
	for (i = 0U; i < 12U; i++) {
		unsigned int ch = 0x500U + 0x20U * i;
		uint32_t hcchar = base[(ch + 0x00U) / 4];

		if ((hcchar & 0x80000000UL) == 0U)
			continue;
		LOG_ANCHOR("f429: dwc2 ch%u hcchar=%08x hcint=%08x hcintmsk=%08x hctsiz=%08x\n",
			   i, (unsigned int)hcchar,
			   (unsigned int)base[(ch + 0x08U) / 4],
			   (unsigned int)base[(ch + 0x0CU) / 4],
			   (unsigned int)base[(ch + 0x10U) / 4]);
	}
}

void OTG_HS_IRQHandler(void)
{
	uint32_t now = (uint32_t)osKernelGetTickCount();

	usb_board_irqs++;
	if (usb_board_irq_storm != 0U) {
		return;
	}
	if (now != usb_irq_burst_tick) {
		usb_irq_burst_tick = now;
		usb_irq_burst = 0U;
	}
	if (++usb_irq_burst > USB_IRQ_STORM_BURST) {
		usb_board_irq_storm = 1U;
		NVIC_ICER(OTG_HS_IRQ_REG) = OTG_HS_IRQ_BIT;
		return;
	}
	USBH_IRQHandler(0);
}
