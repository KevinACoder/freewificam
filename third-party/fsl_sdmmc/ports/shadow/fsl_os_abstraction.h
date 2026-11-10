/*
 * Copyright (c) 2026 freewificam contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Minimal first-party shadow of the SDK's OS abstraction header, narrowed to
 * what the imported SDIO units reference: the opaque handle storage macros, the
 * wait-forever sentinel and the handle typedefs. The upstream header selects a
 * concrete backend (bare-metal, FreeRTOS) at compile time; that selection is the
 * port's own business here, so only the vocabulary is carried.
 *
 * Handle storage is one aligned pointer-sized word: a handle is an opaque id of
 * an object owned by this project's OSA implementation.
 */

#ifndef FSL_OS_ABSTRACTION_H_SHADOW
#define FSL_OS_ABSTRACTION_H_SHADOW

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *osa_semaphore_handle_t;
typedef void *osa_mutex_handle_t;
typedef void *osa_event_handle_t;
typedef uint32_t osa_event_flags_t;

/*! @brief Wait indefinitely (upstream value: (uint32_t)-1). */
#define osaWaitForever_c ((uint32_t)(-1))

/*! @brief Storage size of one opaque handle, in bytes. */
#define OSA_HANDLE_STORAGE_SIZE (sizeof(uintptr_t))

#define OSA_SEMAPHORE_HANDLE_DEFINE(name) uintptr_t name[1]
#define OSA_MUTEX_HANDLE_DEFINE(name)     uintptr_t name[1]
#define OSA_EVENT_HANDLE_DEFINE(name)     uintptr_t name[1]

#ifdef __cplusplus
}
#endif

#endif /* FSL_OS_ABSTRACTION_H_SHADOW */
