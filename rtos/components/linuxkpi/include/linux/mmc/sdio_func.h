/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/mmc/sdio_func.h
 * @brief   The SDIO function device and the I/O calls a driver makes on it.
 *
 * @design declaration-only compatibility face, grown per driver. The shapes are
 *         the interface's own (include/linux/mmc/sdio_func.h): the function
 *         device record (which embeds its struct device, so a driver reaches the
 *         device model through &func->dev exactly as it does upstream), the
 *         driver record with its id table, the two device-matching macros, and
 *         the I/O entry points. The bodies are the engine's
 *         (components/linuxkpi/sdio/linuxkpi_sdio.c), which is this layer's
 *         adapter-neutral core; this header only publishes what a driver sees.
 *         Shape reference and discipline:
 *         components/linuxkpi/include/linux/usb.h:1-23.
 *
 *         One deliberate divergence is recorded in the component's DIVERGENCES
 *         file: the interface's device id header is included from here as well
 *         as by the driver, so a driver that names SDIO_DEVICE() gets it either
 *         way.
 * @date    12.10.2026
 */

#ifndef _LINUXKPI_LINUX_MMC_SDIO_FUNC_H_
#define _LINUXKPI_LINUX_MMC_SDIO_FUNC_H_

#include "../../../linuxkpi_core.h"

#include <linux/device.h>
#include <linux/device/driver.h>
#include <linux/device-id/sdio.h>
#include <linux/mmc/pm.h>

struct mmc_card;
struct sdio_func;

typedef void (sdio_irq_handler_t)(struct sdio_func *);

/* one CIS tuple the core does not recognise, kept as read */
struct sdio_func_tuple {
	struct sdio_func_tuple	*next;
	unsigned char		code;
	unsigned char		size;
	unsigned char		data[];
};

/*
 * The function device. Its struct device is embedded, which is what makes
 * dev_to_sdio_func() and sdio_get_drvdata() work the way a driver expects.
 */
struct sdio_func {
	struct mmc_card		*card;		/* the card this function belongs to */
	struct device		dev;		/* the function's device */
	sdio_irq_handler_t	*irq_handler;	/* the claimed IRQ callback */
	unsigned int		num;		/* function number */

	unsigned char		class;		/* standard interface class */
	unsigned short		vendor;		/* vendor code */
	unsigned short		device;		/* product code */

	unsigned int		max_blksize;	/* the function's maximum block size */
	unsigned int		cur_blksize;	/* the block size in effect */

	unsigned int		enable_timeout;	/* maximum enable wait, milliseconds */

	unsigned int		state;		/* SDIO_STATE_* */
#define	SDIO_STATE_PRESENT	(1u << 0)	/* present and usable */

	u8			*tmpbuf;	/* scratch buffer for the 2/4-byte calls */

	u8			major_rev;	/* function revision, major */
	u8			minor_rev;	/* function revision, minor */
	unsigned int		num_info;	/* number of info strings */
	const char		**info;		/* info strings */

	struct sdio_func_tuple	*tuples;
};

#define	sdio_func_present(f)	((f)->state & SDIO_STATE_PRESENT)
#define	sdio_func_set_present(f) ((f)->state |= SDIO_STATE_PRESENT)
#define	sdio_func_id(f)		(dev_name(&(f)->dev))
#define	sdio_get_drvdata(f)	dev_get_drvdata(&(f)->dev)
#define	sdio_set_drvdata(f, d)	dev_set_drvdata(&(f)->dev, d)
#define	dev_to_sdio_func(d)	container_of(d, struct sdio_func, dev)

/*
 * The function driver. The interface splits the driver record into this part
 * and the device-model part it embeds; the embedded part is a real member (the
 * record's layout is the interface's), so its header is included above.
 */
struct sdio_driver {
	char			*name;
	const struct sdio_device_id *id_table;

	int	(*probe)(struct sdio_func *, const struct sdio_device_id *);
	void	(*remove)(struct sdio_func *);
	void	(*shutdown)(struct sdio_func *);

	struct device_driver	drv;
};

/*
 * The two id-table entries. SDIO_DEVICE names a specific part (its class field
 * becomes the "any" value), SDIO_DEVICE_CLASS names a standard interface class
 * (vendor and product become "any").
 */
#define	SDIO_DEVICE(vend, dev)					\
	.class = SDIO_ANY_ID,					\
	.vendor = (vend), .device = (dev)

#define	SDIO_DEVICE_CLASS(dev_class)				\
	.class = (dev_class),					\
	.vendor = SDIO_ANY_ID, .device = SDIO_ANY_ID

#define	SDIO_DEVICE_TABLE(_table)	MODULE_DEVICE_TABLE(sdio, _table)

/* the interface routes registration through a module-owner argument; on this
 * port the image is the module, so THIS_MODULE is NULL and carries no meaning */
struct module;

int	__sdio_register_driver(struct sdio_driver *drv, struct module *owner);
void	sdio_unregister_driver(struct sdio_driver *drv);

#define	sdio_register_driver(drv)	__sdio_register_driver((drv), THIS_MODULE)

/* --- bus claim ------------------------------------------------------------ */

void	sdio_claim_host(struct sdio_func *func);
void	sdio_release_host(struct sdio_func *func);

/* --- function power ------------------------------------------------------- */

int	sdio_enable_func(struct sdio_func *func);
int	sdio_disable_func(struct sdio_func *func);

int	sdio_set_block_size(struct sdio_func *func, unsigned int blksz);

/* --- interrupts ----------------------------------------------------------- */

int	sdio_claim_irq(struct sdio_func *func, sdio_irq_handler_t *handler);
int	sdio_release_irq(struct sdio_func *func);

/* --- byte and word access (one direct transfer) --------------------------- */

u8	sdio_readb(struct sdio_func *func, unsigned int addr, int *err_ret);
u16	sdio_readw(struct sdio_func *func, unsigned int addr, int *err_ret);
u32	sdio_readl(struct sdio_func *func, unsigned int addr, int *err_ret);

void	sdio_writeb(struct sdio_func *func, u8 b, unsigned int addr,
	    int *err_ret);
void	sdio_writew(struct sdio_func *func, u16 b, unsigned int addr,
	    int *err_ret);
void	sdio_writel(struct sdio_func *func, u32 b, unsigned int addr,
	    int *err_ret);

/* --- buffered access (extended transfers) --------------------------------- */

int	sdio_memcpy_fromio(struct sdio_func *func, void *dst, unsigned int addr,
	    int count);
int	sdio_memcpy_toio(struct sdio_func *func, unsigned int addr, void *src,
	    int count);

int	sdio_readsb(struct sdio_func *func, void *dst, unsigned int addr,
	    int count);
int	sdio_writesb(struct sdio_func *func, unsigned int addr, void *src,
	    int count);

/* --- function 0 access ---------------------------------------------------- */

unsigned char	sdio_f0_readb(struct sdio_func *func, unsigned int addr,
	    int *err_ret);
void	sdio_f0_writeb(struct sdio_func *func, unsigned char b, unsigned int addr,
	    int *err_ret);

/* --- power management ----------------------------------------------------- */

mmc_pm_flag_t	sdio_get_host_pm_caps(struct sdio_func *func);
int	sdio_set_host_pm_flags(struct sdio_func *func, mmc_pm_flag_t flags);

#endif /* _LINUXKPI_LINUX_MMC_SDIO_FUNC_H_ */
