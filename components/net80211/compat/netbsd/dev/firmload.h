/*
 * @file
 * @brief firmware(9) shell; blobs resolve through the port.
 */

#ifndef _COMPAT_DEV_FIRMLOAD_H_
#define _COMPAT_DEV_FIRMLOAD_H_

#include <sys/cdefs.h>
#include <sys/types.h>

struct wlan_firmware;
typedef struct wlan_firmware *firmware_handle_t;

int firmware_open(const char *drv, const char *img,
	firmware_handle_t *handle);
size_t firmware_get_size(firmware_handle_t handle);
void *firmware_malloc(size_t len);
int firmware_read(firmware_handle_t handle, size_t offset, void *buf,
	size_t len);
void firmware_close(firmware_handle_t handle);
void firmware_free(void *buf, size_t len);

#endif /* _COMPAT_DEV_FIRMLOAD_H_ */
