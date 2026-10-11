/**
 * @file    msc_app.c
 * @brief   Application half of the mass-storage class: run/stop hooks.
 * @author  zhugengyu
 * @date    08.10.2026
 * @design  template method - the imported class driver owns matching and the
 *          transport (class/msc/usbh_msc.c) and calls the weak run/stop hooks;
 *          this file is the integrator's half, the same split the prior
 *          carrier used (its usbh_msc.c's run hook).
 *
 * On connect the library has already built the device context; the run hook
 * performs the SCSI identity handshake (ready -> inquiry -> capacity) and only
 * then is the disk usable. Failures are printed as anchors: a disk that never
 * becomes ready is otherwise indistinguishable from a controller that never
 * enumerated.
 */

#include <stdint.h>

#include "log.h"

#include "usbh_core.h"
#include "usbh_msc.h"

#include "msc_app.h"

#define MSC_APP_MAX 4

static struct usbh_msc *msc_app_table[MSC_APP_MAX];

void usbh_msc_run(struct usbh_msc *msc_class)
{
	uint32_t slot = (uint32_t)(msc_class->sdchar - 'a');
	int ret;

	if (slot >= MSC_APP_MAX) {
		LOG_ERROR("usbh_msc: ordinal %c outside the table\n", (char)msc_class->sdchar);
		return;
	}

	ret = usbh_msc_scsi_init(msc_class);
	if (ret < 0) {
		LOG_ERROR("usbh_msc: init failed bus=%u err=%d\n",
			  msc_class->hport->bus->busid, ret);
		return;
	}

	msc_app_table[slot] = msc_class;
	LOG_INFO("usbh_msc: ready bus=%u dev=%u blocks=%u bsize=%u (%u MiB)\n",
		  msc_class->hport->bus->busid, msc_class->hport->dev_addr,
		  (unsigned)msc_class->blocknum, (unsigned)msc_class->blocksize,
		  (unsigned)(((uint64_t)msc_class->blocknum * msc_class->blocksize) >> 20));
}

void usbh_msc_stop(struct usbh_msc *msc_class)
{
	uint32_t slot = (uint32_t)(msc_class->sdchar - 'a');

	if (slot < MSC_APP_MAX) {
		msc_app_table[slot] = 0;
	}
}

struct usbh_msc *usb_msc_instance(void)
{
	for (unsigned i = 0; i < MSC_APP_MAX; i++) {
		if (msc_app_table[i] != 0) {
			return msc_app_table[i];
		}
	}
	return 0;
}

int usb_msc_ordinal(void)
{
	for (unsigned i = 0; i < MSC_APP_MAX; i++) {
		if (msc_app_table[i] != 0) {
			return (int)i;
		}
	}
	return -1;
}
