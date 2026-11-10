/* Fixed host-tier definitions.  The host build is not an image profile: it
 * links the CMSIS-RTOS2 conformance binaries against both kernels, so the
 * kernel identity is supplied by host_freertos_defs.h / host_threadx_defs.h
 * instead of by a generated backend alias.  Everything tunable comes from the
 * generated freewificam_config.h. */
#ifndef FREEWIFICAM_HOST_COMMON_DEFS_H
#define FREEWIFICAM_HOST_COMMON_DEFS_H

#define FREEWIFICAM_NO_CASE_MACROS 1
#include "freewificam_config.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#define RTOS_HOST_TIER 1
#define RTOS_CASE_CMSIS2 1
#ifndef ULOG_ENABLED
#define ULOG_ENABLED 1
#endif

#undef RTOS_BOARD_AUTO_RESET
#undef RTOS_BACKEND_NAME
#undef RTOS_KERNEL_NAME

#endif /* FREEWIFICAM_HOST_COMMON_DEFS_H */
