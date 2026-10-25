/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linux/acpi.h
 * @brief   The ACPI surface a driver's platform layer names: the object records, the
 *          status convention and the two evaluation entry points.
 *
 * @design declaration-only compatibility face, stated with the interface's own no-ACPI
 *         answers. This port has no ACPI namespace and no firmware table to walk: the
 *         interface's build without the facility answers exactly this way
 *         (include/linux/acpi.h, its `!CONFIG_ACPI` arm: `#define ACPI_HANDLE(dev)
 *         (NULL)` and an `acpi_evaluate_dsm()` that returns NULL), and a driver's
 *         platform-quirk path then finds no handle and returns before it can allocate
 *         anything - which is the truth here rather than a stub accident.
 *
 *         The records are the ACPICA interface's own (include/acpi/actypes.h) with their
 *         exact field order and types, because a driver walks a returned object by type
 *         tag and reads the arm that matches; the values of the type tags are contract.
 *         `acpi_get_handle()` and `acpi_evaluate_object()` are declared rather than
 *         given bodies: their bodies are the ACPICA interpreter's, which this port does
 *         not have, so they belong to the port seam the platform layer will own - never
 *         to a stub here that would answer "not found" to a call the port might one day
 *         route somewhere.
 *         Reference for the face shape: components/linuxkpi/include/linux/usb.h:1-20.
 * @date    09.10.2026
 */

#ifndef _LINUXKPI_LINUX_ACPI_H_
#define _LINUXKPI_LINUX_ACPI_H_

#include "../../linuxkpi_core.h"
#include <linux/uuid.h>

struct device;

/* ---- the ACPICA scalar types [LNX] include/acpi/actypes.h:421-424,644,127 ---- */

typedef u32		acpi_status;	/* All ACPI Exceptions */
typedef char		*acpi_string;	/* Null terminated ASCII string */
typedef void		*acpi_handle;	/* Actually a ptr to a NS Node */
typedef u32		acpi_object_type;
typedef u64		acpi_size;
typedef u64		acpi_io_address;

/* [LNX] include/acpi/actypes.h:646-651, the arms a driver discriminates on */
#define	ACPI_TYPE_ANY		0x00
#define	ACPI_TYPE_INTEGER	0x01
#define	ACPI_TYPE_STRING	0x02
#define	ACPI_TYPE_BUFFER	0x03
#define	ACPI_TYPE_PACKAGE	0x04

/* [LNX] include/acpi/acexcep.h:60 and :57-58: success is the zero status, and the
 * predicates are the interface's own arithmetic. */
#define	AE_OK			(acpi_status)0x0000
#define	ACPI_SUCCESS(a)		(!(a))
#define	ACPI_FAILURE(a)		(a)

/* [LNX] include/acpi/actypes.h:978-981 */
struct acpi_buffer {
	acpi_size	length;	/* Length in bytes of the buffer */
	void		*pointer;	/* pointer to buffer */
};

/* [LNX] include/acpi/actypes.h:968-973; the value is the one the interface's
 * ACPICA-allocated build states (the other arm's 0 is its fixed-buffer build). */
#define	ACPI_ALLOCATE_BUFFER	(acpi_size)(-1)

/* [LNX] include/acpi/actypes.h:911-933, the arms the compiled set names. The union's
 * first member is the type tag every arm repeats at offset 0, which is how a driver
 * reads a returned object. */
union acpi_object {
	acpi_object_type	type;	/* See definition of acpi_ns_type for values */
	struct {
		acpi_object_type	type;	/* ACPI_TYPE_INTEGER */
		u64		value;	/* The actual number */
	} integer;
	struct {
		acpi_object_type	type;	/* ACPI_TYPE_STRING */
		u32		length;	/* # of bytes in string, excluding trailing null */
		char		*pointer;	/* points to the string value */
	} string;
	struct {
		acpi_object_type	type;	/* ACPI_TYPE_BUFFER */
		u32		length;	/* # of bytes in buffer */
		u8		*pointer;	/* points to the buffer */
	} buffer;
	struct {
		acpi_object_type	type;	/* ACPI_TYPE_PACKAGE */
		u32		count;	/* # of elements in package */
		union acpi_object *elements;	/* array of ACPI_OBJECTs */
	} package;
};

/* [LNX] include/acpi/actypes.h:936-939 */
struct acpi_object_list {
	u32			count;
	union acpi_object	*pointer;
};

/* [LNX] include/linux/acpi.h:844: the interface's no-ACPI spelling. A caller that takes
 * a handle and finds none keeps its default path, which is this port's real answer. */
#define	ACPI_HANDLE(dev)	(NULL)

/* [LNX] include/acpi/actypes.h:350 (the os-services frees); nothing on this port can be
 * an allocated ACPI object, so the caller's release obligation is satisfied by the
 * transfer itself and no allocator is named. The macro keeps the interface's expression
 * shape so a call site reads unchanged. */
#define	ACPI_FREE(a)		((void)(a))

/* ---- entry points whose bodies are the interpreter's (port seam) ---- */

/* [LNX] include/acpi/acpixf.h:556-559 */
acpi_status acpi_evaluate_object(acpi_handle object, acpi_string pathname,
				 struct acpi_object_list *external_params,
				 struct acpi_buffer *return_buffer);

/* [LNX] include/acpi/acpixf.h, the namespace lookup */
acpi_status acpi_get_handle(acpi_handle parent, acpi_string pathname,
			    acpi_handle *ret_handle);

/* [LNX] include/linux/acpi.h:1037-1043: the no-ACPI answer, returning no object. */
static __always_inline __unused union acpi_object *
acpi_evaluate_dsm(acpi_handle handle, const guid_t *guid, u64 rev, u64 func,
		  union acpi_object *argv4)
{
	(void)handle;
	(void)guid;
	(void)rev;
	(void)func;
	(void)argv4;
	return NULL;
}

/* [LNX] include/acpi/actypes.h:412,415: the boolean spellings the interface's ACPI
 * header defines and a compiled set reaches through it. */
#ifndef TRUE
#define	TRUE		(1 == 1)
#endif
#ifndef FALSE
#define	FALSE		(1 == 0)
#endif

#endif /* _LINUXKPI_LINUX_ACPI_H_ */
