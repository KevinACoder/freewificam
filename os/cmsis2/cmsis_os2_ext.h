/*
 * @file   cmsis_os2_ext.h
 * @brief  Project extensions to the CMSIS-RTOS2 face (os/cmsis2 owns it).
 *
 * These are NOT CMSIS-RTOS2 standard symbols; the vendored cmsis_os2.h
 * (V2.3.0) must not grow them. Components that run partly in interrupt
 * context (the cherrysh RX chain, later the USB osal) declare their ISR
 * calls through this header so the extension stays visible at the call
 * sites that depend on it.
 */

#ifndef FREEWEBCAM_CMSIS_OS2_EXT_H
#define FREEWEBCAM_CMSIS_OS2_EXT_H

#include "cmsis_os2.h"

/* osThreadFlagsSet for interrupt context. The ThreadX twin routes this to
 * the same tx_thread_flags_set the task-context call uses (callable from
 * ISR on the UP kernel); the distinct name keeps the call sites honest
 * about the context they run in. Same return contract: the flags-after-
 * set value, or the CMSIS error bits. */
uint32_t osThreadFlagsSetFromISR(osThreadId_t thread_id, uint32_t flags);

#endif /* FREEWEBCAM_CMSIS_OS2_EXT_H */
