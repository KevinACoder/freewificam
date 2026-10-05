#ifndef _LINUXKPI_LINUX_FIRMWARE_H_
#define _LINUXKPI_LINUX_FIRMWARE_H_
#include "../../linuxkpi_core.h"

struct firmware {
	size_t		size;
	const u8	*data;
	void		*priv;		/* the allocation to free */
};

/* Synchronous: the registry is in-memory (compiled-in arrays), so this
 * never sleeps meaningfully.  The Linux path form "rtw88/rtw8821c_fw.bin"
 * is accepted; only the base name is looked up. */
int lkpi_request_firmware(const struct firmware **, const char *name,
    struct device *dev);
void lkpi_release_firmware(const struct firmware *);

#define	request_firmware(fwp, name, dev) \
	lkpi_request_firmware((fwp), (name), (dev))
#define	request_firmware_nowait(mod, need, name, dev, gfp, ctx, cont) \
	lkpi_request_firmware_nowait((name), (dev), (ctx), (cont))
#define	release_firmware(fw)		lkpi_release_firmware(fw)

typedef void (*lkpi_fw_cont)(const struct firmware *, void *);
int lkpi_request_firmware_nowait(const char *name, struct device *dev,
    void *context, lkpi_fw_cont cont);

#endif
