/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * mockvec.h - loader for the container's vector data files.
 *
 * The suite reads its expected byte strings from the shared vector factory
 * (tools/mock/vectors) through this loader, so an assertion compares the
 * captured bytes against the decoded file content - never against a constant
 * that merely mentions the file.  The container's checker owns format
 * validation; the loader re-checks the parts it depends on (the length the
 * header declares must equal the decoded payload) and reports a readable
 * error otherwise.
 */

#ifndef MOCKVEC_H
#define MOCKVEC_H

#include <stddef.h>
#include <stdint.h>

#define MOCKVEC_MAX_PAYLOAD	4096
#define MOCKVEC_ID_MAX		96

struct mockvec {
	char		id[MOCKVEC_ID_MAX];	/* file stem, the vector id */
	char		seg[8];
	char		dir[8];
	char		anchor[64];
	unsigned char	payload[MOCKVEC_MAX_PAYLOAD];
	size_t		len;
};

/* load <dir>/<seg>/<file>; returns 0 on success, -1 with a message in err */
int mockvec_load(const char *root, const char *seg, const char *file,
    struct mockvec *out, char *err, size_t errlen);

/* byte-compare helper: returns -1 when equal, else the first offset that
 * differs (or the shorter length when one side is a prefix) */
int mockvec_diff(const unsigned char *got, size_t got_len,
    const struct mockvec *v);

#endif /* MOCKVEC_H */
