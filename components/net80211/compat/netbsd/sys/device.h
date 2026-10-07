/*
 * @file
 * @brief autoconf(9) shell: the port hands out device shells.
 */

#ifndef _COMPAT_SYS_DEVICE_H_
#define _COMPAT_SYS_DEVICE_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/callout.h>

typedef struct device *device_t;
typedef struct device *cfdata_t;

enum devact { DVACT_ACTIVATE = 1, DVACT_DEACTIVATE = 2 };
#define ACT_ACTIVATE DVACT_ACTIVATE
#define ACT_DEACTIVATE DVACT_DEACTIVATE

struct cfattach {
	const char *ca_name;
	size_t ca_devsize;
	int (*ca_match)(device_t, cfdata_t, void *);
	void (*ca_attach)(device_t, device_t, void *);
	int (*ca_detach)(device_t, int);
	int (*ca_activate)(device_t, enum devact);
};

#define CFATTACH_DECL_NEW(name, ssize, matchfn, attachfn, detachfn, \
	activatefn) \
	static const struct cfattach __cfattach_##name __used = { \
	    #name, (ssize), (matchfn), (attachfn), (detachfn), (activatefn) \
	}

struct device {
	char dv_xname[16];
	void *dv_private;
};

#define device_xname(d) ((d) != NULL ? (d)->dv_xname : "urtwn")
#define device_private(d) ((d)->dv_private)
#define device_self(d) ((d))

#define CFARGS_NONE 0

/* dev printing helpers */
#define aprint_normal_dev(dev, fmt, ...) \
	printf("%s: " fmt, device_xname(dev), ##__VA_ARGS__)
#define aprint_error_dev(dev, fmt, ...) \
	printf("%s: " fmt, device_xname(dev), ##__VA_ARGS__)
#define aprint_verbose_dev(dev, fmt, ...) \
	printf("%s: " fmt, device_xname(dev), ##__VA_ARGS__)
#define aprint_debug_dev(dev, fmt, ...) \
	printf("%s: " fmt, device_xname(dev), ##__VA_ARGS__)
#define aprint_naive_dev(dev, fmt, ...) \
	printf("%s: " fmt, device_xname(dev), ##__VA_ARGS__)
#define device_printf(dev, fmt, ...) \
	printf("%s: " fmt, device_xname(dev), ##__VA_ARGS__)
#define aprint_normal(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define aprint_error(fmt, ...) printf(fmt, ##__VA_ARGS__)

static inline int pmf_device_register(device_t dev,
	void (*suspend)(void *, int), void (*resume)(void *, int)) {
	(void) dev;
	(void) suspend;
	(void) resume;
	return 1;
}

static inline void pmf_device_deregister(device_t dev) {
	(void) dev;
}

static inline bool pmf_class_network_register(device_t dev, void *ifp) {
	(void) dev;
	(void) ifp;
	return true;
}

/* deferred configuration is unnecessary here: the firmware blobs are
 * embedded, so the mountroot hook runs synchronously */
static inline void config_mountroot(device_t dev,
	void (*hook)(device_t)) {
	hook(dev);
}

/* the ports run without a kernel lock; the assertion always holds */
#define KERNEL_LOCKED_P() 1

#endif /* _COMPAT_SYS_DEVICE_H_ */
