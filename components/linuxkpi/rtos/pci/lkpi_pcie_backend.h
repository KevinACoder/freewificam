/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * lkpi_pcie_backend.h - the backend seam of the linuxkpi PCI layer.
 *
 * The PCI engine (linuxkpi_pci.c) speaks Linux semantics to its callers and
 * reaches the host through this narrow interface only: bring the bus up and
 * report what enumerated, tear it down, one config access in each direction, one
 * BAR window, one interrupt request and its release, and a coherent / streaming
 * DMA pair. The seam carries no register, iATU, PHY or link-training vocabulary -
 * a backend is anything that can enumerate a function, answer a config cycle, hand
 * out an address window and deliver an interrupt.
 *
 * Implementations:
 *   - the host mock suite plugs in a synthetic host and a scripted device model
 *     (the Gate 1 shape: a config-space constant table, a BAR memory region and a
 *     scripted doorbell, no hardware);
 *   - the target build maps the same calls onto the board's root-complex driver
 *     and its interrupt domain (ports/pcie/), which is the only party that knows
 *     the controller's registers and windows.
 *
 * Enumeration contract: bus_up fills up to `max` opaque device handles with one
 * identity record each and returns the count. The engine keeps the handle as the
 * device identity and never dereferences it; everything it needs about the device
 * travels in the record. bus_down releases the whole set at once.
 *
 * Interrupt contract: irq_setup asks for one vector of `kind` and returns the
 * INTID to arm; the backend has already made the device point its message at that
 * vector, and calls `handler(arg)` when the device raises it. irq_clear releases
 * the vector. kind is MSI or MSI-X; the seam does not carry INTx (a legacy line
 * has no consumer in this port and a criterion that named it would be a second
 * delivery path - see the line's design note).
 *
 * DMA contract: dma_alloc hands out a coherent buffer (CPU address and bus
 * address); dma_map / dma_unmap make a streaming buffer visible in a direction;
 * dma_sync re-makes a live streaming mapping visible in a direction, for the
 * hand-back the map/unmap pair cannot express while the mapping stays live. The
 * seam carries no cache vocabulary on purpose - the adapter below it owns the
 * cache maintenance the host needs, and a synthetic backend ignores it entirely.
 *
 * Error codes are this seam's own; the low values are kept identical to the
 * controller library's FError numbering the adapter maps from, so every mapping in
 * the adapter keeps its meaning. The 101..104 group is the seam's addition (link,
 * device, vector and DMA-mask conditions the library has no code for) and sits in
 * the library's unused range. Error values travel as the negative of the code.
 *
 * @design ports & adapters: the engine is the adapter-neutral core, a backend is
 *         the adapter; the seam is deliberately narrow (twelve operations, one
 *         device record) so a second backend cannot fork the engine. Shape
 *         reference: this project's USB seam, components/linuxkpi/usb/
 *         lkpi_usb_backend.h:1-37 (the vocabulary discipline, the error-code
 *         family and the @design note this header follows).
 */

#ifndef LKPI_PCIE_BACKEND_H
#define LKPI_PCIE_BACKEND_H

#include "linuxkpi_core.h"

/* the most BARs the identity record carries (PCI's standard function has six) */
#define	LKPI_PCIE_BE_MAX_BARS		6

/* irq_setup() kinds */
#define	LKPI_PCIE_BE_IRQ_MSI		0
#define	LKPI_PCIE_BE_IRQ_MSIX		1

/* dma_map()/dma_unmap() directions; the values are enum dma_data_direction's
 * (linux/dma-mapping.h) so the engine's pass-through is the identity */
#define	LKPI_PCIE_BE_DMA_BIDIRECTIONAL	0
#define	LKPI_PCIE_BE_DMA_TO_DEVICE	1
#define	LKPI_PCIE_BE_DMA_FROM_DEVICE	2
#define	LKPI_PCIE_BE_DMA_NONE		3

/* error codes (the negative of these travels in return values) */
#define	LKPI_PCIE_BE_ERR_FAIL		1	/* generic failure */
#define	LKPI_PCIE_BE_ERR_INVAL		4	/* invalid argument */
#define	LKPI_PCIE_BE_ERR_TIMEOUT	5	/* the access timed out */
#define	LKPI_PCIE_BE_ERR_RANGE		7	/* out of range */
#define	LKPI_PCIE_BE_ERR_XFER		12	/* the transfer failed */
/* seam additions (the library's unused range) */
#define	LKPI_PCIE_BE_ERR_NOLINK		101	/* no link came up */
#define	LKPI_PCIE_BE_ERR_NODEV		102	/* no such device */
#define	LKPI_PCIE_BE_ERR_NOVECTOR	103	/* no interrupt vector available */
#define	LKPI_PCIE_BE_ERR_DMAMASK	104	/* the host cannot satisfy the DMA mask */

/* one address window of a function; the backend has already mapped it, so phys is
 * both the bus address the device sees and the CPU address the window answers at
 * (this port identity-maps its device windows) */
struct lkpi_pcie_be_bar {
	u64	phys;
	u64	size;
	u32	flags;		/* IORESOURCE_* (linux/pci.h) */
};

/* what enumerated; the engine's device record and the driver's id table both read
 * this, so it carries identity and geometry only - never a register image. The
 * two link fields are the anchor a bring-up log prints (generation and width). */
struct lkpi_pcie_be_desc {
	u16	vendor;
	u16	device;
	u16	subsys_vendor;
	u16	subsys_device;
	u32	class_code;	/* class/subclass/prog-if, 24 bits */
	u8	revision;
	u8	bus;		/* the bus the function sits on */
	u8	dev;		/* device number on that bus */
	u8	func;		/* function number */
	u8	msi_capable;	/* the function carries an MSI capability */
	u8	msix_capable;	/* the function carries an MSI-X capability */
	u8	link_gen;	/* link generation, 0 if unknown */
	u8	link_width;	/* link width in lanes, 0 if unknown */
	u32	host_dma_mask;	/* the widest DMA mask the host can guarantee */
	struct lkpi_pcie_be_bar bar[LKPI_PCIE_BE_MAX_BARS];
};

struct lkpi_pcie_be_dev;

struct lkpi_pcie_be_ops {
	/* Bring the bus up, enumerate it and fill up to `max` handles and records.
	 * Returns the number of devices (>= 0) or the negative of a code above. */
	int	(*bus_up)(void *ctx, struct lkpi_pcie_be_dev **devs,
		    struct lkpi_pcie_be_desc *descs, int max, int *count);
	/* Tear the bus down; the seam calls it at most once per bus_up(). */
	void	(*bus_down)(void *ctx);
	/* One config-space access of 1, 2 or 4 bytes. */
	int	(*cfg_read)(void *ctx, struct lkpi_pcie_be_dev *d, u16 off,
		    int size, u32 *val);
	int	(*cfg_write)(void *ctx, struct lkpi_pcie_be_dev *d, u16 off,
		    int size, u32 val);
	/* Map a BAR and hand back a CPU address the window answers at. */
	int	(*bar_map)(void *ctx, struct lkpi_pcie_be_dev *d, int index,
		    void **mmio);
	/* Ask for one vector of `kind`; the backend arms the device and returns the
	 * INTID to register a handler for, calling handler(arg) on delivery. */
	int	(*irq_setup)(void *ctx, struct lkpi_pcie_be_dev *d, int kind,
		    void (*handler)(void *), void *arg, int *irqn);
	/* Release the vector irq_setup returned. */
	void	(*irq_clear)(void *ctx, struct lkpi_pcie_be_dev *d, int irqn);
	/* One coherent buffer: CPU address and bus address, no maintenance needed. */
	int	(*dma_alloc)(void *ctx, size_t len, void **cpu, u64 *bus);
	void	(*dma_free)(void *ctx, void *cpu, u64 bus, size_t len);
	/* Make a streaming buffer visible in `dir` (the adapter owns the cache
	 * maintenance; the bus address is handed back for the device). */
	int	(*dma_map)(void *ctx, void *cpu, size_t len, int dir, u64 *bus);
	void	(*dma_unmap)(void *ctx, void *cpu, size_t len, int dir);
	/* Re-make a live streaming mapping visible in `dir` without unmapping it
	 * (the CPU and the device hand the same buffer back and forth). The
	 * maintenance is the adapter's, exactly as for dma_map / dma_unmap, and
	 * `dir` selects it: a to-device buffer is cleaned, a from-device buffer
	 * invalidated. An adapter that needs cache maintenance for its streaming
	 * buffers must implement it, because the mapping stays live. */
	void	(*dma_sync)(void *ctx, void *cpu, size_t len, int dir);
};

/* install the backend (the host mock harness and the target bring-up call this
 * once before the first bus_up; without a backend every entry is refused with
 * -LKPI_PCIE_BE_ERR_NODEV) */
void	lkpi_pcie_be_register(const struct lkpi_pcie_be_ops *ops, void *ctx);

/* non-zero once a backend is installed */
bool	lkpi_pcie_be_ready(void);

/* the seam's entries; each returns 0 or the negative of a code above */
int	lkpi_pcie_be_bus_up(struct lkpi_pcie_be_dev **devs,
	    struct lkpi_pcie_be_desc *descs, int max, int *count);
void	lkpi_pcie_be_bus_down(void);
int	lkpi_pcie_be_cfg_read(struct lkpi_pcie_be_dev *d, u16 off, int size,
	    u32 *val);
int	lkpi_pcie_be_cfg_write(struct lkpi_pcie_be_dev *d, u16 off, int size,
	    u32 val);
int	lkpi_pcie_be_bar_map(struct lkpi_pcie_be_dev *d, int index, void **mmio);
int	lkpi_pcie_be_irq_setup(struct lkpi_pcie_be_dev *d, int kind,
	    void (*handler)(void *), void *arg, int *irqn);
void	lkpi_pcie_be_irq_clear(struct lkpi_pcie_be_dev *d, int irqn);
int	lkpi_pcie_be_dma_alloc(size_t len, void **cpu, u64 *bus);
void	lkpi_pcie_be_dma_free(void *cpu, u64 bus, size_t len);
int	lkpi_pcie_be_dma_map(void *cpu, size_t len, int dir, u64 *bus);
void	lkpi_pcie_be_dma_unmap(void *cpu, size_t len, int dir);
void	lkpi_pcie_be_dma_sync(void *cpu, size_t len, int dir);

#endif /* LKPI_PCIE_BACKEND_H */
